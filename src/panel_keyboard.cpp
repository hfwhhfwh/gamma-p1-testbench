/*
 * panel_keyboard.cpp — 可点击钢琴键盘面板
 *
 * 布局：默认 C2~B4 三个八度（21 白键 + 15 黑键），Octave +/- 以八度为步进
 * 整体平移（C-1 ~ C7 起步，受 MIDI 0..127 限制）。
 *
 * 交互（全部走 ui_note_on / ui_note_off_note，与 MIDI 输入同一条通路，
 * 最终汇入 kcapi_note_on / kcapi_note_off → synth 层演奏 API）：
 *   - 按下琴键 note_on，松开 note_off；velocity 由点击纵坐标线性插值
 *     （键上沿 40 → 下沿 127）；
 *   - 按住拖动滑过琴键 = glissando：进入新键触发新 note_on，旧键 note_off；
 *   - Hold 开启时点击 = 锁定发声，再点一次释放（单声部内核下由 UI 侧
 *     音符栈做回退，符合 V1 演奏层语义）。
 *
 * 显示：发声键高亮（含外部 MIDI / 电脑键盘触发，兼作输入监视器）；
 * 白键按下变深色、黑键按下变亮色；每组 C 标八度号。
 *
 * 变更摘要 (2026-10-07 TBENCH-FIX-3):
 *   P-36: Hide 按钮 / Octave- / Octave+ 三处置 mouse_key=-1 之前,
 *   若 mouse_key>=0 (非 Hold 模式鼠标正按着的键) 先补
 *   ui_note_off_note(app, mouse_key) —— 原行为静默丢弃跟踪, 按着的
 *   音挂住。Hold 模式锁存音符不跟踪在 mouse_key (Hold 分支每帧
 *   置 -1), 不在本修改范围 (设计语义)。
 */

#include "panel_keyboard.h"

#include "imgui.h"
#include "imgui_internal.h" /* BringWindowToDisplayFront */

#include <cmath>
#include <cstdio>

namespace gp1_ui {

/* ---------- 几何 ---------- */
static const int kNumOctaves = 3;
static const int kNumWhite   = 7 * kNumOctaves; /* 21 */
static const int kWhiteSemi[7]  = {0, 2, 4, 5, 7, 9, 11};
/* 黑键：所在八度内的半音偏移 + 其左侧白键在八度内的序号 */
struct BlackKey { int semi; int left_white; };
static const BlackKey kBlackPat[5] = {
    {1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}
};

/* 冷板岩配色 */
static const ImU32 kColWhite      = IM_COL32(206, 212, 224, 255);
static const ImU32 kColWhitePress = IM_COL32(132, 142, 164, 255); /* 白键按下变深 */
static const ImU32 kColWhiteHeld  = IM_COL32(116, 164, 216, 255); /* 发声高亮(蓝) */
static const ImU32 kColBlack      = IM_COL32( 26,  28,  36, 255);
static const ImU32 kColBlackPress = IM_COL32( 96, 146, 224, 255); /* 黑键按下变亮 */
static const ImU32 kColBlackHeld  = IM_COL32( 64, 108, 186, 255);
static const ImU32 kColBorder     = IM_COL32( 12,  13,  16, 255);
static const ImU32 kColLabel      = IM_COL32( 60,  66,  80, 255);

struct KeyRect {
    ImVec2 lo, hi;
    int    note;
};

/* 推子式 Hold 开关（小滑块 toggle） */
static bool toggle_switch(const char *id, bool *v)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = 36.0f, h = 18.0f;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *v = !*v;
        changed = true;
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImU32 track = *v ? IM_COL32(64, 108, 186, 255) : IM_COL32(58, 62, 72, 255);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, h * 0.5f);
    float knob_x = *v ? (p.x + w - h + 2.0f) : (p.x + 2.0f);
    dl->AddCircleFilled(ImVec2(knob_x + (h - 4.0f) * 0.5f, p.y + h * 0.5f),
                        (h - 6.0f) * 0.5f, IM_COL32(226, 230, 238, 255));
    return changed;
}

/* 由点击纵坐标求 velocity：键上沿 40 → 下沿 127，线性插值 */
static int velocity_from_y(float y, const ImVec2 &lo, const ImVec2 &hi)
{
    float t = (y - lo.y) / (hi.y - lo.y);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 40 + (int)std::lround(t * (127.0f - 40.0f));
}

