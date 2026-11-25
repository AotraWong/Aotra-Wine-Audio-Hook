/*
 * audio_switch_hook.c
 *
 * DYLD interposer that makes Wine/GPTK CoreAudio output automatically
 * follow macOS default output device changes (e.g., speaker → earphone).
 *
 * Problem: Wine's CoreAudio driver queries the default output device once
 * when a game opens its audio stream, then never listens for changes.
 * Plugging in headphones mid-game leaves sound on the speakers.
 *
 * Solution: Interpose AudioOutputUnitStart/AudioComponentInstanceDispose
 * to track active output AudioUnits, and register a CoreAudio property
 * listener for kAudioHardwarePropertyDefaultOutputDevice. When the default
 * device changes, we stop each tracked unit, switch its device, and restart.
 *
 * Usage:
 *   clang -dynamiclib -framework CoreAudio -framework AudioToolbox \
 *         -arch arm64 -o libaudioswitchhook.dylib audio_switch_hook.c
 *
 *   DYLD_INSERT_LIBRARIES=/path/to/libaudioswitchhook.dylib wine game.exe
 *
 * Build for x86_64 (Rosetta/CrossOver):
 *   clang -dynamiclib -framework CoreAudio -framework AudioToolbox \
 *         -arch x86_64 -o libaudioswitchhook_x86.dylib audio_switch_hook.c
 *
 * Copyright 2024 – Public Domain / MIT License
 */

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Logging                                                           */
/* ------------------------------------------------------------------ */

#define LOG_PREFIX "[audio-hook] "

static int g_verbose = 0;

#define LOG(fmt, ...)   fprintf(stderr, LOG_PREFIX fmt "\n", ##__VA_ARGS__)
#define VLOG(fmt, ...)  do { if (g_verbose) LOG(fmt, ##__VA_ARGS__); } while (0)

/* ------------------------------------------------------------------ */
/*  Tracked AudioUnit registry                                        */
/* ------------------------------------------------------------------ */

#define MAX_TRACKED_UNITS 64

typedef struct {
    AudioUnit   unit;
    int         running;    /* was the unit started? */
} TrackedUnit;

static TrackedUnit  g_units[MAX_TRACKED_UNITS];
static int          g_num_units = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/*  Helpers                                                           */
/* ------------------------------------------------------------------ */

static AudioObjectID get_default_output_device(void)
{
    AudioObjectID deviceId = kAudioObjectUnknown;
    UInt32 size = sizeof(deviceId);
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope    = kAudioObjectPropertyScopeGlobal,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        .mElement  = kAudioObjectPropertyElementMain
#else
        .mElement  = kAudioObjectPropertyElementMaster
#endif
    };
    AudioObjectGetPropertyData(kAudioObjectSystemObject,
                               &addr, 0, NULL, &size, &deviceId);
    return deviceId;
}

static const char *get_device_name(AudioObjectID deviceId)
{
    static char name[256];
    CFStringRef cfName = NULL;
    UInt32 size = sizeof(cfName);
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioObjectPropertyName,
        .mScope    = kAudioObjectPropertyScopeGlobal,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        .mElement  = kAudioObjectPropertyElementMain
#else
        .mElement  = kAudioObjectPropertyElementMaster
#endif
    };

    OSStatus err = AudioObjectGetPropertyData(deviceId, &addr,
                                              0, NULL, &size, &cfName);
    if (err == noErr && cfName) {
        CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8);
        CFRelease(cfName);
        return name;
    }
    snprintf(name, sizeof(name), "device#%u", (unsigned)deviceId);
    return name;
}

/* ------------------------------------------------------------------ */
/*  Switch all tracked units to a new output device                   */
/* ------------------------------------------------------------------ */

