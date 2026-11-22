/*
 * seq.cpp — 16 步音序器实现 (测试台侧, 内核零改动)
 *
 * 每步两个参数: 音高 note (0..127) 与 gate length (0..1,
 * 占该步时长的比例; 0 = 休止)。走带按 UI 帧驱动:
 *   seq_tick(app, now) 每帧调用一次, 内部比较时间戳推进步进。
 * 发声经 ui_note_on / ui_note_off_note (UI 音符栈入口),
 * 键盘连奏/操作日志/动作录制全部沿用既有语义。
 */

#include "seq.h"
#include "synth_ui.h"

#include "imgui.h"

#include <cstdio>

namespace gp1_ui {

#define SEQ_STEPS 16

struct SeqStep { int note; float gate; };

static bool   g_running = false;
static float  g_bpm = 120.0f;
static int    g_cur_step = 0;          /* 0..15, 下一个将触发的步 */
static double g_next_step_time = 0.0;  /* 下一步触发时间戳 */
static double g_off_time = 0.0;        /* 当前保持音的关音时间戳 */
static bool   g_holding = false;       /* 音序器持有的音符是否在栈内 */
static int    g_hold_note = -1;

/* 默认小旋律 (C 小调五声), gate=0 的步为休止 */
static SeqStep g_steps[SEQ_STEPS] = {
    {60, 0.22f}, {60, 0.00f}, {63, 0.22f}, {62, 0.22f},
    {65, 0.22f}, {63, 0.22f}, {60, 0.22f}, {60, 0.00f},
    {58, 0.22f}, {60, 0.22f}, {60, 0.00f}, {63, 0.22f},
    {67, 0.22f}, {65, 0.22f}, {63, 0.22f}, {62, 0.22f},
};

static double seq_step_dur(void) { return 60.0 / (double)g_bpm / 4.0; }

void seq_tick(AppState *app, double now)
{
    if (!app || !app->synth)
        return;

    /* 停止走带: 释放音序器持有的音 (用户键盘按住的不动) */
    if (!g_running) {
        if (g_holding) {
            ui_note_off_note(app, g_hold_note);
            g_holding = false;
        }
        return;
    }

    /* 到期关音 (gate length 短于整步时) */
    if (g_holding && now >= g_off_time) {
        ui_note_off_note(app, g_hold_note);
        g_holding = false;
    }

    /* 推进步进; 16 次护栏防卡顿后追爆栈, 仍落后就重锚到下一拍 */
    int guard = 0;
    while (now >= g_next_step_time && guard++ < SEQ_STEPS) {
        if (g_holding) {
            ui_note_off_note(app, g_hold_note);
            g_holding = false;
        }
        const SeqStep &st = g_steps[g_cur_step];
        if (st.gate > 0.0f) {
            ui_note_on(app, st.note, 100);
            g_holding = true;
            g_hold_note = st.note;
            g_off_time = g_next_step_time + (double)st.gate * seq_step_dur();
        }
        g_next_step_time += seq_step_dur();
        g_cur_step = (g_cur_step + 1) & (SEQ_STEPS - 1);
    }
    if (now >= g_next_step_time)
        g_next_step_time = now + seq_step_dur();
}

void seq_panel(AppState *app)
{
    const bool was_running = g_running;
    if (ImGui::Checkbox("Run", &g_running)) {
        if (g_running && !was_running)
            g_next_step_time = ImGui::GetTime();   /* 立即开走, 从当前步起 */
        if (!g_running && was_running && g_holding && app) {
            ui_note_off_note(app, g_hold_note);
            g_holding = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        g_cur_step = 0;
    ImGui::SameLine();
    ImGui::SliderFloat("BPM", &g_bpm, 30.0f, 300.0f, "%.0f");
    ImGui::SameLine();
    ImGui::Text("step %d/16%s", g_cur_step + 1, g_running ? "" : " (stop)");

    /* 两组 8 步表格: 行 = 音高号 / 音名 / gate length */
    for (int blk = 0; blk < 2; blk++) {
        char tname[16];
        std::snprintf(tname, sizeof(tname), "seq_blk%d", blk);
        if (!ImGui::BeginTable(tname, 9,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
            continue;
        ImGui::TableSetupColumn("##row");
        for (int i = 0; i < 8; i++) {
            char h[8];
            std::snprintf(h, sizeof(h), "%d", blk * 8 + i + 1);
            ImGui::TableSetupColumn(h, ImGuiTableColumnFlags_WidthFixed, 56.0f);
        }
        ImGui::TableHeadersRow();

        for (int row = 0; row < 3; row++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", row == 0 ? "note" : (row == 1 ? "name" : "gate"));
            for (int i = 0; i < 8; i++) {
                const int s = blk * 8 + i;
                ImGui::TableSetColumnIndex(i + 1);
                const bool cur = g_running && s == g_cur_step;
                if (cur)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                           IM_COL32(80, 90, 40, 255));
                ImGui::PushID(s * 10 + row);
                if (row == 0) {
                    ImGui::SetNextItemWidth(52.0f);
                    ImGui::DragInt("##n", &g_steps[s].note, 1.0f, 0, 127, "%d");
                } else if (row == 1) {
                    char nm[8];
                    ui_note_name(g_steps[s].note, nm, sizeof(nm));
                    ImGui::Text("%s", nm);
                } else {
                    ImGui::SetNextItemWidth(52.0f);
                    ImGui::SliderFloat("##g", &g_steps[s].gate, 0.0f, 1.0f, "%.2f");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("gate=0 = rest; notes go through ui_note_on/off (action log)");
}

} /* namespace gp1_ui */
