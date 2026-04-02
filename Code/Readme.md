# 网好卡菌 (NetGoodLagBacteria) - 开发者指南

本文档面向希望研究、修改或二次开发“网好卡菌”的开发者。本项目是一个基于 C++ 和 Win32 API 编写的屏幕特效与系统干扰模拟器，代码结构清晰，易于扩展。

## 🛠️ 技术栈与编译环境

- **语言**：C++14
- **编译器**：MinGW-w64 (x86_64-w64-mingw32-g++)
- **核心 API**：Win32 API, GDI (Graphics Device Interface)
- **UI 框架**：纯 Win32 原生控件 + 自绘 (Custom Draw) + Windows 10/11 Acrylic 模糊特效
- **依赖**：全静态链接，无外部 DLL 依赖（除系统自带库外）

### 编译命令
在 Linux/WSL 环境下使用 MinGW 交叉编译的完整命令如下：

```bash
# 1. 编译资源文件（包含图标和 Manifest）
x86_64-w64-mingw32-windres resource.rc -O coff -o resource.res

# 2. 编译主程序
x86_64-w64-mingw32-g++ -DCLEAN -DUNICODE -D_UNICODE \
  -std=c++14 -O2 -mwindows \
  -static-libgcc -static-libstdc++ -static \
  main.cpp payloads.cpp utils.cpp data.cpp resource.res \
  -o NetGoodLagBacteria_Clean_User_v12.exe \
  -lgdi32 -lmsimg32 -luser32 -lkernel32 -lwinmm -lole32 -lshlwapi \
  -lpsapi -lcomctl32 -lcomdlg32 -ldwmapi -luxtheme \
  -lshell32 -ladvapi32 -luuid -loleaut32
```

> **注意**：`-DCLEAN` 宏非常重要。定义此宏将编译出带有控制面板的“安全版”（即当前版本）；如果不定义，将编译出无 UI、直接执行破坏性载荷的“恶意版”（类似于原始 MEMZ 病毒）。

---

## 📂 项目结构

- `main.cpp`：程序入口 (`WinMain`)，负责创建控制面板窗口、处理 UI 消息循环、绘制毛玻璃界面、管理全局快捷键（Shift+O / Shift+Del）。
- `payloads.cpp` / `payloads.h`：核心载荷实现。包含所有 18 种视觉特效和系统干扰的具体逻辑。
- `utils.cpp`：工具函数，如随机数生成、字符串处理等。
- `data.cpp` / `data.h`：存储硬编码的数据（如内置的错误图标、音频数据等）。
- `memz.h`：全局头文件，包含所有必要的系统头文件和全局变量声明。
- `resource.rc` / `app.manifest`：资源文件，用于嵌入应用图标（节奏医生 Logo）和声明 DPI 感知。

---

## 🧠 核心架构解析

### 1. 渐进式强度系统 (Intensity Ramp)
为了实现《节奏医生》中循序渐进的干扰效果，程序在 `payloads.cpp` 中实现了一个基于 Sigmoid 曲线的全局强度函数 `gIntensity()`。
- 启动前 30 秒：强度极低（0.0 - 0.08）。
- 30 秒至 2 分钟：强度明显上升（0.08 - 0.55）。
- 2 分钟至 5 分钟：达到高潮（0.55 - 0.88）。
- 5 分钟后：无限趋近于 1.0（满负荷）。

每个 Payload 在执行时都会调用 `payloadStrength(times, runtime)`，将全局强度与该 Payload 的独立运行时间结合，动态计算当前的延迟（Delay）或绘制数量。

### 2. 弹窗系统与防抢焦点机制
为了不影响用户的正常操作（如打字、玩游戏），所有的 `MessageBoxW` 弹窗都通过 `WH_CBT` (Computer-Based Training) Hook 进行了拦截。
- 在 `msgBoxHook` 中拦截 `HCBT_ACTIVATE` 消息。
- 使用 `SetWindowPos` 将弹窗设置为 `HWND_TOPMOST`，并附加 `SWP_NOACTIVATE` 标志，确保弹窗出现时**绝对不会抢夺当前窗口的焦点**。
- 弹窗创建后，会为其分配一个独立的 `roamerThread`，使其在屏幕上随机游走。

