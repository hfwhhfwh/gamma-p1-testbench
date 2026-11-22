# baseline_win — TBENCH-WIN-1 Windows 渲染基线目录 (WP4a)

## 内容

- `mac_ref/render_dump.csv` — Mac 侧渲染基线（冻结，永不覆写）。
  由 `tests/win_render_dump.c` 在 macOS 上生成：默认 patch 复位态 +
  engine_mode=1 (FM) + fm.algorithm=5 + vca.level=0.8 + vcf.cutoff=0.66 +
  note_on 60/vel 100，150 块 + note_off 后 50 块，共 12800 样本，`%.9g`。
  生成后已用同种子双跑验证逐字节确定性。
- `render_dump_win.csv` — 搭档 Windows 侧产物（verify_win.ps1 自动生成）。
- `compare_report.txt` — 容差比对报告（阈值 1e-6，verify_win.ps1 生成）。

## 口径说明（W8）

13 件测试自身不落音频 CSV（音频等价性由各测试内嵌判据覆盖：
snapshot_kcapi_test 双实例渲染逐位一致、kcapi_readback_test WP3d
A==B cmp=0、p0_modsrc_test Goertzel 键跟踪/RMS 判定）。本目录的
render_dump.csv 是任务书 WP4a 要求的显式跨平台渲染基线，补上述
"同平台自比"之外的跨平台容差证据。

13 件唯一落盘的参数 CSV `tests/tbfix2_out/wp_readback_after.csv` 已在
Mac 回归中与冻结基线逐字节一致（见 TBENCH_WIN1_REPORT.md W3 节）；
Windows 侧由 kcapi_readback_test 重新生成，如需复核请与 Mac 副本容差比对。

## W9 日志归一化口径

Windows GUI session 日志与 Mac 样本比对时归一三列：
t_ms 数值列、`# session_start` 日期/编译戳、以及 op=signal 行的
target 平台字面（Mac `SIGINT|SIGTERM` / Windows `CTRL_C|CTRL_CLOSE`，
任务书 WP2c 规格）。归一后 diff 必须为空。可用 baseline_win 下
normalize_log.ps1：

    powershell -File normalize_log.ps1 <日志文件> > <归一后.txt>
