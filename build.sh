#!/bin/bash
# Fallback build script for systems without CMake installed.
# One-command build: ./build.sh
# ASAN build: ./build.sh asan
#
# ---- 变更摘要 (TBENCH-INFRA-1, 2026-10-25) ----
#   S7 / P-68: set -e 之后新增平台守卫, 非 macOS (Darwin) 立即退出 1,
#   避免在 Linux 等平台上跑出一半的半成品构建。正常路径行为不变。
#
# ---- 变更摘要 (SNAPSHOT-FIX-1, 2026-10-26) ----
#   S6: KERNEL_SRCS 追加 ${GP1_ROOT}/snapshot_v1.c (内核拆出的 snapshot_v1
#   实现 TU)。编译循环按 basename 生成 snapshot_v1.o, 链接处 ${OBJ_DIR}/*.o
#   通配自动覆盖, 无需另补。
#
# ---- 变更摘要 (TBENCH-WIN-1, 2026-11-17) ----
#   WP1a: action_log.c 的 POSIX 调用点收进平台层, 新增 platform_posix.o
#   编译段 (platform.h/.c 更新触发重编); 链接走 ${OBJ_DIR}/*.o 通配自动
#   覆盖。Mac 行为逐位不变 (POSIX 实现即原 API 直转调)。

set -e