void panel_keyboard(AppState *app)
{
    if (!app)
        return;

    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float kb_h = app->kb_collapsed ? UI_KEYBOARD_COLLAPSED_HEIGHT : UI_KEYBOARD_HEIGHT;
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - UI_STATUSBAR_HEIGHT - kb_h),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, kb_h), ImGuiCond_Always);

    /* 无标题栏通栏（折叠后只剩 36px 控制行）；置顶避免被浮动窗口盖住 */
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("Keyboard", nullptr, flags);
    /* 底部通栏永远置顶：避免其他浮动窗口被点击后盖住琴键区域 */
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    static int  mouse_key = -1; /* 非 Hold 模式下鼠标当前按住的键（glissando 跟踪） */
    static bool hold = false;

    /* ---------- 控制行 ---------- */
    if (ImGui::SmallButton(app->kb_collapsed ? "Show##kb" : "Hide##kb")) {
        app->kb_collapsed = !app->kb_collapsed;
        // TBENCH-FIX-3: P-36 置 -1 前补 off (鼠标按着的音不再挂住)
        if (mouse_key >= 0) ui_note_off_note(app, mouse_key);
        mouse_key = -1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Keyboard");
    ImGui::SameLine();
    if (ImGui::SmallButton("-##oct")) {
        if (app->kb_base_note > 0) app->kb_base_note -= 12;
        // TBENCH-FIX-3: P-36 置 -1 前补 off (鼠标按着的音不再挂住)
        if (mouse_key >= 0) ui_note_off_note(app, mouse_key);
        mouse_key = -1;
    }
    ImGui::SameLine();
    {
        char lo[8], hi[8];
        int hi_note = app->kb_base_note + 35;
        ui_note_name(app->kb_base_note, lo, sizeof(lo));
        ui_note_name(hi_note > 127 ? 127 : hi_note, hi, sizeof(hi));
        ImGui::Text("%s .. %s", lo, hi);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+##oct")) {
        if (app->kb_base_note < 96) app->kb_base_note += 12;
        // TBENCH-FIX-3: P-36 置 -1 前补 off (鼠标按着的音不再挂住)
        if (mouse_key >= 0) ui_note_off_note(app, mouse_key);
        mouse_key = -1;
    }
    ImGui::SameLine();
    ImGui::Text("  Hold");
    ImGui::SameLine();
    toggle_switch("##hold", &hold);
    ImGui::SameLine();
    ImGui::Checkbox("PC Keys (Z/Q rows)", &app->kb_pc_map_enabled);
    ImGui::SameLine();
    {
        /* 电脑键盘映射提示：Z 排 = 中八度（base+12），Q 排 = 高八度 */
        char z_lo[8], m_hi[8], q_lo[8], u_hi[8];
        int zb = app->kb_base_note + 12;
        ui_note_name(zb, z_lo, sizeof(z_lo));
        ui_note_name(zb + 11 > 127 ? 127 : zb + 11, m_hi, sizeof(m_hi));
        ui_note_name(zb + 12 > 127 ? 127 : zb + 12, q_lo, sizeof(q_lo));
        ui_note_name(zb + 23 > 127 ? 127 : zb + 23, u_hi, sizeof(u_hi));
        ImGui::TextDisabled("  PC: Z..M = %s..%s | Q..U = %s..%s", z_lo, m_hi, q_lo, u_hi);
    }

    /* 折叠态：只显示控制行，琴键区不绘不响应 */
    if (app->kb_collapsed) {
        ImGui::End();
        return;
    }

    /* ---------- 琴键几何 ---------- */
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail  = ImGui::GetContentRegionAvail();
    const float white_w = avail.x / (float)kNumWhite;
    const float white_h = avail.y > 40.0f ? avail.y - 4.0f : 40.0f;
    const float black_w = white_w * 0.62f;
    const float black_h = white_h * 0.62f;

    KeyRect whites[kNumWhite];
    KeyRect blacks[5 * kNumOctaves];
    int n_black = 0;

    for (int oct = 0; oct < kNumOctaves; oct++) {
        for (int k = 0; k < 7; k++) {
            int wi = oct * 7 + k;
            whites[wi].note = app->kb_base_note + oct * 12 + kWhiteSemi[k];
            whites[wi].lo = ImVec2(origin.x + white_w * (float)wi, origin.y);
            whites[wi].hi = ImVec2(origin.x + white_w * (float)(wi + 1), origin.y + white_h);
        }
        for (int b = 0; b < 5; b++) {
            float bx = origin.x + white_w * (float)(oct * 7 + kBlackPat[b].left_white + 1)
                       - black_w * 0.5f;
            blacks[n_black].note = app->kb_base_note + oct * 12 + kBlackPat[b].semi;
            blacks[n_black].lo = ImVec2(bx, origin.y);
            blacks[n_black].hi = ImVec2(bx + black_w, origin.y + black_h);
            n_black++;
        }
    }

    /* ---------- 命中检测（黑键在上，先测黑键） ---------- */
    ImVec2 mp = ImGui::GetIO().MousePos;
    int hover_note = -1;
    ImVec2 hover_lo(0.0f, 0.0f), hover_hi(0.0f, 0.0f);
    bool in_keys = ImGui::IsWindowHovered() &&
                   mp.x >= origin.x && mp.x < origin.x + avail.x &&
                   mp.y >= origin.y && mp.y < origin.y + white_h;
    if (in_keys) {
        for (int i = 0; i < n_black; i++) {
            if (mp.x >= blacks[i].lo.x && mp.x < blacks[i].hi.x &&
                mp.y >= blacks[i].lo.y && mp.y < blacks[i].hi.y &&
                blacks[i].note <= 127) {
                hover_note = blacks[i].note;
                hover_lo = blacks[i].lo; hover_hi = blacks[i].hi;
                break;
            }
        }
        if (hover_note < 0) {
            int wi = (int)((mp.x - origin.x) / white_w);
            if (wi >= 0 && wi < kNumWhite && whites[wi].note <= 127) {
                hover_note = whites[wi].note;
                hover_lo = whites[wi].lo; hover_hi = whites[wi].hi;
            }
        }
    }

    /* ---------- 交互 ---------- */
    if (!hold) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_note >= 0) {
            int vel = velocity_from_y(mp.y, hover_lo, hover_hi);
            ui_note_on(app, hover_note, vel);
            mouse_key = hover_note;
        } else if (mouse_key >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (hover_note >= 0 && hover_note != mouse_key) {
                /* glissando：旧键 note_off，新键 note_on */
                int vel = velocity_from_y(mp.y, hover_lo, hover_hi);
                ui_note_off_note(app, mouse_key);
                ui_note_on(app, hover_note, vel);
                mouse_key = hover_note;
            }
        } else if (mouse_key >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ui_note_off_note(app, mouse_key);
            mouse_key = -1;
        }
    } else {
        /* Hold：点击锁定 / 再点释放；不做 glissando */
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_note >= 0) {
            int vel = velocity_from_y(mp.y, hover_lo, hover_hi);
            if (ui_is_note_held(app, hover_note))
                ui_note_off_note(app, hover_note);
            else
                ui_note_on(app, hover_note, vel);
        }
        mouse_key = -1;
    }

    /* ---------- 绘制 ---------- */
    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < kNumWhite; i++) {
        int note = whites[i].note;
        bool valid = note <= 127;
        bool pressed = (note == mouse_key);
        bool held = valid && ui_is_note_held(app, note);
        ImU32 fill = !valid ? IM_COL32(70, 72, 80, 255)
                     : pressed ? kColWhitePress
                     : held ? kColWhiteHeld : kColWhite;
        dl->AddRectFilled(whites[i].lo, whites[i].hi, fill);
        dl->AddRect(whites[i].lo, whites[i].hi, kColBorder);
        /* 每组的 C 标八度号 */
        if (valid && note % 12 == 0) {
            char name[8];
            ui_note_name(note, name, sizeof(name));
            dl->AddText(ImVec2(whites[i].lo.x + 3.0f, whites[i].hi.y - 16.0f),
                        kColLabel, name);
        }
    }
    for (int i = 0; i < n_black; i++) {
        int note = blacks[i].note;
        bool valid = note <= 127;
        bool pressed = (note == mouse_key);
        bool held = valid && ui_is_note_held(app, note);
        ImU32 fill = !valid ? IM_COL32(40, 41, 48, 255)
                     : pressed ? kColBlackPress
                     : held ? kColBlackHeld : kColBlack;
        dl->AddRectFilled(blacks[i].lo, blacks[i].hi, fill);
        dl->AddRect(blacks[i].lo, blacks[i].hi, kColBorder);
    }

    /* 占位，固定内容区高度，防止面板自动收缩 */
    ImGui::Dummy(avail);

    ImGui::End();
}

} /* namespace gp1_ui */
