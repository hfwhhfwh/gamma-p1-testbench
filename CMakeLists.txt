# gamma-p1-testbench
# Cross-platform real-time testbench for the gamma-p1 synthesizer kernel.
# Builds a single executable with vendored RtAudio/RtMidi/ImGui/GLFW.
#
# ---- 变更摘要 (TBENCH-INFRA-1, 2026-10-25) ----
#   S4 / P-39: USE_ASAN=ON 时原仅作用于 testbench 壳目标, gp1_kernel
#   成员未插桩; 现 USE_ASAN=ON 给 gp1_kernel 追加 -fsanitize=address
#   -fno-omit-frame-pointer, 并经 INTERFACE 链接选项同步覆盖所有链接
#   gp1_kernel 的可执行目标。USE_ASAN=OFF 路径一字未动。
#
# ---- 变更摘要 (SNAPSHOT-FIX-1, 2026-10-26) ----
#   S6: gp1_kernel 源列表追加 ${GP1_ROOT}/snapshot_v1.c (内核实施员
#   拆出的 snapshot_v1 实现 TU; V5 wrap/分发与 synth_restore_blob 在
#   此, kcapi 的 V5 保存口与 legacy 加载分发依赖它)。其余目标不动。
#
# ---- 变更摘要 (TBENCH-WIN-1, 2026-11-17, Windows 移植) ----
#   WP1a: gp1_kernel 源列表追加平台层 (WIN32 时 src/platform_win32.c,
#   否则 src/platform_posix.c — action_log.c 的 POSIX 调用点封装,
#   POSIX 实现即原 API 直转调, Mac 行为逐位不变)。
#   WP1b: MSVC 分支 — 内核成员 /W3 /std:c11, 宿主 /W4 /std:c++17,
#   全部 /fp:precise (严禁 /fp:fast) + /utf-8 (中文行字面量) +
#   _CRT_SECURE_NO_WARNINGS; 链接 opengl32 (imgui 内嵌 GL loader 直用)
#   与 WASAPI/MM 所需系统库 (avrt ksuser mfplat mfuuid wmcodecdspuuid
#   winmm ole32); 静态运行库 (/MT) 免装 VC  redist。控制台子系统为
#   add_executable 默认 (CONSOLE), stderr 诊断行 (LOG-PATH-1 落点行)
#   在控制台可见。RtAudio 编译定义 __WINDOWS_WASAPI__ / RtMidi
#   __WINDOWS_MM__ (Mac 侧 __MACOSX_CORE__ 分支逐字保留)。
#   WP3/WP4: 新增 GP1_TESTS 选项 (默认 OFF, Mac 默认构建表面不变) —
#   ON 时把 13 件测试台测试建成可执行目标 + CTest 注册, Windows 与
#   Mac 同一套; 产物运行 WORKING_DIRECTORY 为 tests/ (与手工脚本
#   惯例同), 判据口径与旗标照抄 tests/build_tbfix3_tests.sh。
#   默认 (GP1_TESTS=OFF, 非 MSVC) 路径与 Mac 原行为逐字一致。

cmake_minimum_required(VERSION 3.16)
project(gamma_p1_testbench LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Options ----
option(USE_ASAN "Build with AddressSanitizer" OFF)
option(GP1_TESTS "Build the 13 testbench test executables (TBENCH-WIN-1)" OFF)

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR "Supported platforms are Windows and macOS.")
endif()
if(WIN32 AND NOT MSVC)
    message(FATAL_ERROR "Windows builds currently require MSVC.")
endif()

if(MSVC)
    # TBENCH-WIN-1 WP1b: 静态运行库 (免 VC redist); 双配置统一
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- Kernel include roots ----
set(GP1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../gamma-p1" CACHE PATH
    "Path to the gamma-p1 kernel source tree")
if(NOT EXISTS "${GP1_ROOT}/synth.c")
    message(FATAL_ERROR "gamma-p1 kernel not found at GP1_ROOT=${GP1_ROOT}")
endif()
set(GP1_INCLUDES
    ${GP1_ROOT}
    ${GP1_ROOT}/vco_c
    ${GP1_ROOT}/vcf_a
    ${GP1_ROOT}/vca
    ${GP1_ROOT}/lfo
    ${GP1_ROOT}/env
    ${GP1_ROOT}/fm
)

# TBENCH-WIN-1 WP1a: 平台层 (Windows/POSIX 对偶实现)
if(WIN32)
    set(PLATFORM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_win32.c)
elseif(APPLE)
    set(PLATFORM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_posix.c)
endif()

