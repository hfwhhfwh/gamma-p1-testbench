#!/bin/bash
# TBENCH-FIX-3 测试构建脚本 (手工测试件, 不进构建系统, 同 log_wire 惯例)
# 用法: cd gamma-p1-testbench/tests && ./build_tbfix3_tests.sh
# 产物: tbfix3_out/*.o + 13 个测试二进制 + tbfix3_out/build_log.txt
#
# 变更摘要 (2026-10-26 SNAPSHOT-FIX-1, 测试台侧 WP3):
#   S6: KERNEL_SRCS 追加 $GP/snapshot_v1.c, CORE_OBJS 追加
#   tbfix3_out/snapshot_v1.o (照 basename 惯例)。
#   自证件新增两目标 (照既有段式, 链接 CORE_OBJS[+synth_ui.o]):
#     8) snapshot_envread_test —— S7 九读断口动态自证 (env0/1/2 ×
#        sustain_point/loop_start/loop_end 写后读回全等);
#     9) snapshot_ui_test —— S4/S5 UI 路径 headless 自证 (V5 往返 /
#        V1/V3 夹具加载 / 垃圾 5732B 失败不污染)。
# 变更摘要 (2026-10-27 DEFENSE-1, 测试台侧 WP3):
#   新增第 11 段: 重建 ../src/ui_panels.o (带 ImGui/GLFW 头, 照 build.sh
#   同口径加 -DGL_SILENCE_DEPRECATION -Wno-nontrivial-memcall) 并链接
#   defense_p51_test —— P-51 显示越界双保险显示层探针, 直链
#   ui_panels.o 的两个界检查辅助 (defense1_engine_mode_name /
#   defense1_algo_slider_prep), 不实例化 ImGui。
#   链接取 ../build/obj 的 app/第三方对象 (glfw_*/imgui*/RtAudio/RtMidi/
#   audio/midi/keyboard/panel_keyboard; 排除 main.o 与 synth_ui.o/
#   ui_panels.o —— 前者与本件 main 冲突, 后两者用本脚本自编译版),
#   需先跑过 ./build.sh (守卫检查)。
# 变更摘要 (2026-10-27 DEFENSE-1 WP4, 测试台侧):
#   新增第 12 段: defense1_kcapi_test —— T-i kcapi 路径 (恶意 V4 blob
#   拒载 rc==-1 + 线表零污染 + 孪生渲染逐位一致) + T-d engine_mode
#   回归 + 合法 V5/V1 加载回归。总数 11 -> 12。
set -e
cd "$(dirname "$0")"
OUT=tbfix3_out
mkdir -p "$OUT"
LOG="$OUT/build_log.txt"
: > "$LOG"

GP=../../gamma-p1
INC="-I../src -I$GP -I$GP/vco_c -I$GP/vcf_a -I$GP/vca -I$GP/lfo -I$GP/env -I$GP/fm"
CFLAGS="-O2 -Wall -Wextra -Werror -std=c11 -pedantic -Wno-unused-parameter"
CXXFLAGS="-O2 -Wall -Wextra -Werror -std=c++17 -Wno-unused-parameter"

KERNEL_SRCS=(
    $GP/synth.c $GP/vmatrix.c $GP/voice.c
    $GP/vco_c/vco.c $GP/vco_c/wavetables_data.c
    $GP/vcf_a/vcf.c $GP/vca/vca.c
    $GP/lfo/lfo.c $GP/lfo/lfo_wavetables_data.c
    $GP/env/env.c $GP/fm/fm.c
    $GP/snapshot_v1.c   # SNAPSHOT-FIX-1: S6 snapshot_v1 实现 TU
)

echo "== TBENCH-FIX-3 测试构建日志 $(date) ==" | tee -a "$LOG"

# ---- 内核 + kcapi + alog + platform + synth_ui (C 件 / C++ 件分开) ----
# TBENCH-WIN-1: ../src/platform_posix.c 并入 C 件循环 (action_log 的
# POSIX 调用点封装; POSIX 实现即原 API 直转调, Mac 行为不变)
for f in "${KERNEL_SRCS[@]}" ../src/kernel_capi.c ../src/action_log.c \
         ../src/platform_posix.c; do
    o="$OUT/$(basename "${f}" .c).o"
    echo "\$ cc $CFLAGS -c $f -o $o" >> "$LOG"
    cc $CFLAGS $INC -c "$f" -o "$o" 2>> "$LOG"
done
echo "\$ c++ synth_ui.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c ../src/synth_ui.cpp -o "$OUT/synth_ui.o" 2>> "$LOG"

