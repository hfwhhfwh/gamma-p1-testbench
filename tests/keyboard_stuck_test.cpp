// keyboard_stuck_test.cpp — TBENCH-FIX-3 T-D: PC 键盘挂音回归 (P-14/P-15)
// (不进构建系统, 手工编译, 同 midi_route_test.cpp 惯例)
//
// 驱动【真实】keyboard.cpp (修复后的按下登记/松键路径), GLFW/imgui 走
// kbd_stubs/ 无头桩 (glfwGetKey 由 g_kbd_stub_keys 驱动, WantTextInput
// 可控); ui_note_on/ui_note_off_note 走真实 synth_ui 音符栈 —— 与
// midi_route_test 同一批 ui_* 函数, 非测试桩。
//
// 口径差异 (不一致项登记): 测试台是 GLFW 桌面 app, 无头环境无真实
// 键盘事件; 本驱动经桩注入按键状态后调真实 keyboard_poll, 等价于
// 每帧轮询路径 (glfwGetKey 是 keyboard.cpp 唯一的键盘输入来源)。
//
// 检查:
//  D1 P-14 主场景: base=48 按 Z (登记 60) -> kb_base_note+=12 -> 松 Z:
//     off 落在登记值 60 (不按新 base 重算 72), 音符栈空, 无残留;
//  D2 P-15 场景: 按住 Z (60) 后 base 提到 120 (当前名 132>127):
//     poll 按登记值发 off 60, 无泄漏;
//  D3 按下瞬间 note>127: base=120 按 Z (132>127) 不登记不发 note_on,
//     栈恒空 (按不下 = 无 off 责任);
//  D4 总开关失效路径: 按住 Z (60) -> kb_base_note+=12 -> 关总开关:
//     释放走登记值 60, 栈空。
//
// 运行: 在 gamma-p1-testbench/tests/ 下 ./keyboard_stuck_test
//   (日志写 tbfix3_out/, 目录须已存在; alog 追加模式, 重跑前清场)

#include "GLFW/glfw3.h"   /* kbd_stubs 无头桩 (-I kbd_stubs 优先) */
#include "imgui.h"        /* 同上 */

#include "../src/keyboard.h"
#include "../src/synth_ui.h"
#include "../src/action_log.h"

#include <cstdio>
#include <cstring>

using namespace gp1_ui;

/* ---- 桩状态定义 (单实例) ---- */
int g_kbd_stub_keys[KBD_STUB_KEY_COUNT];
namespace ImGui {
ImGuiIO &GetIO() { static ImGuiIO io{false}; return io; }
}

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { std::printf("  PASS "); std::printf(__VA_ARGS__); } \
    else      { std::printf("  FAIL "); std::printf(__VA_ARGS__); g_fails++; } \
    std::printf("\n");                                           \
} while (0)

static char g_dummy_window[16];   /* keyboard_poll 仅判空, 桩不解引用 */

