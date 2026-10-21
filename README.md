<div align="center">

# MOONDROP DAWN PRO 2 Native DSD ASIO Driver

**A lightweight, high-performance, open-source ASIO 2.3 driver enabling Bit-Perfect Native DSD256 on Windows.**

[![Build & Release](https://github.com/B5-Software/moondrop-dawn-pro2-asio/actions/workflows/build-release.yml/badge.svg)](https://github.com/B5-Software/moondrop-dawn-pro2-asio/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20(x64)-brightgreen.svg)]()
[![Author: B5-Software](https://img.shields.io/badge/Author-B5--Software-red.svg)](https://github.com/B5-Software)
[![ASIO Standard](https://img.shields.io/badge/Audio%20API-Steinberg%20ASIO%202.3-orange.svg)]()
[![Format: Native DSD256](https://img.shields.io/badge/DSD-Native%20DSD64%20%2F%20128%20%2F%20256-blueviolet.svg)]()

[English](#english) | [简体中文](#简体中文)

</div>

---

<a name="简体中文"></a>
## 简体中文

### 📖 项目简介

水月雨 破晓2（**MOONDROP DAWN PRO 2**，硬件标识 `USB\VID_35D8&PID_011D`）搭载普林芯驰 SPV4040 主控与双 Cirrus Logic CS43198 解码芯片。

在 Windows 10/11 系统下，微软内置的类驱动 `usbaudio2.sys` 仅支持标准 PCM 输出，无法激活 UAC2 协议中声明的 `RAW_DATA` 原始比特流接口，导致无法直接硬解 DSD256。

本项目是一套**完全开源、无需高危内核驱动、基于微软与开源生态标准接口打造的原生 Steinberg ASIO 2.3 驱动**。通过高速 `libusbK` KISO 管道直接对接破晓2的音频流接口（AltSetting 4），实现 **Bit-Perfect 原生 Native DSD256（11.2896 MHz）源码直解**，彻底告别 DSD 转 PCM 的有损妥协！

---

### ✨ 核心特性

- 🎯 **原生 Native DSD 直解**：支持 Native DSD64、DSD128、DSD256（11.2896 MHz）硬件直出，破晓2指示灯点亮 DSD 硬件解码状态。
- ⚡ **完整 PCM 高解析支持**：支持 44.1 kHz 至 384 kHz（16-bit / 24-bit / 32-bit）全规格 PCM。
- 🛡️ **纯用户态，零蓝屏风险**：底层基于成熟稳定的 `libusbK` 等时传输栈，完全运行于用户态，无需在系统安装未经签名的第三方高危内核 `.sys` 驱动。
- ⏱️ **闭环硬件时钟同步**：动态监听端点 `0x83` 的 125µs 微帧异步时钟反馈包，微秒级动态调步，杜绝缓冲区抖动与爆音。
- 🎧 **独立子槽立体声分离**：严格按照 UAC2 规范实现左右声道物理隔离的 32 位独立子槽打包，100% 纯净立体声分离度，零声道串扰。

---

### 🏛️ 驱动架构图

```
┌────────────────────────────────────────────────────────┐
│     音乐播放器 (Foobar2000 / AIMP / HQPlayer 等)        │
└───────────────────────────┬────────────────────────────┘
                            │ 标准 Steinberg ASIO 2.3 API
                            ▼
┌────────────────────────────────────────────────────────┐
│  dawn_pro2_asio.dll (本开源 64位 ASIO 用户态驱动引擎)    │
│  - 独立双声道 32-bit 子槽打包器                         │
│  - 125µs 微帧时钟反馈环 (0x83 Feedback Loop)            │
│  - 动态抖动吸收缓冲池 (Jitter Buffer)                   │
└───────────────────────────┬────────────────────────────┘
                            │ libusbK KISO Pipe API
                            ▼
┌────────────────────────────────────────────────────────┐
│  libusbK.dll / libusbK.sys (微软认证开源通用 USB 驱动)   │
└───────────────────────────┬────────────────────────────┘
                            │ USB 2.0 High-Speed 等时流 (Isochronous)
                            ▼
┌────────────────────────────────────────────────────────┐
│  MOONDROP DAWN PRO 2 硬件 (SPV4040 + 双 CS43198 DAC)   │
│  - Endpoint 0x07 (OUT): Raw DSD256 源码比特流          │
│  - Endpoint 0x83 (IN) : 异步时钟速率动态反馈            │
└────────────────────────────────────────────────────────┘
```

---

### 🚀 快速安装与配置指南

#### 第一步：将音频流接口绑定为 libusbK
破晓2出厂为复合 USB 设备（Interface 0 为 HID 音量键，Interface 1 为音频接口）。我们需要将音频接口交由 `libusbK` 接管：

1. 下载并运行常见的开源工具 **[Zadig](https://zadig.akeo.ie/)**（或使用工程 `tools/zadig-2.9.exe`）。
2. 在 Zadig 顶部菜单勾选：**Options -> List All Devices**。
3. 在下拉列表中选中：**`DAWN PRO2 (Interface 1)`**（对应硬件 ID `USB\VID_35D8&PID_011D&MI_01`）。
   > ⚠️ **注意**：千万**不要**选择 `Interface 0`，Interface 0 是物理音量滚轮与按键接口。
4. 目标驱动选择：**`libusbK`**（v3.1.0.0 或更新版）。
5. 点击 **Replace Driver**，等待提示安装成功。

#### 第二步：注册 ASIO 驱动
1. 下载本项目的最新 [Release 压缩包](https://github.com/B5-Software/moondrop-dawn-pro2-asio/releases)。
2. 解压到任意固定目录（例如 `D:\AudioDrivers\DawnPro2\`，注意解压后请勿随意移动或删除）。
3. 鼠标右键点击 **`install_asio.bat`**，选择 **以管理员身份运行**。
4. 提示注册成功后，系统中即会出现名为 **`MOONDROP DAWN PRO 2 ASIO`** 的标准音频驱动。

#### 第三步：在 Foobar2000 中启用 Native DSD

1. 确保 Foobar2000 已安装 **`foo_out_asio+dsd`**（或 `foo_out_asio`）与 **`foo_input_sacd`** 插件。
2. 打开 Foobar2000 的 **Preferences (Ctrl+P)**：
   - 导航至 **Playback -> Output**：
     - Device 下拉菜单选择：**`ASIO : MOONDROP DAWN PRO 2 ASIO`**。
   - 导航至 **Tools -> SACD**：
     - Output Mode 选择：**`DSD`**（或 `DSD+PCM`）。
     - DSD Processor：设为 `None`。
3. 播放任意 **DSD256**（`.dsf` / `.dff` / 11.2 MHz）音频文件：
   - 破晓2 指示灯将变为 DSD 硬件直解状态，左右声道完全分离，享受原生纯净声音！

---

### 🔄 恢复日常 Windows 声卡模式（随时可逆）

如果想让破晓2变回 Windows 默认的免驱系统普通声卡（用于看网页视频、日常游戏）：
1. 鼠标右键点击 Windows 开始按钮 -> **设备管理器**。
2. 展开 `libusbK USB Devices`，找到 `DAWN PRO2 (Interface 1)`。
3. 右键选择 **更新驱动程序 -> 浏览我的电脑以查找驱动程序 -> 让我从计算机上的可用驱动程序列表中选取**。
4. 选中 **USB 音频设备 2.0 (USB Audio 2.0)**，点击下一步即可无损秒切回普通声卡。

---

<a name="english"></a>
## English

### 📖 Introduction

The **MOONDROP DAWN PRO 2** (Hardware ID: `USB\VID_35D8&PID_011D`) is equipped with a SpaceTouch SPV4040 MCU and dual Cirrus Logic CS43198 DACs.

Under Windows 10/11, the native `usbaudio2.sys` driver only negotiates standard PCM alternate settings, bypassing the UAC2 `RAW_DATA` (format `0x80000000`) descriptor required for native DSD bitstream transmission.

This project delivers a **clean-room, fully open-source Steinberg ASIO 2.3 driver** built specifically for Windows. Leveraging the high-speed `libusbK` KISO isochronous pipeline, it streams **Bit-Perfect Native DSD256 (11.2896 MHz)** directly to AltSetting 4 on the DAC hardware without PCM transcoding or DoP encapsulation overhead.

### ✨ Highlights

- **Native DSD Direct Streaming**: Native DSD64, DSD128, and DSD256 (11.2896 MHz) hardware direct stream with DAC indicator confirmation.
- **Full PCM Hi-Res Support**: PCM 44.1 kHz to 384 kHz (16/24/32-bit).
- **Zero Kernel Risk**: Operates 100% in user-space over WHQL-compatible `libusbK` architecture. Zero BSOD risks, no unsigned `.sys` files.
- **Hardware Feedback Synchronization**: Closes the feedback loop by polling endpoint `0x83` every microframe for dynamic timing adjustments.
- **Isolated Subslot Framing**: Discrete 32-bit channel-independent subslot serialization ensuring 100% stereo isolation.

---

### 🛠️ Building from Source

Prerequisites:
- [MinGW-w64](https://www.mingw-w64.org/) GCC 8.1.0 or newer.

Simply run the build script in the repository root:
```cmd
build_asio.bat
```
Or compile manually via:
```bash
g++ -O2 -shared -static -Wall -o dawn_pro2_asio.dll dawn_asio.cpp dawn_asio.def -I. -lwinmm -ladvapi32 -luser32 -lole32 -luuid
strip --strip-unneeded dawn_pro2_asio.dll
```

Automated cross-compilation and packaging are also continuously handled by the [GitHub Actions CI Workflow](.github/workflows/build-release.yml).

---

### 👤 Author / 作者

- **B5-Software** (GitHub: [@B5-Software](https://github.com/B5-Software))

---

### ⚖️ Disclaimer & License

- **License**: Released under the [MIT License](LICENSE).
- **Disclaimer**: This is an independent open-source project created by audio enthusiasts for the community. MOONDROP is a registered trademark of Chengdu Shuiyueyu Technology Co., Ltd. This project is not affiliated with, endorsed by, or associated with MOONDROP or Cirrus Logic.
