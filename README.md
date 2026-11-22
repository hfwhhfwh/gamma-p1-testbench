# gamma-p1-testbench

Windows/macOS 实时测试台 UI，用于驱动 `gamma-p1` 已验收合成器内核（减法 / FM / 深度合成 / 算子级融合 / 调制矩阵）。

Windows 的安装步骤、VS 环境设置和常见问题另见 [Windows 构建指南](./WINDOWS_BUILD.md)。

## 构建

### Windows 前置条件

- Windows 10/11 x64
- Visual Studio 2022 或 Build Tools（含 MSVC C++ 工具链）
- CMake 3.16+

在 VS Developer PowerShell / x64 Native Tools Command Prompt 中运行：

```powershell
cmake -B build -A x64 -DGP1_TESTS=ON
cmake --build build --config Release
pushd build
ctest -C Release --output-on-failure
popd
.\build\Release\testbench.exe
```

也可使用 `.\verify_win.ps1` 一键构建、运行测试并生成跨平台渲染比对报告。Windows 后端使用 WASAPI 和 Windows Multimedia MIDI。

### macOS 前置条件

- macOS 12+（Apple Silicon 或 Intel）
- Xcode Command Line Tools（`clang`, `clang++`, `make`）
- 可选：CMake（推荐）；无 CMake 时使用 `build.sh`