int main(void)
{
    const char *logpath = "tbfix3_out/keyboard_stuck_actions.log";
    std::remove(logpath);          /* alog 追加模式: 重跑前清场 */
    std::printf("== TBENCH-FIX-3 T-D: PC 键盘挂音回归 (P-14/P-15) ==\n\n");
    GLFWwindow *win = reinterpret_cast<GLFWwindow *>(g_dummy_window);

    /* ---- D1: P-14 主场景 —— 按住中 Octave+, 松键 off 落登记值 ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        app.kb_base_note = 48;                 /* base+12 = 60 */
        keyboard_init(win, &app);
        std::memset(g_kbd_stub_keys, 0, sizeof(g_kbd_stub_keys));

        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_PRESS;
        keyboard_poll(win, &app);              /* 按下沿: note_on 60, 登记 */
        bool held60 = ui_is_note_held(&app, 60);

        app.kb_base_note += 12;                /* 按住期间 Octave+ */
        keyboard_poll(win, &app);              /* 仍按住: 无动作 */

        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_RELEASE;
        keyboard_poll(win, &app);              /* 松键: off 应落登记值 60 */
        std::printf("  信息  D1: 松键后 held(60)=%d held(72)=%d 栈深=%zu "
                    "current_note=%d\n",
                    (int)ui_is_note_held(&app, 60),
                    (int)ui_is_note_held(&app, 72),
                    app.held_notes.size(), app.current_note);
        CHECK(held60 && !ui_is_note_held(&app, 60) &&
              !ui_is_note_held(&app, 72) && app.held_notes.empty() &&
              app.current_note == -1,
              "D1: P-14 —— Octave+ 后松键 off 落登记值 60 (非 72), "
              "音符栈空无残留");
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- D2: P-15 场景 —— 按住后 base 提升使当前名 >127 ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        app.kb_base_note = 48;
        keyboard_init(win, &app);
        std::memset(g_kbd_stub_keys, 0, sizeof(g_kbd_stub_keys));

        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_PRESS;
        keyboard_poll(win, &app);              /* 登记 60 */
        bool held60 = ui_is_note_held(&app, 60);

        app.kb_base_note = 120;                /* 当前名 132 > 127 */
        keyboard_poll(win, &app);              /* 应按登记值发 off 60 */
        std::printf("  信息  D2: base->120 后 held(60)=%d 栈深=%zu "
                    "current_note=%d\n",
                    (int)ui_is_note_held(&app, 60), app.held_notes.size(),
                    app.current_note);
        CHECK(held60 && !ui_is_note_held(&app, 60) &&
              app.held_notes.empty() && app.current_note == -1,
              "D2: P-15 —— note>127 分支按登记值发 off 再清标记, 无泄漏");
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- D3: 按下瞬间 note>127 —— 不登记 (按不下 = 无 off 责任) ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        app.kb_base_note = 120;                /* Z -> 132 > 127 */
        keyboard_init(win, &app);
        std::memset(g_kbd_stub_keys, 0, sizeof(g_kbd_stub_keys));

        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_PRESS;
        keyboard_poll(win, &app);              /* 不允许登记/发 note_on */
        bool any_held = !app.held_notes.empty();
        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_RELEASE;
        keyboard_poll(win, &app);              /* 无 off 责任 */
        std::printf("  信息  D3: 按/松全程栈深=%zu current_note=%d\n",
                    app.held_notes.size(), app.current_note);
        CHECK(!any_held && app.held_notes.empty() && app.current_note == -1,
              "D3: P-15 —— 按下瞬间 note>127 不登记不发 note_on, 栈恒空");
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- D4: 总开关失效路径 —— 释放走登记值 ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        app.kb_base_note = 48;
        keyboard_init(win, &app);
        std::memset(g_kbd_stub_keys, 0, sizeof(g_kbd_stub_keys));

        g_kbd_stub_keys[GLFW_KEY_Z] = GLFW_PRESS;
        keyboard_poll(win, &app);              /* 登记 60 */
        app.kb_base_note += 12;                /* 按住期间 Octave+ */
        app.kb_pc_map_enabled = false;         /* 关总开关 (键仍按着) */
        keyboard_poll(win, &app);              /* 释放残留: off 应落 60 */
        std::printf("  信息  D4: 关总开关后 held(60)=%d held(72)=%d "
                    "栈深=%zu\n",
                    (int)ui_is_note_held(&app, 60),
                    (int)ui_is_note_held(&app, 72), app.held_notes.size());
        CHECK(!ui_is_note_held(&app, 60) && !ui_is_note_held(&app, 72) &&
              app.held_notes.empty() && app.current_note == -1,
              "D4: P-14 —— 总开关失效释放走登记值 60 (非 72), 栈空");
        alog_stop();
        ui_shutdown(&app);
    }

    std::printf("\n== 汇总: %d 项检查, %d 项 FAIL ==\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