# TBENCH-INFRA-1: P-68 build.sh 仅支持 macOS, 非 Darwin 立即失败
[ "$(uname -s)" = "Darwin" ] || { echo "build.sh 仅支持 macOS (Darwin)"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
mkdir -p "${BUILD_DIR}"

CC=${CC:-cc}
CXX=${CXX:-clang++}

GP1_ROOT="${SCRIPT_DIR}/../gamma-p1"
TP="${SCRIPT_DIR}/third_party"

CFLAGS="-O2 -Wall -Wextra -std=c11 -pedantic -Wno-unused-parameter"
CXXFLAGS="-O2 -Wall -Wextra -std=c++17 -Wno-unused-parameter -Wno-nontrivial-memcall -DGL_SILENCE_DEPRECATION"
INCS="-I${SCRIPT_DIR}/src -I${GP1_ROOT} -I${GP1_ROOT}/vco_c -I${GP1_ROOT}/vcf_a -I${GP1_ROOT}/vca -I${GP1_ROOT}/lfo -I${GP1_ROOT}/env -I${GP1_ROOT}/fm"

if [ "$1" == "asan" ]; then
    CFLAGS="$CFLAGS -fsanitize=address -fno-omit-frame-pointer"
    CXXFLAGS="$CXXFLAGS -fsanitize=address -fno-omit-frame-pointer"
    LDFLAGS="-fsanitize=address"
fi

OBJ_DIR="${BUILD_DIR}/obj"
mkdir -p "${OBJ_DIR}"

# ---- Kernel C sources (verbatim, no edits) ----
KERNEL_SRCS=(
    "${GP1_ROOT}/vco_c/vco.c"
    "${GP1_ROOT}/vco_c/wavetables_data.c"
    "${GP1_ROOT}/vcf_a/vcf.c"
    "${GP1_ROOT}/vca/vca.c"
    "${GP1_ROOT}/lfo/lfo.c"
    "${GP1_ROOT}/lfo/lfo_wavetables_data.c"
    "${GP1_ROOT}/env/env.c"
    "${GP1_ROOT}/fm/fm.c"
    "${GP1_ROOT}/vmatrix.c"
    "${GP1_ROOT}/voice.c"
    "${GP1_ROOT}/synth.c"
    "${GP1_ROOT}/snapshot_v1.c"   # SNAPSHOT-FIX-1: S6 snapshot_v1 实现 TU
)

# Kernel headers: any newer kernel header forces rebuild of all kernel objects
# (Voice/FmSynth layouts flow through voice.c/synth.c/vmatrix.c — stale objects
# cause ABI mismatch crashes, not just silent misbehavior).
KERNEL_HDRS=(
    "${GP1_ROOT}/"*.h "${GP1_ROOT}/vco_c/"*.h "${GP1_ROOT}/vcf_a/"*.h
    "${GP1_ROOT}/vca/"*.h "${GP1_ROOT}/lfo/"*.h "${GP1_ROOT}/env/"*.h
    "${GP1_ROOT}/fm/"*.h
)
kernel_hdr_newer_than() {
    for hdr in "${KERNEL_HDRS[@]}"; do
        if [ "$hdr" -nt "$1" ]; then return 0; fi
    done
    return 1
}

for src in "${KERNEL_SRCS[@]}"; do
    obj="${OBJ_DIR}/$(basename "${src}" .c).o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || kernel_hdr_newer_than "$obj"; then
        echo "CC  $src"
        ${CC} ${CFLAGS} ${INCS} -c "$src" -o "$obj"
    fi
done

# ---- GLFW (vendored) ----
GLFW_DIR="${TP}/glfw-3.3.9"
GLFW_SRCS=(
    "${GLFW_DIR}/src/context.c"
    "${GLFW_DIR}/src/init.c"
    "${GLFW_DIR}/src/input.c"
    "${GLFW_DIR}/src/monitor.c"
    "${GLFW_DIR}/src/vulkan.c"
    "${GLFW_DIR}/src/window.c"
    "${GLFW_DIR}/src/cocoa_init.m"
    "${GLFW_DIR}/src/cocoa_joystick.m"
    "${GLFW_DIR}/src/cocoa_monitor.m"
    "${GLFW_DIR}/src/cocoa_time.c"
    "${GLFW_DIR}/src/cocoa_window.m"
    "${GLFW_DIR}/src/nsgl_context.m"
    "${GLFW_DIR}/src/posix_thread.c"
    "${GLFW_DIR}/src/osmesa_context.c"
    "${GLFW_DIR}/src/egl_context.c"
)
GLFW_CFLAGS="-I${GLFW_DIR}/include -D_GLFW_COCOA -D_GLFW_USE_RETINA"
for src in "${GLFW_SRCS[@]}"; do
    obj="${OBJ_DIR}/glfw_$(basename "${src}" .m).o"
    obj="${obj%.o}.o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "CC  $src"
        ${CC} -O2 -Wall -Wextra -std=c11 ${GLFW_CFLAGS} -c "$src" -o "$obj"
    fi
done

# ---- RtAudio ----
if [ ! -f "${OBJ_DIR}/RtAudio.o" ] || [ "${TP}/rtaudio-6.0.1/RtAudio.cpp" -nt "${OBJ_DIR}/RtAudio.o" ]; then
    echo "CXX ${TP}/rtaudio-6.0.1/RtAudio.cpp"
    ${CXX} ${CXXFLAGS} -I${TP}/rtaudio-6.0.1 -D__MACOSX_CORE__ -c "${TP}/rtaudio-6.0.1/RtAudio.cpp" -o "${OBJ_DIR}/RtAudio.o"
fi

# ---- RtMidi ----
if [ ! -f "${OBJ_DIR}/RtMidi.o" ] || [ "${TP}/rtmidi-6.0.0/RtMidi.cpp" -nt "${OBJ_DIR}/RtMidi.o" ]; then
    echo "CXX ${TP}/rtmidi-6.0.0/RtMidi.cpp"
    ${CXX} ${CXXFLAGS} -I${TP}/rtmidi-6.0.0 -D__MACOSX_CORE__ -c "${TP}/rtmidi-6.0.0/RtMidi.cpp" -o "${OBJ_DIR}/RtMidi.o"
fi

# ---- Dear ImGui ----
IMGUI_DIR="${TP}/imgui-1.91.0"
IMGUI_SRCS=(
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
for src in "${IMGUI_SRCS[@]}"; do
    obj="${OBJ_DIR}/$(basename "${src}" .cpp).o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "CXX $src"
        ${CXX} ${CXXFLAGS} -I${IMGUI_DIR} -I${IMGUI_DIR}/backends -I${GLFW_DIR}/include -c "$src" -o "$obj"
    fi
done

# ---- C API wrapper (compiled as C11) ----
if [ ! -f "${OBJ_DIR}/kernel_capi.o" ] || [ "${SCRIPT_DIR}/src/kernel_capi.c" -nt "${OBJ_DIR}/kernel_capi.o" ] || [ "${SCRIPT_DIR}/src/kernel_capi.h" -nt "${OBJ_DIR}/kernel_capi.o" ] || kernel_hdr_newer_than "${OBJ_DIR}/kernel_capi.o"; then
    echo "CC  ${SCRIPT_DIR}/src/kernel_capi.c"
    ${CC} ${CFLAGS} ${INCS} -c "${SCRIPT_DIR}/src/kernel_capi.c" -o "${OBJ_DIR}/kernel_capi.o"
fi

# ---- Action log module (TBENCH-LOG-WIRE: 操作日志常驻接线的构建接入) ----
if [ ! -f "${OBJ_DIR}/action_log.o" ] || [ "${SCRIPT_DIR}/src/action_log.c" -nt "${OBJ_DIR}/action_log.o" ] || [ "${SCRIPT_DIR}/src/action_log.h" -nt "${OBJ_DIR}/action_log.o" ]; then
    echo "CC  ${SCRIPT_DIR}/src/action_log.c"
    ${CC} ${CFLAGS} ${INCS} -c "${SCRIPT_DIR}/src/action_log.c" -o "${OBJ_DIR}/action_log.o"
fi

# ---- Platform layer (TBENCH-WIN-1: action_log 的 POSIX 调用点封装) ----
PLATFORM_SRC="${SCRIPT_DIR}/src/platform_posix.c"
if [ ! -f "${OBJ_DIR}/platform_posix.o" ] || [ "${PLATFORM_SRC}" -nt "${OBJ_DIR}/platform_posix.o" ] || [ "${SCRIPT_DIR}/src/platform.h" -nt "${OBJ_DIR}/platform_posix.o" ]; then
    echo "CC  ${PLATFORM_SRC}"
    ${CC} ${CFLAGS} ${INCS} -c "${PLATFORM_SRC}" -o "${OBJ_DIR}/platform_posix.o"
fi

# ---- App sources ----
# NOTE: no compiler-generated header deps here; rebuild app objects when any
# project header is newer (stale AppState layouts cause silent ABI breakage).
APP_SRCS=(
    "${SCRIPT_DIR}/src/main.cpp"
    "${SCRIPT_DIR}/src/synth_ui.cpp"
    "${SCRIPT_DIR}/src/audio.cpp"
    "${SCRIPT_DIR}/src/midi.cpp"
    "${SCRIPT_DIR}/src/keyboard.cpp"
    "${SCRIPT_DIR}/src/ui_panels.cpp"
    "${SCRIPT_DIR}/src/panel_keyboard.cpp"
    "${SCRIPT_DIR}/src/seq.cpp"
)
for src in "${APP_SRCS[@]}"; do
    obj="${OBJ_DIR}/$(basename "${src}" .cpp).o"
    rebuild=0
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        rebuild=1
    else
        for hdr in "${SCRIPT_DIR}/src/"*.h; do
            if [ "$hdr" -nt "$obj" ]; then rebuild=1; break; fi
        done
    fi
    if [ "$rebuild" -eq 1 ]; then
        echo "CXX $src"
        ${CXX} ${CXXFLAGS} ${INCS} -I${IMGUI_DIR} -I${IMGUI_DIR}/backends -I${GLFW_DIR}/include \
            -I${TP}/rtaudio-6.0.1 -I${TP}/rtmidi-6.0.0 -c "$src" -o "$obj"
    fi
done

# ---- Link ----
echo "LINK testbench"
${CXX} ${CXXFLAGS} ${LDFLAGS} ${OBJ_DIR}/*.o \
    -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo \
    -framework CoreFoundation -framework CoreAudio -framework CoreMIDI \
    -o "${BUILD_DIR}/testbench"

echo "Build OK: ${BUILD_DIR}/testbench"