CORE_OBJS="$OUT/synth.o $OUT/vmatrix.o $OUT/voice.o $OUT/vco.o \
$OUT/wavetables_data.o $OUT/vcf.o $OUT/vca.o $OUT/lfo.o \
$OUT/lfo_wavetables_data.o $OUT/env.o $OUT/fm.o \
$OUT/snapshot_v1.o $OUT/kernel_capi.o $OUT/action_log.o \
$OUT/platform_posix.o"   # TBENCH-WIN-1

# ---- 1) kcapi_readback_test (T-A/T-B/T-C + 既有回归) ----
echo "\$ cc kcapi_readback_test.c" >> "$LOG"
cc $CFLAGS $INC -c kcapi_readback_test.c -o "$OUT/kcapi_readback_test.o" 2>> "$LOG"
c++ -o kcapi_readback_test "$OUT/kcapi_readback_test.o" $CORE_OBJS -lm 2>> "$LOG"

# ---- 2) log_wire_test (P-16 重跑验收 T-F) ----
echo "\$ c++ log_wire_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c log_wire_test.cpp -o "$OUT/log_wire_test.o" 2>> "$LOG"
c++ -o log_wire_test "$OUT/log_wire_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 3) midi_route_test (既有回归, 重链新 kcapi) ----
echo "\$ c++ midi_route_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c midi_route_test.cpp -o "$OUT/midi_route_test.o" 2>> "$LOG"
c++ -o midi_route_test "$OUT/midi_route_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 4) keyboard_stuck_test (T-D; keyboard.cpp 走 kbd_stubs 无头桩) ----
echo "\$ c++ keyboard.cpp (kbd_stubs)" >> "$LOG"
c++ $CXXFLAGS -Ikbd_stubs $INC -c ../src/keyboard.cpp -o "$OUT/keyboard_stubbed.o" 2>> "$LOG"
echo "\$ c++ keyboard_stuck_test.cpp" >> "$LOG"
c++ $CXXFLAGS -Ikbd_stubs $INC -c keyboard_stuck_test.cpp -o "$OUT/keyboard_stuck_test.o" 2>> "$LOG"
c++ -o keyboard_stuck_test "$OUT/keyboard_stuck_test.o" "$OUT/keyboard_stubbed.o" \
    $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 5) panel_kb_fix_test (T-E) ----
echo "\$ c++ panel_kb_fix_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c panel_kb_fix_test.cpp -o "$OUT/panel_kb_fix_test.o" 2>> "$LOG"
c++ -o panel_kb_fix_test "$OUT/panel_kb_fix_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 6) p0_modsrc_test / p3_headless_test (既有回归, 重链新 kcapi) ----
echo "\$ c++ p0_modsrc_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c p0_modsrc_test.cpp -o "$OUT/p0_modsrc_test.o" 2>> "$LOG"
c++ -o p0_modsrc_test "$OUT/p0_modsrc_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"
echo "\$ c++ p3_headless_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c p3_headless_test.cpp -o "$OUT/p3_headless_test.o" 2>> "$LOG"
c++ -o p3_headless_test "$OUT/p3_headless_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 8) snapshot_envread_test (SNAPSHOT-FIX-1: S7 九读断口动态自证) ----
echo "\$ cc snapshot_envread_test.c" >> "$LOG"
cc $CFLAGS $INC -c snapshot_envread_test.c -o "$OUT/snapshot_envread_test.o" 2>> "$LOG"
c++ -o snapshot_envread_test "$OUT/snapshot_envread_test.o" $CORE_OBJS -lm 2>> "$LOG"

# ---- 9) snapshot_ui_test (SNAPSHOT-FIX-1: S4/S5 UI 路径 headless 自证) ----
echo "\$ c++ snapshot_ui_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c snapshot_ui_test.cpp -o "$OUT/snapshot_ui_test.o" 2>> "$LOG"
c++ -o snapshot_ui_test "$OUT/snapshot_ui_test.o" $CORE_OBJS "$OUT/synth_ui.o" -lm 2>> "$LOG"

# ---- 10) snapshot_kcapi_test (SNAPSHOT-FIX-1 WP4: T-d kcapi 路径 +
#      kcapi_snapshot_save_v5/load_legacy V5 往返 + V1/V2/V3 夹具回归) ----
echo "\$ cc snapshot_kcapi_test.c" >> "$LOG"
cc $CFLAGS $INC -c snapshot_kcapi_test.c -o "$OUT/snapshot_kcapi_test.o" 2>> "$LOG"
c++ -o snapshot_kcapi_test "$OUT/snapshot_kcapi_test.o" $CORE_OBJS -lm 2>> "$LOG"