### macOS 一键构建（CMake，推荐）
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGP1_TESTS=ON
cmake --build build -j
(cd build && ctest --output-on-failure)
./build/testbench
```

### macOS 一键构建（无 CMake 回退）
```bash
./build.sh
./build/testbench
```

`build.sh` 是仅支持 macOS 的 CMake 回退脚本；Windows 请使用上面的 CMake/MSVC 命令。

### ASAN 构建（macOS / Clang）
```bash
cmake -S . -B build-asan -DUSE_ASAN=ON -DGP1_TESTS=ON
cmake --build build-asan -j
(cd build-asan && ctest --output-on-failure)
```

## 依赖清单（全部 vendored，无包管理器）

| 库 | 版本 | 用途 | 位置 |
|---|---|---|---|
| RtAudio | 6.0.1 | macOS CoreAudio / Windows WASAPI 音频 | `third_party/rtaudio-6.0.1` |
| RtMidi | 6.0.0 | macOS CoreMIDI / Windows Multimedia MIDI | `third_party/rtmidi-6.0.0` |
| Dear ImGui | 1.91.0 | 界面 | `third_party/imgui-1.91.0` |
| GLFW | 3.3.9 | OpenGL 窗口/上下文 | `third_party/glfw-3.3.9` |

内核源码引用自同级目录 `../gamma-p1`，未做任何修改。可通过 CMake 的 `GP1_ROOT` 指向其他内核目录。
`GP1_TESTS=ON` 会构建并注册 13 个测试；Windows 的 `verify_win.ps1` 还会生成渲染基线比较报告。

## Windows 与 macOS 差异

| 项目 | Windows | macOS |
|---|---|---|
| 音频 / MIDI | RtAudio WASAPI / RtMidi Windows MM | RtAudio CoreAudio / RtMidi CoreMIDI |
| 平台实现 | `src/platform_win32.c`：Win32 计时、路径、临时目录和控制台接口 | `src/platform_posix.c`：POSIX 对应实现 |
| 主构建工具 | Visual Studio 2022 + CMake；`verify_win.ps1` 提供构建、测试和基线比对流程 | Xcode Command Line Tools + CMake；`build.sh` 是仅限 macOS 的备用构建脚本 |
| 编译器选项 | MSVC `/fp:precise`、`/utf-8`、静态运行库 `/MT`；内核 `/W3`、宿主 `/W4` | Clang `-O2 -Wall -Wextra`、C11/C++17；测试目标启用 `-Werror` |
| 测试 | 统一 CMake 配置下 13 个 CTest；可额外运行 Windows 渲染基线工具 | 统一 CMake 配置下同一组 13 个 CTest；也保留 `tests/build_tbfix3_tests.sh` 手工脚本 |

原 Mac 副本的手工测试脚本曾列 12 项；统一项目采用 Win 版新增的 `logpath1_test`，两平台现由同一 CMake 测试清单构建 13 项。

## 面板说明

窗口默认 1600×1200，分为四列 + 底部通栏：

- **左列**：Engine（引擎模式/主电平/Panic）、VCF、VCA、Tools（Preset/动作录制/离线渲染）、Scope（示波器）
- **中列**：VCO 0–2、ENV 0–2、LFO 0–2
- **右列**：FM 全局参数、FM Operators（6 算子表格）、Modulation Matrix（控制线/音频线编辑器）
- **底部通栏**：Keyboard（钢琴键盘，固定置顶于其他浮动窗口之上；`Hide`/`Show` 折叠为 36px 控制行）
- **最底部**：状态栏（回调负载 %、引擎模式、活跃音频线数/预算、输出峰值、最近触发音符）

布局纪律（v2）：
- 底部保留区 = 状态栏 28px + 键盘条（展开 210px / 收起 36px），所有面板每帧被钳制在保留区之上
  （窗口缩小时自动上移，不会被键盘条盖住；Keyboard/StatusBar 自身豁免）
- ImGui ini 使用布局版本化文件名 `imgui_layout_v2.ini`，旧 `imgui.ini` 自然失效
- Tools 面板 `Reset Layout` 按钮一键恢复全部面板默认坐标（默认坐标表在 `ui_panels.cpp`
  顶部 `kPanelDefaults[]`，与 FirstUseEver 初始布局共用同一数据源）

### 1) Engine
- `engine_mode`：SUB_ONLY / FM_ONLY / SERIES / PARALLEL
- `master_level`：总输出电平（UI 侧）
- `soft_clip_master`：UI 侧软饱和开关
- `Retrig LFOs on note`（P3 新增，默认关）：开启时每次 note_on 把 3 个 LFO 相位归零
- PARALLEL 模式下显示 `sub_gain` / `fm_gain`
- `PANIC`：all-notes-off

### 2) VCO ×3
- waveform（17 种含 4 噪声）、frequency、fine_tune(cents)、pulse_width、level
- `vco_mix`：混合增益（写入 `voice.vco_mix0/1/2`）
- sync_enabled、morph_mode

### 3) VCF
- mode：LADDER / SVF
- svf_type：LP / BP / HP / NOTCH
- cutoff、resonance、drive(dB)、soft_clip、drive_comp、smoothing、oversampling

### 4) VCA
- level、drive、curve（LINEAR/EXP）、smooth_ms

### 5) ENV ×3
- mode（ADHSR / BREAKPOINT / BEZIER）
- 顶部包络形状实时图（P3 新增）：ADHSR 段图（predelay→attack→hold→decay→sustain→release，
  段宽按 log10(1+t·1000) 压缩分配）/ BREAKPOINT 折线 / BEZIER 平滑曲线，
  曲线公式与内核 env.c 烘焙一致；图上亮点实时显示该 ENV 当前输出
- predelay、attack、hold、decay、sustain、release
- attack_curve / decay_curve
- `Retrigger` 按钮

### 6) LFO ×3
- waveform、frequency、depth、offset、fade_time
- sync_mode（FREE_RUN / KEY_SYNC）
- `set_phase 0`、`fade_release` 按钮

### 7) FM
- algorithm（1–32）、feedback op0/op1、porta_on/rate、transpose、pitch_bend_range
- PEG：peg_ar / peg_d1r / peg_d1l / peg_d2r / peg_rr
- FM-LFO：lfo_wave / lfo_speed / lfo_delay / lfo_pmd / lfo_amd / lfo_sync

### 8) FM Operators（6 行）
- waveform、coarse、fine、detune、output_level、tone_cutoff、vel_sens、krs、pms
- 【TBENCH-FIX-1】算子包络控件组（表格下方）：算子选择（1~6）+
  eg_mode 切换（EXT legacy 五参 / DX7 4R4L）+ 当前算子 EG 控件
  （eg_mode=DX7 时 R1~R4/L1~L4 八件；EXT 时 AR/D1R/D1L/D2R/RR 五件，
  随 eg_mode 切换显示）。写入路径已修复（此前算子 EG 参数在
  kernel_capi 被静默丢弃）；任何写不进去的参数名会在 stderr 留下
  `[kcapi] 未知参数名: ...` 日志且 `kcapi_set_param` 返回 -1

### 9) Modulation Matrix
- 端口下拉列表动态枚举自 vmatrix 端口描述表，不硬编码
- 控制线：源 + 目标 + amount（-1..1），支持增删改
- 音频线：同上；列表显示 ACTIVE / DORMANT；最多 8 条活跃，第 9 条进入候场，断开后自动补位
- 全部走 `synth_cable_op` 入口
- 默认 patch（P3 新增）：`ui_init` 自动建立 `env0.value → vca.level`（amount=1.0，
  命中 DST_MOD 口，增益 = clamp(vca.level + env0.value, 0, 1)），并把 `vca.level` 置 0
  ——开机静音，点键由 env0 包络驱动发声；该线在面板中可见、可删（删除后恢复常鸣）

### 10) Tools
- Preset：保存/加载二进制 `.snp`，另存文本 dump
- Action Log：Record / Stop / Replay / Clear / Save / Load
- Offline Render：输入秒数 → 导出 32-bit float WAV

### 11) 演奏
- RtMidi：自动打开第一个 MIDI 输入端口；支持 note on/off、pitch bend、CC1(modwheel)、CC11(breath)、CC2/4/74(aftertouch)、通道触后 0xD0（P0-MODSRC 新增，真实键盘 aftertouch 主通道）
- 电脑键盘（tracker 布局，Keyboard 面板 "PC Keys" 开关全局使能）：
  `Z S X D C V G B H N J M` = C~B（含黑键），`Q 2 W 3 E R 5 T 6 Y 7 U` = 高八度（数字排为黑键）；
  基准音跟随 Keyboard 面板 Octave（Z = 面板中八度 C）；按下沿触发、不重复；
  输入框聚焦（io.WantTextInput）时自动失效

### 12) Keyboard（底部通栏钢琴键盘）
- 默认 C2~B4 三个八度（21 白键 + 15 黑键），ImDrawList 自绘；Octave `-`/`+` 以八度平移（C-1 起步，受 MIDI 0~127 限制）
- 鼠标按下 = note_on，松开 = note_off；velocity 由点击纵坐标线性插值（键上沿 40 → 下沿 127）
- 按住拖动 = glissando：进入新键触发新 note_on，旧键 note_off
- **Hold** 推子开关：点击锁定发声，再点一次释放；多键锁定时由 UI 侧音符栈做回退
  （单声部内核语义：松开发声键回退到最近一个仍按住的键，不修改内核）
- 发声键高亮（含外部 MIDI / 电脑键盘触发，兼作输入监视器）；
  白键按下变深、黑键按下变亮；每组 C 标八度号
- 状态栏显示最近触发：`Last: A4 (69) vel=96`

## 已知限制
- `build.sh` 无编译器自动头文件依赖：改动 `src/*.h` 后按头文件时间戳强制重编全部 app 对象
  （防止 AppState 布局 ABI 漂移导致静默错误）。
- ImGui 使用 master 分支（1.91.0），无 docking 分支；窗口采用固定初始布局，可手动拖动调整。
- `vco_mix` 在 UI 上按 `voice.vco_mix0/1/2` 写入，与 PARAMETERS.txt 中的 `voice.vco_mix[i]` 对应。
- 离线渲染通过快照复制到临时 Synth 实例完成，不影响实时流。
- 动作录制回放基于 UI 层日志（`ui_set_param` / cable 函数），与内核 `synth_set_param` / `synth_cable_op` 挂点一一对应。

## 交付物
- 全部源码：`src/`、`CMakeLists.txt`、`build.sh`
- 依赖库：`third_party/`
- 界面截图：`screenshot_annotated.png`
