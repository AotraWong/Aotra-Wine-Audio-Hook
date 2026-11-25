# Aotra-Wine-Audio-Hook

**English** | [中文](#中文)

A macOS audio device hot-swap hook for **Whisky / Wine** that fixes the issue where audio output fails to automatically switch when plugging or unplugging headphones (such as 3.5mm wired headphones or Bluetooth headsets) mid-game ([frankea/Whisky#405](https://github.com/frankea/Whisky/issues/405)).

## Included Files

- `libaudioswitchhook.dylib` — Pre-compiled Universal binary (supports `arm64` + `x86_64` / Rosetta 2)
- `audio_switch_hook.c` — Hook source code
- `build.sh` — Build script
- `install_hook.sh` — Installation script (copies the dylib to `~/.local/lib/libaudioswitchhook.dylib`)

## Usage

### 1. Install (or use the dylib directly from the current directory)

```bash
bash install_hook.sh
```

### 2. Configure Environment Variables in Whisky

Open **Whisky** → target **Bottle / Program Settings** → **Environment**, and add:

- **Key**: `DYLD_INSERT_LIBRARIES`
- **Value**: `/Users/<your-username>/.local/lib/libaudioswitchhook.dylib` (**Must use the full absolute path; `~` is not supported**)

Optional verbose logging variable:
- **Key**: `AUDIO_HOOK_VERBOSE`
- **Value**: `1`

---

## 中文

**Whisky / Wine 音频设备热切换补丁 (macOS)**

解决在 Whisky / Wine 中运行游戏时，中途插拔耳机（如 3.5mm 有线耳机、蓝牙耳机）后声音不会自动切换输出设备的问题（[frankea/Whisky#405](https://github.com/frankea/Whisky/issues/405)）。

### 包含文件

- `libaudioswitchhook.dylib` — 已编译的 Universal 二进制（支持 `arm64` + `x86_64` / Rosetta 2）
- `audio_switch_hook.c` — Hook 源码
- `build.sh` — 编译脚本
- `install_hook.sh` — 安装脚本（将 dylib 复制到 `~/.local/lib/libaudioswitchhook.dylib`）

### 使用方法

#### 1. 安装（或直接使用当前目录下的 dylib）

```bash
bash install_hook.sh
```

#### 2. 在 Whisky 中配置环境变量

打开 Whisky → 对应 Bottle / 程序设置 → **环境变量 (Environment)**，添加：

- **Key**: `DYLD_INSERT_LIBRARIES`
- **Value**: `/Users/<你的用户名>/.local/lib/libaudioswitchhook.dylib`（**必须填写完整绝对路径，不能使用 `~`**）

可选日志调试变量：
- **Key**: `AUDIO_HOOK_VERBOSE`
- **Value**: `1`