# ---- 11) defense_p51_test (DEFENSE-1 WP3: P-51 显示越界显示层探针) ----
# 重建 ui_panels.o (DEFENSE-1 界检查辅助所在 TU; ImGui/GLFW 头 + 照
# build.sh 口径的额外 flag), 链接取 ../build/obj app/第三方对象。
if [ ! -f ../build/obj/ui_panels.o ]; then
    echo "错误: 未找到 ../build/obj/ui_panels.o —— 请先跑 ../build.sh" | tee -a "$LOG"
    exit 1
fi
TP=../third_party
UI_CXXFLAGS="$CXXFLAGS -Wno-nontrivial-memcall -DGL_SILENCE_DEPRECATION"
UI_INCS="$INC -I$TP/imgui-1.91.0 -I$TP/imgui-1.91.0/backends -I$TP/glfw-3.3.9/include"
echo "\$ c++ ui_panels.cpp (DEFENSE-1 重建)" >> "$LOG"
c++ $UI_CXXFLAGS $UI_INCS -c ../src/ui_panels.cpp -o "$OUT/ui_panels.o" 2>> "$LOG"
APP_LINK_OBJS="../build/obj/glfw_*.o ../build/obj/imgui*.o \
../build/obj/RtAudio.o ../build/obj/RtMidi.o \
../build/obj/audio.o ../build/obj/midi.o ../build/obj/keyboard.o \
../build/obj/panel_keyboard.o ../build/obj/seq.o"
# LOG-PATH-1 构建修复: seq.cpp (2026-11-08 SEQ 功能, 晚于 DEFENSE-1) 使
# ui_panels.o 引用 seq_panel; APP_LINK_OBJS 补 ../build/obj/seq.o, 否则
# defense_p51_test 链接缺符号 (既有脚本自 SEQ 落地起即断, 非本任务引入)。
echo "\$ c++ defense_p51_test.cpp" >> "$LOG"
c++ $CXXFLAGS $INC -c defense_p51_test.cpp -o "$OUT/defense_p51_test.o" 2>> "$LOG"
c++ -o defense_p51_test "$OUT/defense_p51_test.o" "$OUT/ui_panels.o" \
    $APP_LINK_OBJS $CORE_OBJS "$OUT/synth_ui.o" -lm \
    -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
    -framework CoreFoundation -framework CoreAudio -framework CoreMIDI 2>> "$LOG"

# ---- 12) defense1_kcapi_test (DEFENSE-1 WP4: T-i kcapi 路径 + T-d 回归) ----
echo "\$ cc defense1_kcapi_test.c" >> "$LOG"
cc $CFLAGS $INC -c defense1_kcapi_test.c -o "$OUT/defense1_kcapi_test.o" 2>> "$LOG"
c++ -o defense1_kcapi_test "$OUT/defense1_kcapi_test.o" $CORE_OBJS -lm 2>> "$LOG"

# ---- 13) logpath1_test (LOG-PATH-1 WP1: 失败可见化/绝对落点/幂等/格式 v1) ----
echo "\$ cc logpath1_test.c" >> "$LOG"
cc $CFLAGS $INC -c logpath1_test.c -o "$OUT/logpath1_test.o" 2>> "$LOG"
c++ -o logpath1_test "$OUT/logpath1_test.o" $CORE_OBJS -lm 2>> "$LOG"

# 变更摘要 (2026-11-17 LOG-PATH-1 WP3, 测试台侧):
#   新增第 13 段: logpath1_test —— alog_start 失败可见化(-1+stderr 含
#   路径/errno)/成功绝对落点/幂等/NULL 回退默认名/格式 v1 冻结(含
#   op=signal 行形) 库层判据, 只链 CORE_OBJS, 严格级零告警。
# 变更摘要 (2026-11-17 TBENCH-WIN-1, 测试台侧 WP1a):
#   platform_posix.c 并入 C 件编译循环 (action_log POSIX 调用点封装),
#   CORE_OBJS 追加 platform_posix.o; POSIX 实现即原 API 直转调,
#   Mac 行为逐位不变。产物仍 13 个测试二进制。
WARNS=$(grep -ci 'warning' "$LOG" || true)
echo "warning 计数: $WARNS" | tee -a "$LOG"
echo "构建完成: 13 个测试二进制 (见 $LOG)"