static void switch_units_to_device(AudioObjectID newDevice)
{
    TrackedUnit snapshot[MAX_TRACKED_UNITS];
    int count = 0;

    pthread_mutex_lock(&g_lock);
    count = g_num_units;
    if (count > 0) {
        memcpy(snapshot, g_units, (size_t)count * sizeof(TrackedUnit));
    }
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < count; i++) {
        AudioUnit unit = snapshot[i].unit;
        int was_running = snapshot[i].running;
        OSStatus err;

        /* 1. Stop the unit if it's running */
        if (was_running) {
            err = AudioOutputUnitStop(unit);
            VLOG("  Stop unit[%d]=%p → %d", i, unit, (int)err);
        }

        /* 2. Uninitialize so we can change properties */
        err = AudioUnitUninitialize(unit);
        VLOG("  Uninitialize unit[%d]=%p → %d", i, unit, (int)err);

        /* 3. Set the new device */
        err = AudioUnitSetProperty(unit,
                                   kAudioOutputUnitProperty_CurrentDevice,
                                   kAudioUnitScope_Global, 0,
                                   &newDevice, sizeof(newDevice));
        if (err == noErr) {
            LOG("  Switched unit[%d]=%p → %s", i, unit,
                get_device_name(newDevice));
        } else {
            LOG("  FAILED to switch unit[%d]=%p: err=%d", i, unit, (int)err);
        }

        /* 4. Re-initialize */
        err = AudioUnitInitialize(unit);
        VLOG("  Initialize unit[%d]=%p → %d", i, unit, (int)err);

        /* 5. Restart if it was running */
        if (was_running) {
            err = AudioOutputUnitStart(unit);
            VLOG("  Start unit[%d]=%p → %d", i, unit, (int)err);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  CoreAudio property listener callback                              */
/* ------------------------------------------------------------------ */

static OSStatus on_default_device_changed(
    AudioObjectID                       inObjectID,
    UInt32                              inNumberAddresses,
    const AudioObjectPropertyAddress    inAddresses[],
    void                               *inClientData)
{
    (void)inObjectID; (void)inNumberAddresses;
    (void)inAddresses; (void)inClientData;

    AudioObjectID newDevice = get_default_output_device();
    if (newDevice == kAudioObjectUnknown) {
        LOG("Default output device changed but couldn't identify new device");
        return noErr;
    }

    LOG("Default output device changed → %s (id=%u)",
        get_device_name(newDevice), (unsigned)newDevice);

    switch_units_to_device(newDevice);
    return noErr;
}

/* ------------------------------------------------------------------ */
/*  Interposed functions                                              */
/* ------------------------------------------------------------------ */

/*
 * We interpose AudioOutputUnitStart to track which AudioUnits are active
 * output units. This catches the exact moment Wine starts playing audio.
 */
extern OSStatus AudioOutputUnitStart(AudioUnit ci);

static OSStatus hook_AudioOutputUnitStart(AudioUnit ci)
{
    pthread_mutex_lock(&g_lock);

    /* Check if already tracked */
    int found = -1;
    for (int i = 0; i < g_num_units; i++) {
        if (g_units[i].unit == ci) { found = i; break; }
    }

    if (found >= 0) {
        g_units[found].running = 1;
    } else if (g_num_units < MAX_TRACKED_UNITS) {
        g_units[g_num_units].unit    = ci;
        g_units[g_num_units].running = 1;
        g_num_units++;
        LOG("Tracking output unit %p (total: %d)", ci, g_num_units);
    } else {
        LOG("WARNING: too many output units, cannot track %p", ci);
    }

    pthread_mutex_unlock(&g_lock);

    /* Call the real AudioOutputUnitStart */
    return AudioOutputUnitStart(ci);
}

/*
 * Interpose AudioOutputUnitStop to update running state.
 */
extern OSStatus AudioOutputUnitStop(AudioUnit ci);

static OSStatus hook_AudioOutputUnitStop(AudioUnit ci)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_num_units; i++) {
        if (g_units[i].unit == ci) {
            g_units[i].running = 0;
            VLOG("Unit %p stopped", ci);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);

    return AudioOutputUnitStop(ci);
}

/*
 * Interpose AudioComponentInstanceDispose to remove disposed units.
 */
extern OSStatus AudioComponentInstanceDispose(AudioUnit ci);

static OSStatus hook_AudioComponentInstanceDispose(AudioUnit ci)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_num_units; i++) {
        if (g_units[i].unit == ci) {
            g_units[i] = g_units[--g_num_units];
            LOG("Untracked unit %p (remaining: %d)", ci, g_num_units);
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);

    return AudioComponentInstanceDispose(ci);
}

/* ------------------------------------------------------------------ */
/*  DYLD interpose table                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const void *replacement;
    const void *replacee;
} interpose_tuple_t;

__attribute__((used, section("__DATA,__interpose")))
static const interpose_tuple_t interpose_table[] = {
    { (const void *)hook_AudioOutputUnitStart,
      (const void *)AudioOutputUnitStart },
    { (const void *)hook_AudioOutputUnitStop,
      (const void *)AudioOutputUnitStop },
    { (const void *)hook_AudioComponentInstanceDispose,
      (const void *)AudioComponentInstanceDispose },
};

/* ------------------------------------------------------------------ */
/*  Constructor: register device-change listener at load time         */
/* ------------------------------------------------------------------ */

__attribute__((constructor))
static void audio_hook_init(void)
{
    /* Check env for verbose logging */
    const char *env = getenv("AUDIO_HOOK_VERBOSE");
    if (env && *env && *env != '0')
        g_verbose = 1;

    AudioObjectID currentDevice = get_default_output_device();
    LOG("Initialized — current default output: %s (id=%u)",
        get_device_name(currentDevice), (unsigned)currentDevice);

    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope    = kAudioObjectPropertyScopeGlobal,
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 120000
        .mElement  = kAudioObjectPropertyElementMain
#else
        .mElement  = kAudioObjectPropertyElementMaster
#endif
    };

    OSStatus err = AudioObjectAddPropertyListener(
        kAudioObjectSystemObject, &addr,
        on_default_device_changed, NULL);

    if (err == noErr) {
        LOG("Listening for default output device changes ✓");
    } else {
        LOG("FAILED to register device change listener: %d", (int)err);
    }
}
