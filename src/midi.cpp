/*
 * midi.cpp — RtMidi 输入解析
 *
 * 变更摘要 (2026-09-28 LEGATO-FIX-1):
 *   note_on vel=0 与 note_off 0x80 的路由由 ui_note_off (清全栈 +
 *   无条件 note_off, 绕过音符栈) 改为 ui_note_off_note(app, note)
 *   (synth_ui.cpp:132 既有函数, 与 PC 键盘 keyboard.cpp 同一路由):
 *   松开非栈顶键只出栈不发事件, 栈顶替换时 legato 回退到仍按住键。
 *   修复前: 连奏中任意一个 note_off (含 vel=0 的 note_on) 会把
 *   整个音符栈清空并把 gate 拉 0, 第二个音被掐。
 *
 * 变更摘要 (TBENCH-WIN-1, 2026-11-17):
 *   WP2a: RtMidi API 显式选择收进 #ifdef _WIN32 —— Windows 用
 *   WINDOWS_MM (RtMidi 侧编译定义 __WINDOWS_MM__), Mac 保留
 *   MACOSX_CORE 逐字不变。消息解析逻辑零改动。
 */

#include "midi.h"
#include "RtMidi.h"

#include <vector>

namespace gp1_ui {

static RtMidiIn *g_midi_in = nullptr;

bool midi_start(AppState *app)
{
    (void)app;
    if (g_midi_in)
        return true;

    try {
        /* TBENCH-WIN-1 WP2a: Windows MIDI 用多媒体 API (RtMidi 侧编译
         * 定义 __WINDOWS_MM__); Mac 路径原样 */
#ifdef _WIN32
        g_midi_in = new RtMidiIn(RtMidi::WINDOWS_MM, "gamma-p1 testbench");
#else
        g_midi_in = new RtMidiIn(RtMidi::MACOSX_CORE, "gamma-p1 testbench");
#endif
        g_midi_in->ignoreTypes(false, false, false);
        unsigned int n = g_midi_in->getPortCount();
        if (n > 0)
            g_midi_in->openPort(0);
    } catch (RtMidiError &) {
        if (g_midi_in) {
            delete g_midi_in;
            g_midi_in = nullptr;
        }
        return false;
    }
    return true;
}

void midi_stop(AppState *app)
{
    (void)app;
    if (g_midi_in) {
        g_midi_in->closePort();
        delete g_midi_in;
        g_midi_in = nullptr;
    }
}

void midi_poll(AppState *app)
{
    if (!app || !g_midi_in)
        return;

    std::vector<unsigned char> msg;
    double stamp = 0.0;
    while ((stamp = g_midi_in->getMessage(&msg)) >= 0.0) {
        if (msg.empty())
            break;

        unsigned char status = msg[0] & 0xF0;
        unsigned char ch = msg[0] & 0x0F;
        (void)ch;

        if (status == 0x90 && msg.size() >= 3) {
            int note = msg[1];
            int vel = msg[2];
            if (vel > 0)
                ui_note_on(app, note, vel);
            else
                ui_note_off_note(app, note);   /* LEGATO-FIX-1: vel=0 走音符栈路由 */
        } else if (status == 0x80 && msg.size() >= 3) {
            ui_note_off_note(app, msg[1]);     /* LEGATO-FIX-1: 0x80 走音符栈路由 */
        } else if (status == 0xE0 && msg.size() >= 3) {
            int val = (msg[2] << 7) | msg[1];
            float bend = (val - 8192) / 8192.0f;
            ui_pitch_bend(app, bend);
        } else if (status == 0xB0 && msg.size() >= 3) {
            int cc = msg[1];
            int val = msg[2];
            if (cc == 1) {
                ui_mod_wheel(app, val / 127.0f);
            } else if (cc == 11) {
                ui_breath(app, val / 127.0f);
            } else if (cc == 2 || cc == 4 || cc == 74) {
                ui_aftertouch(app, val / 127.0f);
            }
        } else if (status == 0xD0 && msg.size() >= 2) {
            /* P0-MODSRC-2: 通道触后 (0xD0) —— 真实键盘的 aftertouch 主要走
             * 这个状态字节, 此前只接了 CC2/4/74 代理, 真键盘按下无响应 */
            ui_aftertouch(app, msg[1] / 127.0f);
        }

        if (stamp == 0.0)
            break; /* no pending messages */
    }
}

} /* namespace gp1_ui */