# ---- Kernel static library (sources copied verbatim, no edits) ----
add_library(gp1_kernel STATIC
    ${GP1_ROOT}/vco_c/vco.c
    ${GP1_ROOT}/vco_c/wavetables_data.c
    ${GP1_ROOT}/vcf_a/vcf.c
    ${GP1_ROOT}/vca/vca.c
    ${GP1_ROOT}/lfo/lfo.c
    ${GP1_ROOT}/lfo/lfo_wavetables_data.c
    ${GP1_ROOT}/env/env.c
    ${GP1_ROOT}/fm/fm.c
    ${GP1_ROOT}/vmatrix.c
    ${GP1_ROOT}/voice.c
    ${GP1_ROOT}/synth.c
    ${GP1_ROOT}/snapshot_v1.c   # SNAPSHOT-FIX-1: S6 snapshot_v1 实现 TU
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kernel_capi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/action_log.c   # TBENCH-LOG-WIRE
    ${PLATFORM_SRC}                                # TBENCH-WIN-1 WP1a
)
target_include_directories(gp1_kernel PUBLIC ${GP1_INCLUDES} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(gp1_kernel PRIVATE
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-O2>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-std=c11>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-pedantic>
    $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
    # TBENCH-WIN-1 WP1b: MSVC 内核 /W3, 浮点钉死 precise
    $<$<C_COMPILER_ID:MSVC>:/W3>
    $<$<C_COMPILER_ID:MSVC>:/std:c11>
    $<$<C_COMPILER_ID:MSVC>:/fp:precise>
    $<$<C_COMPILER_ID:MSVC>:/utf-8>
)

# ---- GLFW (build from vendored source) ----
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.3.9)
add_subdirectory(${GLFW_DIR} glfw_build EXCLUDE_FROM_ALL)

# ---- RtAudio ----
add_library(rtaudio STATIC third_party/rtaudio-6.0.1/RtAudio.cpp)
target_include_directories(rtaudio PUBLIC third_party/rtaudio-6.0.1)
# TBENCH-WIN-1 WP2a: Windows 编译 WASAPI 后端 (库清单照抄 rtaudio
# CMakeLists WASAPI 段: ksuser mfplat mfuuid wmcodecdspuuid + winmm ole32,
# 另补 avrt); Mac 分支逐字保留
if(WIN32)
    target_compile_definitions(rtaudio PRIVATE __WINDOWS_WASAPI__)
    target_link_libraries(rtaudio PUBLIC
        avrt ksuser mfplat mfuuid wmcodecdspuuid winmm ole32
    )