### 3. UI 渲染与 Acrylic 模糊
控制面板的 UI 放弃了传统的灰色窗口，采用了现代的 Glassmorphism（拟态玻璃）风格。
- **毛玻璃背景**：通过调用未公开的 Windows API `SetWindowCompositionAttribute` 并传入 `ACCENT_ENABLE_ACRYLICBLURBEHIND` 实现全窗口的亚克力模糊。
- **双缓冲绘图**：在 `main.cpp` 的 `DoPaint` 函数中，使用内存 DC (`CreateCompatibleDC`) 进行双缓冲绘制，彻底消除按钮状态切换时的闪烁问题。
- **暗色标题栏**：通过 `DwmSetWindowAttribute` 强制开启暗色模式，并将标题栏颜色设置为深藏青色（`#0A0C1E`）。

### 4. 竞态条件与一键关闭 (Shift+O)
当用户按下 `Shift+O` 时，程序需要立即关闭所有正在游走的弹窗。
- 早期版本使用全局数组注册 HWND，但存在线程创建与 Hook 触发之间的竞态窗口。
- 当前版本在 `closeAllPopups()` 中使用 `EnumWindows` 遍历当前进程的所有顶层窗口，匹配类名为 `#32770`（Dialog）的窗口，并直接发送 `WM_COMMAND IDCANCEL` 和 `WM_CLOSE`，实现了 100% 可靠的瞬间清屏。

---

## 🚀 如何添加新的 Payload

如果你想添加一个新的视觉特效，只需遵循以下步骤：

1. 在 `payloads.cpp` 中编写你的 Payload 函数，签名必须为 `int payloadYourName(PAYLOADFUNC)`。
2. 在函数开头必须包含宏 `PAYLOADHEAD`（用于处理暂停逻辑）。
3. 函数的返回值应为下一次执行该 Payload 的延迟时间（毫秒）。
4. 在 `memz.h` 中声明你的函数。
5. 在 `payloads.cpp` 顶部的 `payloads[]` 数组中注册你的 Payload，提供函数指针、UI 显示名称和默认开启状态。

```cpp
// 示例：一个简单的屏幕反色 Payload
int payloadMyInvert(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        HDC hdc = getTargetDC();
        int w = getTargetW(), h = getTargetH();
        // 使用 PATINVERT 进行反色
        PatBlt(hdc, 0, 0, w, h, PATINVERT);
        releaseTargetDC(hdc);
    }
    // 根据全局强度计算下一次执行的延迟
    out: return rampDelay(5000, 100);
}
```

---
*Happy Coding!*

---

## ⚙️ GitHub Actions 自动构建 (CI/CD)

项目根目录下的 `.github/workflows/build.yml` 配置了完整的自动构建流水线，无需在本地安装任何工具链即可通过 GitHub 云端完成编译。

### 触发条件

| 触发事件 | 行为 |
| :--- | :--- |
| 向 `main` / `master` 分支推送代码 | 自动编译，产物作为 Artifact 保留 30 天 |
| 创建 Pull Request | 自动编译，验证代码可正常构建 |
| 推送 `v*` 格式的 Tag（如 `v12.1`） | 编译 + 自动创建 GitHub Release，附带 .exe 和源码包 |
| 手动触发（workflow_dispatch） | 在 Actions 页面点击 "Run workflow" 即可 |

### 发布新版本的完整流程

```bash
# 1. 提交所有改动
git add .
git commit -m "feat: 新增某某特效"
git push origin main

# 2. 打 Tag 触发自动发布
git tag v12.1
git push origin v12.1
# → Actions 自动编译并在 Releases 页面发布带附件的正式版本
```

### 流水线各阶段说明

`build` job 在 `ubuntu-latest` 上运行，依次执行以下步骤：安装 MinGW-w64 交叉编译工具链、用 Python Pillow 从 `rd_logo.png` 生成多尺寸 `app.ico`、编译资源文件（`windres`）、静态链接编译主程序，最后将 `.exe` 作为 Artifact 上传。

`release` job 仅在推送 Tag 时运行，它会下载 `build` job 的产物，打包源码为 `.zip`，然后调用 `softprops/action-gh-release` 自动创建 Release 页面并上传两个附件。

> **权限说明**：`release` job 需要 `contents: write` 权限才能创建 Release。如果你的仓库是 Fork 或组织仓库，请在 Settings → Actions → General → Workflow permissions 中确认已开启 "Read and write permissions"。
