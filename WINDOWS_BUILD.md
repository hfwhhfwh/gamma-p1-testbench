# gamma-p1-testbench — Windows 构建指南

本项目与 macOS 共用源码和依赖；Windows 使用 imgui+GLFW+OpenGL 全面板 GUI、RtAudio(WASAPI) 音频、
RtMidi(Windows MM) MIDI 输入、13 件测试（含 `logpath1_test`）、操作
日志（含 `GP1_LOG_PATH`、`Ctrl+C` 优雅退出、启动 stderr 绝对落点行）。

第三方库全部用 `third_party/` 内与 Mac 锁版同版本的源码直编
（glfw 3.3.9 / rtaudio 6.0.1 / rtmidi 6.0.0 / imgui 1.91.0），
**不需要 vcpkg 也能构建**。`vcpkg.json` 仅在你选择显式接入 vcpkg 时
使用（见文末附录）。

内核 `gamma-p1/` 是只读依赖（与测试台目录平级），本构建一行不改。

---

## 0. 前置要求（一次性）

- **Visual Studio Build Tools 2022**（含 "C++ 生成工具" 工作负荷；
  社区版 Visual Studio 2022 亦可）：
  <https://visualstudio.microsoft.com/downloads/> →
  "Build Tools for Visual Studio 2022"。安装器里勾选
  **"使用 C++ 的桌面开发"**（MSVC v143、Windows 10/11 SDK、CMake 组件可选）。
- **CMake ≥ 3.16**：<https://cmake.org/download/> 装 "Windows x64 Installer"，
  安装时勾选 "Add CMake to the system PATH"。（VS 自带 CMake 也可，
  用 "x64 Native Tools Command Prompt" 里的 `cmake`。）
- **git**（克隆/拷贝代码用）。
- 声卡：任何 Windows 默认音频设备（WASAPI 共享模式即可，**无需 ASIO**）。

验证安装：打开 **"x64 Native Tools Command Prompt for VS 2022"**，执行

```
cmake --version
cl
```

两者都有输出即就绪。

---

## 1. 获取代码

把 `gamma-p1`（内核）与本目录放到同一父目录下，结构如下：

```
<父目录>\
  gamma-p1\              <- 内核 (只读, 不得修改)
  gamma-p1-testbench\<- 本目录
```

## 2. 配置与构建（一键）

在 "x64 Native Tools Command Prompt" 中：

```
cd gamma-p1-testbench
cmake -B build -A x64 -DGP1_TESTS=ON
cmake --build build --config Release
```

- `-A x64`：64 位唯一目标（任务书明确不做 32 位）。
- `-DGP1_TESTS=ON`：同时构建 13 件测试（默认 OFF 只出 GUI 程序）。
- 产物：`build\Release\testbench.exe` + 13 个测试 exe。

编译选项已钉死：`/fp:precise`（严禁 `/fp:fast`）、`/utf-8`、内核 TU
`/W3`、宿主与测试 `/W4`、静态运行库 `/MT`（免装 VC redist）、
控制台子系统（stderr 诊断行可见）。

## 3. 跑 13 件测试

```
cd build
ctest -C Release --output-on-failure
```

预期：13/13 通过。各测试的 stdout 会写进
`Testing\Temporary\LastTest.log`，判据明细（每项 PASS/FAIL）在其中。

也可以一键验证（构建 + 13 件 + 日志闭环检查 + 基线容差比对）：

```
powershell -ExecutionPolicy Bypass -File verify_win.ps1
```

（该脚本见第 6 节"回传清单"。）

## 4. 跑 GUI

```
cd build\Release
testbench.exe
```

- 窗口 1600x1200，全面板与 Mac 版一致（键盘、Seq、连线/调制、
  FM Operators、快照等）。
- 音频走 WASAPI；无 MIDI 设备时程序照常运行（MIDI 为空操作）。
- PC 键盘弹奏：焦点在主窗口直接按键。
- **退出**：直接按窗口关闭按钮，或控制台 `Ctrl+C`。两种都会走优雅
  退出路径，日志尾部落 `op=signal` 行 + `# session_end` 行。

### 日志闭环自证（W7）

```
cd build\Release
set GP1_LOG_PATH=%CD%\session_test.log
testbench.exe
```

启动时 stderr 立刻出现一行：

```
[alog] logging to C:\...\build\Release\session_test.log
```

在 GUI 里做几个操作（点几个参数、按几个琴键、开 Seq 几步），然后
回控制台按 `Ctrl+C`，检查日志尾部：

```
findstr "signal session_end" session_test.log
```

应看到一行 `... | signal     | CTRL_C|CTRL_CLOSE ...` 和
`# session_end lines=N`。

> 说明：`op=signal` 行的 target 平台不同字面不同——Mac 是
> `SIGINT|SIGTERM`，Windows 按任务书规格是 `CTRL_C|CTRL_CLOSE`
> （行结构与 detail/source 列完全一致）。与 Mac 样本做归一化 diff
> 时需把该行 target 列一并归一（verify_win.ps1 已处理）。

## 5. 截图清单（W6）

GUI 实跑时请截 ≥4 张（Win+Shift+S）：

1. 主面板（含示波器/引擎区）
2. 键盘面板（底部展开）
3. Seq（16 步音序器）面板
4. 连线/调制（cable/调制路由）面板

## 6. 回传清单（做完以上后打包发回）

1. 配置与构建日志：`build` 目录下执行时的完整控制台输出
   （`cmake -B ...` 与 `cmake --build ...` 各一份；重跑可加
   `--clean-first`，或直接整目录压缩 `build` 太大就只回传日志文本）
2. `ctest -C Release --output-on-failure` 完整输出
3. 4+ 张 GUI 截图
4. `session_test.log`（W7 日志闭环样本）+ 启动 stderr 截屏
5. 13 件测试运行时产生的 CSV（`tests\tbfix2_out\wp_readback_after.csv`
   等）——verify_win.ps1 会自动复制到 `baseline_win\` 并生成容差
   比对报告 `baseline_win\compare_report.txt`
6. 任何编译告警文本（目标：零告警；若有请原样贴出，报告逐条说明）

## 7. 常见坑

- **`cmake` 不是命令**：用 "x64 Native Tools Command Prompt"，或把
  CMake `bin` 目录加 PATH。
- **找不到编译器**：同上，必须在 VS 开发者命令行里跑 cmake。
- **GLFW 窗口创建失败**：远程桌面/无 GPU 环境下 OpenGL 3.2 core 可能
  失败；实体机 + 正常显卡驱动即可。
- **WASAPI 打开失败**：确认 Windows 默认播放设备可用；独占模式不是
  必需。

---

## 附录：vcpkg 方式（可选，非默认）

默认构建不依赖 vcpkg（第三方库全部 vendored 同版本源码）。如团队要求
走 vcpkg：

```
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
cmake -B build -A x64 -DGP1_TESTS=ON ^
  -DCMAKE_TOOLCHAIN_FILE=%CD%\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

注意：即使接入 vcpkg，CMake 目标仍编译 `third_party/` vendored 源码
（版本锁定纪律），vcpkg 包仅作环境声明/备选。`vcpkg.json` 声明的
glfw3/rtaudio/rtmidi 版本以 vcpkg 仓库当时版本为准，行为兼容性已按
API 面核对（RtAudio::WINDOWS_WASAPI / RtMidi::WINDOWS_MM / GLFW 3.3
API 面），若版本漂移导致告警/错误，把日志回传。