else()
    target_compile_definitions(rtaudio PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtaudio PUBLIC
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()

# ---- RtMidi ----
add_library(rtmidi STATIC third_party/rtmidi-6.0.0/RtMidi.cpp)
target_include_directories(rtmidi PUBLIC third_party/rtmidi-6.0.0)
# TBENCH-WIN-1 WP2a: Windows 编译多媒体 MIDI 后端 (winmm)
if(WIN32)
    target_compile_definitions(rtmidi PRIVATE __WINDOWS_MM__)
    target_link_libraries(rtmidi PUBLIC winmm)
else()
    target_compile_definitions(rtmidi PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtmidi PUBLIC
        "-framework CoreMIDI"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()

# ---- Dear ImGui ----
# TBENCH-WIN-1 WP1d: imgui_impl_opengl3 用其内嵌 loader
# (imgui_impl_opengl3_loader.h), Windows 无需外部 GL loader; 链接
# opengl32 即可。版本与 Mac 同为 vendored 1.91.0 源码直编。
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.91.0)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
if(WIN32)
    target_link_libraries(imgui PUBLIC opengl32)
endif()

# ---- Main executable ----
add_executable(testbench
    src/main.cpp
    src/synth_ui.cpp
    src/audio.cpp
    src/midi.cpp
    src/keyboard.cpp
    src/ui_panels.cpp
    src/panel_keyboard.cpp
    src/seq.cpp
)
target_include_directories(testbench PRIVATE src ${GP1_INCLUDES})
# TBENCH-WIN-1 WP1b: Windows 链接系统库 (Mac framework 分支逐字保留)
if(WIN32)
    target_link_libraries(testbench PRIVATE
        gp1_kernel
        rtaudio
        rtmidi
        imgui
        glfw
        opengl32
    )
else()
    target_link_libraries(testbench PRIVATE
        gp1_kernel
        rtaudio
        rtmidi
        imgui
        glfw
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreFoundation"
    )
endif()

target_compile_options(testbench PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++17>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-nontrivial-memcall>
    # TBENCH-WIN-1 WP1b: MSVC 宿主 /W4, 浮点钉死 precise
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/std:c++17>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
)
target_compile_definitions(testbench PRIVATE GL_SILENCE_DEPRECATION)
if(WIN32)
    # TBENCH-WIN-1: UCRT POSIX 名直用 (getenv/snprintf 等), 非告警来源
    target_compile_definitions(testbench PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(USE_ASAN)
    # TBENCH-WIN-1: ASan 分支仅非 MSVC (clang/gcc); MSVC 不支持该旗标组合
    if(NOT MSVC)
    # TBENCH-INFRA-1: P-39 gp1_kernel 同步插桩, 否则壳目标 ASan 链接时
    # 内核成员对象缺 __asan_* 引用检查、堆错漏报
    target_compile_options(gp1_kernel PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    # TBENCH-INFRA-1: P-39 链接 gp1_kernel 的可执行目标同步 -fsanitize=address
    target_link_options(gp1_kernel INTERFACE -fsanitize=address)
    target_compile_options(testbench PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(testbench PRIVATE -fsanitize=address)
    endif()
endif()

# ---- 13 件测试台测试 (TBENCH-WIN-1 WP3/WP4; 默认 OFF, Mac 默认构建不变) ----
# 旗标/链接对象照抄 tests/build_tbfix3_tests.sh 各段; C 件 /std:c11,
# C++ 件 /std:c++17, 严格级对齐 Mac 脚本 (-Werror -pedantic 的 MSVC
# 对应为 /WX /permissive-; 考虑到 MSVC 告警口径未本地验证, 不挂 /WX,
# 告警清单由 verify_win.ps1 收集入报告)。
if(GP1_TESTS)
    enable_testing()

    # 测试产物目录 (运行 WORKING_DIRECTORY 用, 照 Mac 惯例)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/tbfix2_out)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/tbfix3_out)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/log_wire_out)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/midi_route_out)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/sfix1_out)
    set(GP1_TEST_WD ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    # synth_ui 复用件 (照脚本: 各 UI 路径测试自编译 synth_ui.o 链入)
    add_library(gp1_test_ui STATIC src/synth_ui.cpp)
    target_include_directories(gp1_test_ui PUBLIC src ${GP1_INCLUDES})
    target_link_libraries(gp1_test_ui PUBLIC gp1_kernel)

    # 应用对象复用件 (defense_p51 直链 app/第三方对象, 照脚本 WP3 段)
    add_library(gp1_app_objs STATIC
        src/audio.cpp src/midi.cpp src/keyboard.cpp
        src/panel_keyboard.cpp src/seq.cpp
    )
    target_include_directories(gp1_app_objs PUBLIC src ${GP1_INCLUDES})
    target_link_libraries(gp1_app_objs PUBLIC
        gp1_kernel rtaudio rtmidi imgui glfw
    )

    # 键盘无头桩件 (keyboard_stuck_test 用, 照脚本第 4 段:
    # kbd_stubs 的 GLFW/imgui 桩头优先于真实头)
    add_library(gp1_keyboard_stubbed STATIC src/keyboard.cpp)
    target_include_directories(gp1_keyboard_stubbed BEFORE
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/kbd_stubs)
    target_include_directories(gp1_keyboard_stubbed PUBLIC src ${GP1_INCLUDES})

    # Shared C++ objects also contain UTF-8 comments and must use the same
    # floating-point and source-encoding settings as the host and test targets.
    foreach(t gp1_test_ui gp1_app_objs gp1_keyboard_stubbed)
        target_compile_options(${t} PRIVATE
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++17>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-nontrivial-memcall>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/std:c++17>
            $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
            $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
            $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
        )
        target_compile_definitions(${t} PRIVATE GL_SILENCE_DEPRECATION)
    endforeach()

    # 13 件统一定义: 名 / 源 / 额外链接
    # (顺序照 build_tbfix3_tests.sh 段序)
    set(GP1_TEST_CORE gp1_kernel)
    add_executable(kcapi_readback_test tests/kcapi_readback_test.c)
    target_link_libraries(kcapi_readback_test PRIVATE ${GP1_TEST_CORE})

    add_executable(log_wire_test tests/log_wire_test.cpp)
    target_link_libraries(log_wire_test PRIVATE gp1_test_ui)

    add_executable(midi_route_test tests/midi_route_test.cpp)
    target_link_libraries(midi_route_test PRIVATE gp1_test_ui)

    add_executable(keyboard_stuck_test tests/keyboard_stuck_test.cpp)
    target_link_libraries(keyboard_stuck_test PRIVATE
        gp1_keyboard_stubbed gp1_test_ui)

    add_executable(panel_kb_fix_test tests/panel_kb_fix_test.cpp)
    target_link_libraries(panel_kb_fix_test PRIVATE gp1_test_ui)

    add_executable(p0_modsrc_test tests/p0_modsrc_test.cpp)
    target_link_libraries(p0_modsrc_test PRIVATE gp1_test_ui)

    add_executable(p3_headless_test tests/p3_headless_test.cpp)
    target_link_libraries(p3_headless_test PRIVATE gp1_test_ui)

    add_executable(snapshot_envread_test tests/snapshot_envread_test.c)
    target_link_libraries(snapshot_envread_test PRIVATE ${GP1_TEST_CORE})

    add_executable(snapshot_ui_test tests/snapshot_ui_test.cpp)
    target_link_libraries(snapshot_ui_test PRIVATE gp1_test_ui)

    add_executable(snapshot_kcapi_test tests/snapshot_kcapi_test.c)
    target_link_libraries(snapshot_kcapi_test PRIVATE ${GP1_TEST_CORE})

    # defense_p51: 直链 ui_panels + app 对象 + ImGui/GLFW/RtAudio/RtMidi
    add_executable(defense_p51_test
        tests/defense_p51_test.cpp
        src/ui_panels.cpp
    )
    target_include_directories(defense_p51_test PRIVATE src ${GP1_INCLUDES})
    target_link_libraries(defense_p51_test PRIVATE
        gp1_app_objs gp1_test_ui
    )
    if(NOT MSVC)
        # 照脚本第 11 段 UI_CXXFLAGS: ui_panels.cpp 含 imgui 头,
        # -Wnontrivial-memcall 与 GL 弃用告警按 build.sh 口径关闭
        target_compile_options(defense_p51_test PRIVATE
            -Wno-nontrivial-memcall)
        target_compile_definitions(defense_p51_test PRIVATE
            GL_SILENCE_DEPRECATION)
    endif()
    if(NOT WIN32)
        # 照脚本第 11 段: ui_panels/imgui 对象需 GL 与系统 framework
        target_link_libraries(defense_p51_test PRIVATE
            "-framework OpenGL"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            "-framework CoreFoundation"
            "-framework CoreAudio"
            "-framework CoreMIDI"
        )
    endif()

    add_executable(defense1_kcapi_test tests/defense1_kcapi_test.c)
    target_link_libraries(defense1_kcapi_test PRIVATE ${GP1_TEST_CORE})

    add_executable(logpath1_test tests/logpath1_test.c)
    target_link_libraries(logpath1_test PRIVATE ${GP1_TEST_CORE})

    # TBENCH-WIN-1 WP4: 跨平台渲染基线 dump 工具 (非第 14 件测试, 不进
    # ctest, 不挂 -Werror 平台差异口径; 产物供 baseline_win 容差比对)
    add_executable(win_render_dump tests/win_render_dump.c)
    target_link_libraries(win_render_dump PRIVATE ${GP1_TEST_CORE})
    target_compile_options(win_render_dump PRIVATE
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-O2>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wextra>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-std=c11>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-pedantic>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
        $<$<C_COMPILER_ID:MSVC>:/W4>
        $<$<C_COMPILER_ID:MSVC>:/std:c11>
        $<$<C_COMPILER_ID:MSVC>:/fp:precise>
        $<$<C_COMPILER_ID:MSVC>:/utf-8>
    )

    # C 件与 C++ 件分两组给 -std (clang 拒绝 C++ 用 -std=c11)
    set(GP1_TEST_TARGETS_C
        kcapi_readback_test snapshot_envread_test
        snapshot_kcapi_test defense1_kcapi_test logpath1_test
    )
    set(GP1_TEST_TARGETS_CXX
        log_wire_test midi_route_test keyboard_stuck_test
        panel_kb_fix_test p0_modsrc_test p3_headless_test
        snapshot_ui_test defense_p51_test
    )
    foreach(t ${GP1_TEST_TARGETS_C})
        target_compile_options(${t} PRIVATE
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-O2>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Werror>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-std=c11>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-pedantic>
            $<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
            $<$<C_COMPILER_ID:MSVC>:/W4>
            $<$<C_COMPILER_ID:MSVC>:/std:c11>
            $<$<C_COMPILER_ID:MSVC>:/fp:precise>
            $<$<C_COMPILER_ID:MSVC>:/utf-8>
        )
    endforeach()
    foreach(t ${GP1_TEST_TARGETS_CXX})
        target_compile_options(${t} PRIVATE
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-std=c++17>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-unused-parameter>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
            $<$<CXX_COMPILER_ID:MSVC>:/std:c++17>
            $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
            $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
            $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
        )
    endforeach()
    foreach(t ${GP1_TEST_TARGETS_C} ${GP1_TEST_TARGETS_CXX})
        target_include_directories(${t} PRIVATE src ${GP1_INCLUDES})
        if(NOT WIN32)
            target_link_libraries(${t} PRIVATE m)   # 照脚本 -lm
        endif()
        if(WIN32)
            target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
        add_test(NAME ${t}
                 COMMAND ${t}
                 WORKING_DIRECTORY ${GP1_TEST_WD})
    endforeach()
    if(NOT WIN32)
        target_link_libraries(win_render_dump PRIVATE m)
    endif()
endif()

# ---- Convenience targets ----
add_custom_target(run
    COMMAND $<TARGET_FILE:testbench>
    DEPENDS testbench
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
