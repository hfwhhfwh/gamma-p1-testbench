/*
 * seq_test.cpp — 16 步音序器无头冒烟测试 (手工编译, 不进构建系统,
 * 惯例同 p0_headless_test / p0_modsrc_test)
 *
 * 直 include ../src/seq.cpp 以获得静态走带状态的控制权 (同 TU 访问,
 * 不扩 seq.h 公开接口)。用真实内核实例 + ui_init 默认接线,
 * 以 0.5ms 步长模拟 2.2s 走带, 检测 ui_note_on/off 在 UI 音符栈上
 * 的上升/下降沿, 核对:
 *   S1  note-on 序列 (音符号 + 触发时间, 容差 3ms) 与 16 步默认
 *       旋律完全一致 (含 gate=0 休止步不发声);
 *   S2  step0 (note 60, gate 0.75, 步长 0.125s) 在 ~0.09375s 准时关音;
 *   S3  休止步区间 (step1: 0.125~0.25s) 无保持音;
 *   S4  停止走带后 seq_tick 释放音序器持有的音.
 *
 * 编译 (在 gamma-p1-testbench/ 下):
 *   c++ -O2 -Wall -Wextra -std=c++17 -Wno-unused-parameter \
 *     -Isrc -I../gamma-p1 -I../gamma-p1/vco_c -I../gamma-p1/vcf_a \
 *     -I../gamma-p1/vca -I../gamma-p1/lfo -I../gamma-p1/env -I../gamma-p1/fm \
 *     -Ithird_party/imgui-1.91.0 -Ithird_party/imgui-1.91.0/backends \
 *     tests/seq_test.cpp \
 *     build/obj/{synth,vmatrix,voice,vco,wavetables_data,vcf,vca,lfo,
 *     lfo_wavetables_data,env,fm,snapshot_v1,kernel_capi,action_log,
 *     synth_ui,imgui,imgui_draw,imgui_tables,imgui_widgets,imgui_demo}.o \
 *     -o tests/seq_test -lm
 */

#include "../src/seq.cpp"

#include <cstdio>
#include <cmath>

using namespace gp1_ui;

namespace {

struct Edge { double t; int note; };   /* note=-1 表示下降沿 */

int g_fail = 0;

void check(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

} /* anonymous namespace */

int main()
{
    AppState app;
    ui_init(&app);   /* 真实内核实例 + env0->vca 默认接线 */

    /* 走带初态 (BPM 120 => 16 分音符步长 0.125s) */
    g_bpm = 120.0f;
    g_running = true;
    g_cur_step = 0;
    g_next_step_time = 0.0;
    g_holding = false;

    const double dt = 0.0005;
    std::vector<Edge> edges;
    bool was_held = false;
    for (double t = 0.0; t <= 2.2; t += dt) {
        seq_tick(&app, t);
        const bool held = !app.held_notes.empty();
        if (held && !was_held)
            edges.push_back({t, app.current_note});
        else if (!held && was_held)
            edges.push_back({t, -1});
        was_held = held;
    }

    /* 期望 note-on: (音符号, 触发秒) —— 默认旋律前 14 个发声步 */
    const int    exp_notes[] = {60, 63, 62, 65, 63, 60, 58, 60, 63, 67, 65, 63, 62, 60};
    const double exp_times[] = {0.0, 0.25, 0.375, 0.5, 0.625, 0.75, 1.0, 1.125,
                                1.375, 1.5, 1.625, 1.75, 1.875, 2.0};
    const int n_on = (int)(sizeof(exp_notes) / sizeof(exp_notes[0]));

    std::vector<Edge> ons, offs;
    for (const Edge &e : edges)
        (e.note >= 0 ? ons : offs).push_back(e);

    std::printf("S1 note-on 序列\n");
    check((int)ons.size() == n_on, "note-on 次数 == 14");
    bool seq_ok = ((int)ons.size() == n_on);
    for (int i = 0; seq_ok && i < n_on; i++) {
        if (ons[i].note != exp_notes[i] ||
            std::fabs(ons[i].t - exp_times[i]) > 0.003) {
            std::printf("    step %d: got (n=%d t=%.4f) want (n=%d t=%.3f)\n",
                        i, ons[i].note, ons[i].t, exp_notes[i], exp_times[i]);
            seq_ok = false;
        }
    }
    check(seq_ok, "音符号 + 触发时间逐点一致 (容差 3ms)");

    std::printf("S2 gate length 关音时点\n");
    check(!offs.empty() && std::fabs(offs[0].t - 0.0275) < 0.003,
          "step0 (gate=0.22) 在 ~0.0275s 关音");

    std::printf("S3 休止步 (step1, 0.125~0.25s) 无保持音\n");
    bool rest_quiet = true;
    for (const Edge &e : edges) {
        /* step0 的下降沿 (0.0275) 之后到 step2 的上升沿 (0.25) 之间
         * 不允许有任何上升沿落在休止区间内 */
        if (e.note >= 0 && e.t > 0.05 && e.t < 0.247)
            rest_quiet = false;
    }
    check(rest_quiet, "休止区间无 note-on");

    std::printf("S4 停止走带即释放\n");
    g_running = false;
    seq_tick(&app, 2.2);
    check(app.held_notes.empty(), "停止后音符栈空");

    ui_shutdown(&app);
    std::printf("%s\n", g_fail == 0 ? "== seq_test: ALL PASS ==" : "== seq_test: FAILURES ==");
    return g_fail == 0 ? 0 : 1;
}
