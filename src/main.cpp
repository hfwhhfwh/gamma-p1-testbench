/*
 * main.cpp — gamma-p1-testbench 入口
 *
 * 启动顺序（硬性要求）：
 *   1. alog_start (LOG-PATH-1: 日志最优先, 连初始化接线也记录;
 *      路径取环境变量 GP1_LOG_PATH, 未设回退 "testbench_action.log";
 *      成功后 atexit(alog_stop) 收口)
 *   2. 注册 SIGINT/SIGTERM handler (LOG-PATH-1: 仅置 sig_atomic_t
 *      标志, 主循环轮询)
 *   3. vco_bank_init(&vco_default_bank) 在 ui_init() 内完成
 *   4. synth_init()
 *   5. 开 GLFW 窗口 / ImGui
 *   6. 开 RtAudio 流
 *   7. 开 RtMidi（可选）
 *   退出：主循环 (信号标志 || glfwWindowShouldClose) 退出后单一出口
 *   cleanup —— 顺序 audio_stop -> midi_stop -> ui_panels_shutdown ->
 *   glfwDestroyWindow -> glfwTerminate -> alog_stop -> ui_shutdown。
 *
 * 变更摘要 (2026-09-16 TBENCH-LOG-WIRE):
 *   操作日志常驻: ui_init 之前 alog_start("testbench_action.log")
 *   (连初始化接线也记录); 主循环每 10s 一次 alog_state 心跳摘要;
 *   ui_shutdown 之前 alog_stop (flush 落盘)。
 *
 * 变更摘要 (2026-11-17 LOG-PATH-1, 宿主侧 P-28 失败可见化 + P-43 退出收口):
 *   WP1: alog_start 返回值检查, 失败 stderr "操作日志不可用，继续运
 *   行但不落盘" 并继续; 日志路径 getenv("GP1_LOG_PATH"), 未设回退
 *   默认名 (默认即隐身: 不设时落点/文件名/追加模式与旧版一致)。
 *   WP2: alog_start 成功后 atexit(alog_stop) (alog_stop 幂等);
 *   SIGINT/SIGTERM handler 仅置 volatile sig_atomic_t 标志, 主循环
 *   轮询后经正常清理退出, 清理前记一行 op=signal (格式 v1 冻结,
 *   行结构不变); glfwInit/glfwCreateWindow/ui_panels_init 三失败
 *   路径改走单一出口 cleanup (清理顺序与正常路径一致,
 *   audio_stop/midi_stop 未启动时为空操作)。所有 return 路径经
 *   cleanup 的 alog_stop (+atexit 双保险) 落 # session_end 尾行。
 *
 * 变更摘要 (2026-11-17 TBENCH-WIN-1 WP1c/WP2c):
 *   GLFW_OPENGL_FORWARD_COMPAT hint 收进 #ifdef __APPLE__ (Windows
 *   上下文创建不设 forward-compat, 3.2 core 直用); Windows 控制台
 *   注册 SetConsoleCtrlHandler 处理 CTRL_C_EVENT/CTRL_CLOSE_EVENT,
 *   仅置同一 sig_atomic_t 标志走同一收口路径; op=signal 行 target
 *   按任务书规格写 "CTRL_C|CTRL_CLOSE" (格式 v1 行结构与其余列
 *   不变)。Mac 路径 (std::signal(SIGINT/SIGTERM) + 原 target 串)
 *   逐字保留。
 */

#include "synth_ui.h"
#include "audio.h"
#include "midi.h"
#include "keyboard.h"
#include "ui_panels.h"
#include "action_log.h"   /* TBENCH-LOG-WIRE */

#include "GLFW/glfw3.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32   /* TBENCH-WIN-1 WP2c: Windows 控制台信号走同一标志位 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace gp1_ui;

/* LOG-PATH-1 WP2b: 信号 handler 只置标志 (红线: 仅写 sig_atomic_t,
 * 不做任何清理/IO), 主循环轮询后经正常清理路径退出 */
static volatile sig_atomic_t g_shutdown_signal = 0;

static void on_shutdown_signal(int sig)
{
    g_shutdown_signal = sig;
}

#ifdef _WIN32   /* TBENCH-WIN-1 WP2c: CTRL_C/CTRL_CLOSE 置同一标志 */
static BOOL WINAPI on_console_ctrl(DWORD ctrl_type)
{
    (void)ctrl_type;
    g_shutdown_signal = 1;
    return TRUE;
}
#endif

int main(int /*argc*/, char * /*argv*/[])
{
    AppState app;
    int rc = 0;
    bool glfw_ok = false;
    bool panels_ok = false;
    GLFWwindow *window = nullptr;

    /* TBENCH-LOG-WIRE: 日志先于 ui_init 开启 (初始化接线也记录);
     * LOG-PATH-1 WP1b: 追加模式不覆盖历史 session; 路径 GP1_LOG_PATH
     * 未设回退默认名; 失败不致命 (P-28): 继续运行但不落盘 */
    const char *alog_path = std::getenv("GP1_LOG_PATH");
    if (alog_start(alog_path ? alog_path : "testbench_action.log") != 0) {
        std::fprintf(stderr, "[alog] 操作日志不可用，继续运行但不落盘\n");
    } else {
        /* LOG-PATH-1 WP2a: atexit 收口 —— alog_stop 幂等 (g_fp==NULL
         * 早退, action_log.c), 与正常路径显式调用双保险 */
        std::atexit(alog_stop);
    }

    /* LOG-PATH-1 WP2b: SIGINT/SIGTERM 优雅退出 (先注册, 覆盖全生命周期);
     * TBENCH-WIN-1 WP2c: Windows 控制台用 SetConsoleCtrlHandler
     * (CTRL_C_EVENT/CTRL_CLOSE_EVENT), 置同一 sig_atomic_t 标志走同一
     * 收口路径 */
#ifdef _WIN32
    SetConsoleCtrlHandler(on_console_ctrl, TRUE);
#else
    std::signal(SIGINT, on_shutdown_signal);
    std::signal(SIGTERM, on_shutdown_signal);
#endif

    /* 步骤 1+2：bank init + synth init */
    ui_init(&app);

    /* 步骤 3：GLFW + ImGui */
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        rc = 1;
        goto cleanup;
    }
    glfw_ok = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__   /* TBENCH-WIN-1: Mac 专用 forward-compat hint 收拢 */
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    window = glfwCreateWindow(1600, 1200, "gamma-p1-testbench", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        rc = 1;
        goto cleanup;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!ui_panels_init(window)) {
        std::fprintf(stderr, "ui_panels_init failed\n");
        rc = 1;
        goto cleanup;
    }
    panels_ok = true;

    keyboard_init(window, &app);

    /* 步骤 4：开音频流 */
    if (!audio_start(&app)) {
        std::fprintf(stderr, "audio_start failed (no audio device?)\n");
    }

    /* 步骤 5：开 MIDI */
    if (!midi_start(&app)) {
        std::fprintf(stderr, "midi_start failed (no MIDI input?)\n");
    }

    /* 主循环 (作用域包裹: goto cleanup 不跨过本段任何变量初始化) */
    {
        double last_state_log = glfwGetTime();   /* TBENCH-LOG-WIRE: 心跳计时 */
        while (!g_shutdown_signal && !glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (!ui_panels_frame(&app))
                break;
            /* TBENCH-LOG-WIRE: 每 10s 一条 op=state 心跳摘要 */
            double now = glfwGetTime();
            if (now - last_state_log >= 10.0) {
                alog_state(app.synth, "user");
                last_state_log = now;
            }
        }

        /* LOG-PATH-1 WP2b: 信号触发的退出在清理前记一行 op=signal
         * (格式 v1 行结构不变); TBENCH-WIN-1 WP2c: target 记
         * "CTRL_C|CTRL_CLOSE" (任务书规格, 行结构/其余列不变) */
        if (g_shutdown_signal)
#ifdef _WIN32
            alog_log("signal", "CTRL_C|CTRL_CLOSE", "graceful shutdown", "system");
#else
            alog_log("signal", "SIGINT|SIGTERM", "graceful shutdown", "system");
#endif
    }

cleanup:
    /* 清理顺序与正常路径一致; audio_stop/midi_stop 未启动时为空操作
     * (audio.cpp/midi.cpp 空守卫), 可安全单一出口 */
    audio_stop(&app);
    midi_stop(&app);
    if (panels_ok) ui_panels_shutdown();
    if (window) glfwDestroyWindow(window);
    if (glfw_ok) glfwTerminate();
    alog_stop();      /* TBENCH-LOG-WIRE: flush + 关闭 (synth 销毁前);
                         LOG-PATH-1: 幂等, atexit 再调为 no-op */
    ui_shutdown(&app);

    return rc;
}
