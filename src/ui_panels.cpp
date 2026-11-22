/*
 * ui_panels.cpp — Dear ImGui 面板实现
 *
 * 变更摘要 (2026-09-15 TBENCH-FIX-1):
 *   FM Operators 页新增算子包络控件组: 算子选择 (1~6) + eg_mode 切换
 *   (EXT/DX7) + 当前算子 EG 控件 (eg_mode=DX7 时 4R4L 八件
 *   fm.opN.r1~r4/l1~l4; EXT 时 legacy 五参五件 fm.opN.eg_ar/d1r/d1l/
 *   d2r/rr, 随 eg_mode 切换显示)。读写走 kcapi 路径 (ui_set_param),
 *   写入立即生效 (TBENCH-FIX-1 前这些名字被 kernel_capi 静默丢弃)。
 * 变更摘要 (2026-09-16 TBENCH-LOG-WIRE):
 *   引擎切换调用点换线: panel_engine 改走 alog_engine (记 op=engine
 *   日志; alog_engine 内含 kcapi_set_param), cache 刷新与动作录制
 *   保持与 ui_set_param 同语义。
 * 变更摘要 (2026-10-07 TBENCH-FIX-3):
 *   P-07: 三 VCO 面板的 vco_mix 滑条写名由不存在的裸名
 *   "voice.vco_mix" 改为按面板 idx 生成 "voice.vco_mix0/1/2"
 *   (与上一行读 s.vco_mix[idx] 同一 idx), 三个滑条分别落地三个
 *   端口; 只改该一处写名生成方式, 布局/面板结构不动。
 * 变更摘要 (2026-10-26 DEFENSE-1):
 *   P-51 UI 显示越界双保险 (S8, 只动显示层, 写入语义不变):
 *   engine_mode 两处 (panel_engine Combo 与 status_bar 裸索引) 显示前
 *   界检查 [0,3], 越界时 Combo 的 current_item 钳 0 并 SameLine 显 "?",
 *   状态栏显 "?"; panel_fm algorithm SliderInt 量程 0..31 扩为
 *   0..(FM_NUM_ALGOS-1)=0..63 (fm.h:127, DX7 33~64 合法不再越程),
 *   显示值越 [0,63] (含 algorithm=0 → algo=-1) 钳显示值并显 "?"。
 *   界检查抽为外部链接辅助 defense1_engine_mode_name /
 *   defense1_algo_slider_prep (本 TU 顶部), 自证件
 *   tests/defense_p51_test.cpp 直链本 TU 辅助, 不实例化 ImGui。
 */

#include "ui_panels.h"
#include "synth_ui.h"
#include "action_log.h"   /* TBENCH-LOG-WIRE: alog_engine */
#include "midi.h"
#include "keyboard.h"
#include "panel_keyboard.h"
#include "seq.h"

#include "imgui.h"
#include "imgui_internal.h" /* ImGuiContext::Windows, 窗口钳制用 */
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "GLFW/glfw3.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace gp1_ui {

/* ---------- 全局状态（面板内部用） ---------- */
static GLFWwindow *g_window = nullptr;

/* 面板默认布局表（1600×1200 基准，全部落在键盘条保留区之上；
 * setup_window 与 Reset Layout 共用此表，单一事实来源） */
struct PanelDefault { const char *name; float x, y, w, h; };
static const PanelDefault kPanelDefaults[] = {
    {"Engine",            10.0f,  10.0f, 300.0f, 180.0f},
    {"VCO 0",            320.0f,  10.0f, 260.0f, 300.0f},
    {"VCO 1",            590.0f,  10.0f, 260.0f, 300.0f},
    {"VCO 2",            860.0f,  10.0f, 260.0f, 300.0f},
    {"VCF",               10.0f, 200.0f, 300.0f, 240.0f},
    {"VCA",               10.0f, 450.0f, 300.0f, 130.0f},
    {"Tools",             10.0f, 590.0f, 300.0f, 250.0f},
    {"Scope",             10.0f, 848.0f, 300.0f, 108.0f},
    {"ENV 0",            320.0f, 320.0f, 260.0f, 360.0f},
    {"ENV 1",            590.0f, 320.0f, 260.0f, 360.0f},
    {"ENV 2",            860.0f, 320.0f, 260.0f, 360.0f},
    {"LFO 0",            320.0f, 690.0f, 260.0f, 270.0f},
    {"LFO 1",            590.0f, 690.0f, 260.0f, 270.0f},
    {"LFO 2",            860.0f, 690.0f, 260.0f, 270.0f},
    {"FM",              1130.0f,  10.0f, 460.0f, 400.0f},
    {"FM Operators",    1130.0f, 420.0f, 460.0f, 280.0f},
    {"Modulation Matrix",1130.0f, 710.0f, 460.0f, 245.0f},
    {"Seq",              320.0f, 660.0f, 560.0f, 300.0f},
};
static const int kPanelDefaultCount = (int)(sizeof(kPanelDefaults) / sizeof(kPanelDefaults[0]));

static const PanelDefault *find_panel_default(const char *name)
{
    for (int i = 0; i < kPanelDefaultCount; i++) {
        if (std::strcmp(kPanelDefaults[i].name, name) == 0)
            return &kPanelDefaults[i];
    }
    return nullptr;
}

/* 设置面板初始位置与大小（FirstUseEver 允许用户后续拖动） */
static void setup_window(const char *name)
{
    const PanelDefault *d = find_panel_default(name);
    if (!d) return;
    ImGui::SetNextWindowPos(ImVec2(d->x, d->y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(d->w, d->h), ImGuiCond_FirstUseEver);
}

/* Reset Layout：把所有面板强制回默认坐标（Tools 面板按钮触发） */
static void reset_layout()
{
    for (int i = 0; i < kPanelDefaultCount; i++) {
        const PanelDefault *d = &kPanelDefaults[i];
        ImGui::SetWindowPos(d->name, ImVec2(d->x, d->y), ImGuiCond_Always);
        ImGui::SetWindowSize(d->name, ImVec2(d->w, d->h), ImGuiCond_Always);
    }
}

/* 防拖入：每帧把越界的面板钳回键盘条保留区之上（Keyboard/StatusBar 豁免） */
static void clamp_panels_to_workarea(AppState *app)
{
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    ImGuiViewport *vp = ImGui::GetMainViewport();
    const float limit_y = vp->Pos.y + vp->Size.y - ui_bottom_reserve(app);
    for (ImGuiWindow *w : ctx->Windows) {
        if (!w || !w->Active || w->Hidden)
            continue;
        if (std::strcmp(w->Name, "Keyboard") == 0 || std::strcmp(w->Name, "StatusBar") == 0)
            continue;
        float max_y = limit_y - w->Size.y;
        if (max_y < vp->Pos.y)
            max_y = vp->Pos.y; /* 窗口比可用区还高时钉在顶部 */
        if (w->Pos.y > max_y)
            ImGui::SetWindowPos(w, ImVec2(w->Pos.x, max_y), ImGuiCond_Always);
    }
}

/* ---------- 初始化 / 关闭 ---------- */
bool ui_panels_init(GLFWwindow *window)
{
    if (!window)
        return false;
    g_window = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* 布局版本化 ini：旧版（无键盘条时代）坐标自然失效 */
    io.IniFilename = "imgui_layout_v2.ini";

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    return true;
}

void ui_panels_shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

static const char *engine_mode_names[] = {"SUB_ONLY", "FM_ONLY", "SERIES", "PARALLEL"};

/* DEFENSE-1: P-51 显示越界界检查辅助 (外部链接, 自证件
 * tests/defense_p51_test.cpp 直链本 TU 这两个辅助, 不实例化 ImGui)。
 * 纯显示层口径, 不改任何 ui_set_param 写入语义。 */
const char *defense1_engine_mode_name(float engine_mode)
{
    int idx = (int)engine_mode;
    if (idx < 0 || idx >= (int)(sizeof(engine_mode_names) / sizeof(engine_mode_names[0])))
        return nullptr; /* 越界: 调用侧显 "?", 不索引名字数组 */
    return engine_mode_names[idx];
}

bool defense1_algo_slider_prep(float algorithm, int *display_algo)
{
    int algo = (int)algorithm - 1;
    const bool in_range = (algo >= 0 && algo <= FM_NUM_ALGOS - 1);
    if (!in_range)
        algo = (algo < 0) ? 0 : FM_NUM_ALGOS - 1; /* 钳显示值供 Slider */
    if (display_algo)
        *display_algo = algo;
    return in_range;
}
static const char *vco_wave_names[] = {
    "sine", "square", "pulse", "triangle", "sawtooth", "parabolic",
    "resonant_pulse", "slope", "negative_slope", "partial", "shaped",
    "vocal", "sawsquare", "white_noise", "pink_noise", "red_noise", "blue_noise"
};
static const char *lfo_wave_names[] = {
    "sine", "triangle", "square", "sawtooth", "ramp", "reverse_ramp",
    "rectangle_unipolar", "rectangle_bipolar", "random", "smoothed_random",
    "sample_and_hold", "smoothed_sample_and_hold", "rounded_square", "heartbeat"
};
static const char *vcf_mode_names[] = {"LADDER", "SVF"};
static const char *svf_type_names[] = {"LP", "BP", "HP", "NOTCH"};
static const char *vca_curve_names[] = {"LINEAR", "EXP"};
static const char *env_mode_names[] = {"ADHSR", "BREAKPOINT", "BEZIER"};
static const char *env_curve_names[] = {"LINEAR", "EXP"};
static const char *fm_lfo_wave_names[] = {"SAW", "TRIANGLE", "SQUARE", "S&H"};

/* ---------- 引擎面板 ---------- */
static void panel_engine(AppState *app)
{
    setup_window("Engine");
    ImGui::Begin("Engine");

    UiSnapshot &s = app->snap_cache;
    // DEFENSE-1: 快照 float 无界检, 越界时 Combo current_item 钳 0 并显 "?"
    const bool mode_in_range = (defense1_engine_mode_name(s.engine_mode) != nullptr);
    int mode = mode_in_range ? (int)s.engine_mode : 0;
    if (ImGui::Combo("engine_mode", &mode, engine_mode_names, 4)) {
        /* TBENCH-LOG-WIRE: 引擎切换记 op=engine 日志 (alog_engine 内含
         * kcapi_set_param); cache 刷新与动作录制与 ui_set_param 同语义 */
        alog_engine(app->synth, (int)s.engine_mode, mode, "user");
        ui_refresh_cache(app);
        if (app->recording) {
            ActionLogEntry e{(uint32_t)0, 2, "voice.engine_mode", "", (float)mode, 0.0f};
            app->action_log.push_back(e);
        }
    }
    if (!mode_in_range) { ImGui::SameLine(); ImGui::Text("?"); } /* DEFENSE-1 */

    ImGui::SliderFloat("master_level", &app->master_level, 0.0f, 2.0f);
    ImGui::Checkbox("soft_clip_master", &app->soft_clip_master);
    /* TESTBENCH-P3-3: 按键时把 3 个 LFO 相位归零 (默认关) */
    ImGui::Checkbox("Retrig LFOs on note", &app->retrig_lfos_on_note);

    if (s.engine_mode == KCAPI_ENGINE_PARALLEL) {
        float sub_gain = s.sub_gain;
        float fm_gain = s.fm_gain;
        if (ImGui::SliderFloat("sub_gain", &sub_gain, 0.0f, 2.0f)) ui_set_param(app, "voice.sub_gain", sub_gain);
        if (ImGui::SliderFloat("fm_gain", &fm_gain, 0.0f, 2.0f)) ui_set_param(app, "voice.fm_gain", fm_gain);
    }

    if (ImGui::Button("PANIC (all notes off)")) {
        ui_note_off(app);
    }
    ImGui::End();
}

/* ---------- VCO 面板 ---------- */
static void panel_vco(AppState *app, int idx)
{
    char title[32];
    std::snprintf(title, sizeof(title), "VCO %d", idx);
    setup_window(title);
    ImGui::Begin(title);

    UiSnapshot &s = app->snap_cache;
    const VcoParams &v = s.vco[idx];

    char key[64];
    auto mk = [&](const char *suffix) { std::snprintf(key, sizeof(key), "vco%d.%s", idx, suffix); return key; };

    int waveform = v.waveform;
    if (ImGui::Combo("waveform", &waveform, vco_wave_names, VCO_NUM_WAVEFORMS)) {
        ui_set_param(app, mk("waveform"), (float)waveform);
    }

    float freq = v.frequency;
    if (ImGui::SliderFloat("frequency", &freq, 20.0f, 8000.0f, "%.1f Hz")) ui_set_param(app, mk("frequency"), freq);

    float fine = v.fine_tune_cents;
    if (ImGui::SliderFloat("fine_tune", &fine, -50.0f, 50.0f, "%.1f cents")) ui_set_param(app, mk("fine_tune"), fine);

    float pw = v.pulse_width;
    if (ImGui::SliderFloat("pulse_width", &pw, 0.05f, 0.95f)) ui_set_param(app, mk("pulse_width"), pw);

    float lvl = v.level;
    if (ImGui::SliderFloat("level", &lvl, 0.0f, 1.0f)) ui_set_param(app, mk("level"), lvl);

    float mix = s.vco_mix[idx];
    // TBENCH-FIX-3: P-07 写名按 idx 生成 voice.vco_mix0/1/2 (与上一行
    // 读 s.vco_mix[idx] 同一 idx); 原裸名 "voice.vco_mix" 端口表不存在,
    // kcapi 兜底失败, 三个滑条写入永不落地
    char mix_name[32];
    std::snprintf(mix_name, sizeof(mix_name), "voice.vco_mix%d", idx);
    if (ImGui::SliderFloat("vco_mix", &mix, 0.0f, 1.0f)) ui_set_param(app, mix_name, mix);

    int sync = v.sync_enabled;
    if (ImGui::Checkbox("sync_enabled", (bool *)&sync)) ui_set_param(app, mk("sync_enabled"), (float)sync);

    int morph = v.morph_mode;
    const char *morph_names[] = {"SMOOTH", "STEPPED"};
    if (ImGui::Combo("morph_mode", &morph, morph_names, 2)) ui_set_param(app, mk("morph_mode"), (float)morph);

    ImGui::End();
}

/* ---------- VCF 面板 ---------- */
static void panel_vcf(AppState *app)
{
    setup_window("VCF");
    ImGui::Begin("VCF");
    UiSnapshot &s = app->snap_cache;
    const VcfParams &v = s.vcf;

    int mode = v.mode;
    if (ImGui::Combo("mode", &mode, vcf_mode_names, 2)) ui_set_param(app, "vcf.mode", (float)mode);

    int svf_type = v.svf_type;
    if (ImGui::Combo("svf_type", &svf_type, svf_type_names, 4)) ui_set_param(app, "vcf.svf_type", (float)svf_type);

    float cutoff = v.cutoff;
    if (ImGui::SliderFloat("cutoff", &cutoff, 20.0f, 20000.0f, "%.1f Hz")) ui_set_param(app, "vcf.cutoff", cutoff);

    float res = v.resonance;
    if (ImGui::SliderFloat("resonance", &res, 0.0f, 1.0f)) ui_set_param(app, "vcf.resonance", res);

    float drive = v.drive_db;
    if (ImGui::SliderFloat("drive_dB", &drive, -24.0f, 24.0f)) ui_set_param(app, "vcf.drive", drive);

    int soft_clip = v.soft_clip_on;
    if (ImGui::Checkbox("soft_clip", (bool *)&soft_clip)) ui_set_param(app, "vcf.soft_clip", (float)soft_clip);

    int drive_comp = v.drive_comp;
    if (ImGui::Checkbox("drive_comp", (bool *)&drive_comp)) ui_set_param(app, "vcf.drive_comp", (float)drive_comp);

    int smooth = v.smooth_on;
    if (ImGui::Checkbox("smoothing", (bool *)&smooth)) ui_set_param(app, "vcf.smoothing", (float)smooth);

    int os = v.oversampling;
    const char *os_names[] = {"1x", "2x"};
    if (ImGui::Combo("oversampling", &os, os_names, 2)) ui_set_param(app, "vcf.oversampling", (float)os);

    ImGui::End();
}

/* ---------- VCA 面板 ---------- */
static void panel_vca(AppState *app)
{
    setup_window("VCA");
    ImGui::Begin("VCA");
    UiSnapshot &s = app->snap_cache;
    const VcaParams &v = s.vca;

    float lvl = v.level;
    if (ImGui::SliderFloat("level", &lvl, 0.0f, 1.0f)) ui_set_param(app, "vca.level", lvl);

    float drive = v.drive;
    if (ImGui::SliderFloat("drive", &drive, 0.5f, 8.0f)) ui_set_param(app, "vca.drive", drive);

    int curve = (int)v.curve;
    if (ImGui::Combo("curve", &curve, vca_curve_names, 2)) ui_set_param(app, "vca.curve", (float)curve);

    float smooth = v.smooth_ms;
    if (ImGui::SliderFloat("smooth_ms", &smooth, 0.0f, 50.0f)) ui_set_param(app, "vca.smooth_ms", smooth);

    ImGui::End();
}

/* ---------- ENV 面板 ---------- */
/* TESTBENCH-P3-1: ENV 包络形状实时图 (ImDrawList 自绘)。
 * 曲线公式如实复刻内核 env.c 烘焙数学 (见 env.h 头部注释):
 *   shape_rise(u, EXP) = (1-exp(-4.605170u)) / 0.99, LINEAR 为 u;
 *   release 恒指数: y = y0 * exp(-6.907755 * u), u = t/T;
 *   贝塞尔控制点: 自动手柄 ±段距/3、dy=0, 控制点 x 钳入段内。
 * 时间轴压缩: w = log10(1 + t*1000) —— 0.001s 得 0.30, 10s 得 4.0,
 * 保证 1ms 与 10s 段同屏可读 (ADHSR 按段宽正比于压缩后时长分配,
 * sustain 平台固定像素宽; BK/BEZ 用压缩后的绝对时间轴)。 */
static float env_shape_rise_ui(float u, int curve)
{
    if (curve == ENV_CURVE_EXP)
        return (1.0f - expf(-4.605170f * u)) / 0.99f;
    return u;
}

static float env_time_warp(float t)
{
    return log10f(1.0f + t * 1000.0f);
}

static void draw_env_graph(AppState *app, int idx)
{
    const UiSnapshot &s = app->snap_cache;
    const EnvShapeParams &sh = s.env_shape[idx];
    const EnvVoiceParams &vo = s.env_voice[idx];

    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 org = ImGui::GetCursorScreenPos();
    float W = ImGui::GetContentRegionAvail().x;
    const float H = 90.0f;
    if (W < 40.0f) W = 40.0f;
    const float pad = 4.0f;
    const float y_top = org.y + pad;
    const float y_bot = org.y + H - pad;
    const float x_left = org.x + pad;
    const float x_right = org.x + W - pad;
    const float cw = x_right - x_left;

    dl->AddRect(org, ImVec2(org.x + W, org.y + H), IM_COL32(80, 80, 80, 255));

    const ImU32 col_line = IM_COL32(120, 220, 130, 255);
    const ImU32 col_dot  = IM_COL32(255, 220, 80, 255);
    const ImU32 col_node = IM_COL32(200, 200, 200, 255);

    ImVector<ImVec2> pts;
    /* 亮点位置 (仅 PLAYING/SUSTAIN/RELEASE/PREDELAY/LOOPING 时显示) */
    float dot_x = -1.0f, dot_y = -1.0f;
    float rt_frac = 0.0f, rt_dur = 0.0f;
    int rt_state = app->synth ? kcapi_get_env_runtime(app->synth, idx, &rt_frac, &rt_dur)
                              : 0 /* ENV_IDLE */;
    float rt_val = app->synth ? kcapi_get_env_output(app->synth, idx) : 0.0f;

    /* y 电平 -> 屏幕 y (0..1 映射到底..顶) */
    auto sy = [&](float v) {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        return y_bot - v * (y_bot - y_top);
    };

    if (sh.mode == ENV_MODE_ADHSR) {
        /* 段宽: 压缩时长正比分配, sustain 固定像素 */
        const float sus_px = 28.0f;
        float times[5] = { vo.predelay, sh.attack_time, sh.hold_time,
                           sh.decay_time, sh.release_time };
        float wsum = 0.0f;
        for (int i = 0; i < 5; i++) wsum += env_time_warp(times[i]);
        float scale = (cw - sus_px) / (wsum > 1e-6f ? wsum : 1e-6f);
        float w[5];
        for (int i = 0; i < 5; i++) w[i] = env_time_warp(times[i]) * scale;

        float x = x_left;
        float bx[6]; /* 段界: pd|atk|hold|decay|sustain|release */
        bx[0] = x;
        pts.push_back(ImVec2(x, sy(0.0f)));
        x += w[0]; bx[1] = x;                       /* predelay 平线 (y=0) */
        /* attack 0 -> 1 (attack_curve) */
        for (int k = 1; k <= 16; k++) {
            float u = (float)k / 16.0f;
            pts.push_back(ImVec2(x + u * w[1], sy(env_shape_rise_ui(u, (int)sh.attack_curve))));
        }
        x += w[1]; bx[2] = x;
        pts.push_back(ImVec2(x, sy(1.0f)));         /* hold 平台 */
        x += w[2]; bx[3] = x;
        /* decay 1 -> sustain (decay_curve) */
        for (int k = 1; k <= 16; k++) {
            float u = (float)k / 16.0f;
            float v = 1.0f + (sh.sustain_level - 1.0f) * env_shape_rise_ui(u, (int)sh.decay_curve);
            pts.push_back(ImVec2(x + u * w[3], sy(v)));
        }
        x += w[3]; bx[4] = x;
        pts.push_back(ImVec2(x + sus_px, sy(sh.sustain_level))); /* sustain 平台 */
        bx[5] = x + sus_px;
        x += sus_px;
        /* release 指数收尾: y = sustain * exp(-6.907755 u) */
        for (int k = 1; k <= 16; k++) {
            float u = (float)k / 16.0f;
            pts.push_back(ImVec2(x + u * w[4], sy(sh.sustain_level * expf(-6.907755f * u))));
        }

        /* 亮点: LUT 覆盖 attack..decay (不含 predelay/sustain/release) */
        float ta = sh.attack_time, th = sh.hold_time, td = sh.decay_time;
        if (rt_state == ENV_PLAYING || rt_state == ENV_LOOPING) {
            float t = rt_frac * rt_dur;
            if (t < ta && ta > 0.0f)            dot_x = bx[1] + (t / ta) * w[1];
            else if (t < ta + th && th > 0.0f)  dot_x = bx[2] + ((t - ta) / th) * w[2];
            else if (td > 0.0f)                 dot_x = bx[3] + ((t - ta - th) / td) * w[3];
            dot_y = sy(rt_val);
        } else if (rt_state == ENV_PREDELAY) {
            dot_x = bx[0]; dot_y = sy(rt_val);
        } else if (rt_state == ENV_SUSTAIN) {
            dot_x = bx[4] + sus_px * 0.5f; dot_y = sy(rt_val);
        } else if (rt_state == ENV_RELEASE) {
            dot_x = bx[5]; dot_y = sy(rt_val); /* 松键后沿 release 段起点下滑 */
        }
    } else if (sh.mode == ENV_MODE_BREAKPOINT) {
        float xs[32], ys[32];
        int segc[31];
        int n = kcapi_get_env_bk(app->synth, idx, 32, xs, ys, segc);
        if (n >= 2) {
            /* 节点 x 按 log 压缩定位 (兼顾 1ms/10s 可读), 段内屏幕 x
             * 线性插值: LINEAR 段在屏上保持笔直折线, EXP 弧度仅由
             * shape_rise 产生, 不被时间轴压缩扭曲 */
            float tot = env_time_warp(xs[n - 1]);
            if (tot < 1e-6f) tot = 1e-6f;
            float sxn[32];
            for (int i = 0; i < n; i++)
                sxn[i] = x_left + cw * env_time_warp(xs[i]) / tot;
            pts.push_back(ImVec2(sxn[0], sy(ys[0])));
            for (int i = 0; i < n - 1; i++) {
                for (int k = 1; k <= 16; k++) {
                    float u = (float)k / 16.0f;
                    float v = ys[i] + (ys[i + 1] - ys[i]) * env_shape_rise_ui(u, segc[i]);
                    pts.push_back(ImVec2(sxn[i] + u * (sxn[i + 1] - sxn[i]), sy(v)));
                }
            }
            for (int i = 0; i < n; i++)
                dl->AddCircleFilled(ImVec2(sxn[i], sy(ys[i])), 2.5f, col_node);
            if (rt_state == ENV_PLAYING || rt_state == ENV_LOOPING) {
                float t = rt_frac * rt_dur;
                int i = 0;
                while (i < n - 2 && t > xs[i + 1]) i++;
                float span = xs[i + 1] - xs[i];
                float f = (span > 1e-6f) ? (t - xs[i]) / span : 0.0f;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                dot_x = sxn[i] + f * (sxn[i + 1] - sxn[i]); dot_y = sy(rt_val);
            } else if (rt_state == ENV_SUSTAIN) {
                int sp = sh.sustain_point_index;
                dot_x = sxn[(sp >= 0 && sp < n) ? sp : n - 1]; dot_y = sy(rt_val);
            } else if (rt_state == ENV_RELEASE) {
                dot_x = sxn[n - 1]; dot_y = sy(rt_val);
            } else if (rt_state == ENV_PREDELAY) {
                dot_x = sxn[0]; dot_y = sy(rt_val);
            }
        }
    } else { /* ENV_MODE_BEZIER */
        float xs[8], ys[8], hlx[8], hly[8], hrx[8], hry[8];
        int al[8], ar[8];
        int n = kcapi_get_env_bez(app->synth, idx, 8, xs, ys, hlx, hly, hrx, hry, al, ar);
        if (n >= 2) {
            float tot = env_time_warp(xs[n - 1]);
            if (tot < 1e-6f) tot = 1e-6f;
            float sxn[8];
            for (int i = 0; i < n; i++)
                sxn[i] = x_left + cw * env_time_warp(xs[i]) / tot;
            for (int i = 0; i < n - 1; i++) {
                /* 控制点规则与内核 bake_bezier 一致: 自动手柄 ±span/3,
                 * dy=0; 控制点 x 钳入段内保证时间轴单调 */
                float span = xs[i + 1] - xs[i];
                if (span < 1e-6f) span = 1e-6f;
                float r_x = ar[i] ? span / 3.0f : hrx[i];
                float r_y = ar[i] ? 0.0f : hry[i];
                float l_x = al[i + 1] ? -span / 3.0f : hlx[i + 1];
                float l_y = al[i + 1] ? 0.0f : hly[i + 1];
                float p1x = xs[i] + r_x, p1y = ys[i] + r_y;
                float p2x = xs[i + 1] + l_x, p2y = ys[i + 1] + l_y;
                if (p1x < xs[i]) p1x = xs[i];
                if (p1x > xs[i + 1]) p1x = xs[i + 1];
                if (p2x < xs[i]) p2x = xs[i];
                if (p2x > xs[i + 1]) p2x = xs[i + 1];
                if (i == 0)
                    pts.push_back(ImVec2(sxn[0], sy(ys[0])));
                for (int k = 1; k <= 24; k++) {
                    float u = (float)k / 24.0f;
                    float v = 1.0f - u;
                    float bx = v*v*v*xs[i]   + 3.0f*v*v*u*p1x + 3.0f*v*u*u*p2x + u*u*u*xs[i+1];
                    float by = v*v*v*ys[i]   + 3.0f*v*v*u*p1y + 3.0f*v*u*u*p2y + u*u*u*ys[i+1];
                    /* 段内真实时间位置 -> 段内屏幕线性插值 (同 BK 注释) */
                    float f = (bx - xs[i]) / span;
                    pts.push_back(ImVec2(sxn[i] + f * (sxn[i + 1] - sxn[i]), sy(by)));
                }
            }
            for (int i = 0; i < n; i++)
                dl->AddCircleFilled(ImVec2(sxn[i], sy(ys[i])), 2.5f, col_node);
            if (rt_state == ENV_PLAYING || rt_state == ENV_LOOPING) {
                float t = rt_frac * rt_dur;
                int i = 0;
                while (i < n - 2 && t > xs[i + 1]) i++;
                float span = xs[i + 1] - xs[i];
                float f = (span > 1e-6f) ? (t - xs[i]) / span : 0.0f;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                dot_x = sxn[i] + f * (sxn[i + 1] - sxn[i]); dot_y = sy(rt_val);
            } else if (rt_state == ENV_SUSTAIN) {
                int sp = sh.sustain_point_index;
                dot_x = sxn[(sp >= 0 && sp < n) ? sp : n - 1]; dot_y = sy(rt_val);
            } else if (rt_state == ENV_RELEASE) {
                dot_x = sxn[n - 1]; dot_y = sy(rt_val);
            } else if (rt_state == ENV_PREDELAY) {
                dot_x = sxn[0]; dot_y = sy(rt_val);
            }
        }
    }

    if (pts.Size >= 2)
        dl->AddPolyline(pts.Data, pts.Size, col_line, 0, 1.5f);
    if (dot_x >= 0.0f)
        dl->AddCircleFilled(ImVec2(dot_x, dot_y), 3.5f, col_dot);

    ImGui::Dummy(ImVec2(W, H));
}

static void panel_env(AppState *app, int idx)
{
    char title[32];
    std::snprintf(title, sizeof(title), "ENV %d", idx);
    setup_window(title);
    ImGui::Begin(title);

    UiSnapshot &s = app->snap_cache;
    const EnvShapeParams &sh = s.env_shape[idx];
    const EnvVoiceParams &vo = s.env_voice[idx];

    /* TESTBENCH-P3-1: 顶部包络形状实时图 (数据源 = snap_cache,
     * 参数拖动时 ui_set_param 刷新缓存即重绘) */
    draw_env_graph(app, idx);

    char key[64];
    auto mk = [&](const char *suffix) { std::snprintf(key, sizeof(key), "env%d.%s", idx, suffix); return key; };

    int mode = (int)sh.mode;
    if (ImGui::Combo("mode", &mode, env_mode_names, 3)) ui_set_param(app, mk("mode"), (float)mode);

    float predelay = vo.predelay;
    if (ImGui::SliderFloat("predelay", &predelay, 0.0f, 2.0f, "%.3f s")) ui_set_param(app, mk("predelay"), predelay);

    float attack = sh.attack_time;
    if (ImGui::SliderFloat("attack", &attack, 0.0001f, 10.0f, "%.4f s")) ui_set_param(app, mk("attack"), attack);

    float hold = sh.hold_time;
    if (ImGui::SliderFloat("hold", &hold, 0.0f, 10.0f, "%.3f s")) ui_set_param(app, mk("hold"), hold);

    float decay = sh.decay_time;
    if (ImGui::SliderFloat("decay", &decay, 0.001f, 10.0f, "%.3f s")) ui_set_param(app, mk("decay"), decay);

    float sustain = sh.sustain_level;
    if (ImGui::SliderFloat("sustain", &sustain, 0.0f, 1.0f)) ui_set_param(app, mk("sustain"), sustain);

    float release = sh.release_time;
    if (ImGui::SliderFloat("release", &release, 0.001f, 30.0f, "%.3f s")) ui_set_param(app, mk("release"), release);

    int ac = (int)sh.attack_curve;
    if (ImGui::Combo("attack_curve", &ac, env_curve_names, 2)) ui_set_param(app, mk("attack_curve"), (float)ac);

    int dc = (int)sh.decay_curve;
    if (ImGui::Combo("decay_curve", &dc, env_curve_names, 2)) ui_set_param(app, mk("decay_curve"), (float)dc);

    if (ImGui::Button("Retrigger")) {
        ui_note_off(app);
        ui_note_on(app, app->current_note >= 0 ? app->current_note : 60, 100);
    }

    ImGui::End();
}

/* ---------- LFO 面板 ---------- */
static void panel_lfo(AppState *app, int idx)
{
    char title[32];
    std::snprintf(title, sizeof(title), "LFO %d", idx);
    setup_window(title);
    ImGui::Begin(title);

    UiSnapshot &s = app->snap_cache;
    const LfoParams &v = s.lfo[idx];

    char key[64];
    auto mk = [&](const char *suffix) { std::snprintf(key, sizeof(key), "lfo%d.%s", idx, suffix); return key; };

    int waveform = v.waveform;
    if (ImGui::Combo("waveform", &waveform, lfo_wave_names, LFO_NUM_WAVEFORMS)) ui_set_param(app, mk("waveform"), (float)waveform);

    float freq = v.frequency;
    if (ImGui::SliderFloat("frequency", &freq, 0.01f, 20.0f, "%.2f Hz")) ui_set_param(app, mk("frequency"), freq);

    float depth = v.depth;
    if (ImGui::SliderFloat("depth", &depth, 0.0f, 1.0f)) ui_set_param(app, mk("depth"), depth);

    float offset = v.offset;
    if (ImGui::SliderFloat("offset", &offset, -1.0f, 1.0f)) ui_set_param(app, mk("offset"), offset);

    float fade = v.fade_time;
    if (ImGui::SliderFloat("fade_time", &fade, 0.0f, 10.0f, "%.2f s")) ui_set_param(app, mk("fade_time"), fade);

    int sync = kcapi_get_lfo_sync_mode(app->synth, idx);
    const char *sync_names[] = {"FREE_RUN", "KEY_SYNC"};
    if (ImGui::Combo("sync_mode", &sync, sync_names, 2)) ui_set_param(app, mk("sync_mode"), (float)sync);

    if (ImGui::Button("set_phase 0")) {
        kcapi_lfo_set_phase(app->synth, idx, 0.0f);
    }
    ImGui::SameLine();
    if (ImGui::Button("fade_release")) {
        kcapi_lfo_fade_release(app->synth, idx);
    }

    ImGui::End();
}

/* ---------- FM 面板 ---------- */
static void panel_fm(AppState *app)
{
    setup_window("FM");
    ImGui::Begin("FM");
    UiSnapshot &s = app->snap_cache;
    const FmParams &fm = s.fm;

    // DEFENSE-1: 量程 0..31 → 0..(FM_NUM_ALGOS-1)=0..63 (fm.h:127, DX7
    // 33~64 合法不再越程); 显示值越 [0,63] (含 algorithm=0 → algo=-1)
    // 钳显示值并显 "?", 写入语义不变
    int algo = 0;
    const bool algo_in_range = defense1_algo_slider_prep((float)fm.algorithm, &algo);
    if (ImGui::SliderInt("algorithm", &algo, 0, FM_NUM_ALGOS - 1)) ui_set_param(app, "fm.algorithm", (float)(algo + 1));
    if (!algo_in_range) { ImGui::SameLine(); ImGui::Text("?"); } /* DEFENSE-1 */

    const char *name = fm_algorithm_name(fm.algorithm);
    ImGui::Text("%s", name ? name : "?");

    float fb = fm.ops[0].feedback; /* global-ish display */
    if (ImGui::SliderFloat("feedback op0", &fb, 0.0f, 0.99f)) ui_set_param(app, "fm.op0.feedback", fb);
    fb = fm.ops[1].feedback;
    if (ImGui::SliderFloat("feedback op1", &fb, 0.0f, 0.99f)) ui_set_param(app, "fm.op1.feedback", fb);

    int porta_on = fm.porta_on;
    if (ImGui::Checkbox("porta_on", (bool *)&porta_on)) ui_set_param(app, "fm.porta_on", (float)porta_on);
    int porta_rate = fm.porta_rate;
    if (ImGui::SliderInt("porta_rate", &porta_rate, 0, 99)) ui_set_param(app, "fm.porta_rate", (float)porta_rate);

    int transpose = fm.transpose;
    if (ImGui::SliderInt("transpose", &transpose, -48, 48)) ui_set_param(app, "fm.transpose", (float)transpose);

    int bend_range = fm.bend_range;
    if (ImGui::SliderInt("pitch_bend_range", &bend_range, 0, 12)) ui_set_param(app, "fm.pitch_bend_range", (float)bend_range);

    /* PEG */
    float peg_ar = fm.peg_ar;
    if (ImGui::SliderFloat("peg_ar", &peg_ar, 0.0f, 99.0f)) ui_set_param(app, "fm.peg_ar", peg_ar);
    float peg_d1r = fm.peg_d1r;
    if (ImGui::SliderFloat("peg_d1r", &peg_d1r, 0.0f, 99.0f)) ui_set_param(app, "fm.peg_d1r", peg_d1r);
    float peg_d1l = fm.peg_d1l;
    if (ImGui::SliderFloat("peg_d1l", &peg_d1l, 0.0f, 99.0f)) ui_set_param(app, "fm.peg_d1l", peg_d1l);
    float peg_d2r = fm.peg_d2r;
    if (ImGui::SliderFloat("peg_d2r", &peg_d2r, 0.0f, 99.0f)) ui_set_param(app, "fm.peg_d2r", peg_d2r);
    float peg_rr = fm.peg_rr;
    if (ImGui::SliderFloat("peg_rr", &peg_rr, 0.0f, 99.0f)) ui_set_param(app, "fm.peg_rr", peg_rr);

    /* FM LFO */
    int lfo_wave = fm.lfo_wave;
    if (ImGui::Combo("lfo_wave", &lfo_wave, fm_lfo_wave_names, 4)) ui_set_param(app, "fm.lfo_wave", (float)lfo_wave);
    int lfo_speed = fm.lfo_speed;
    if (ImGui::SliderInt("lfo_speed", &lfo_speed, 0, 99)) ui_set_param(app, "fm.lfo_speed", (float)lfo_speed);
    int lfo_delay = fm.lfo_delay;
    if (ImGui::SliderInt("lfo_delay", &lfo_delay, 0, 99)) ui_set_param(app, "fm.lfo_delay", (float)lfo_delay);
    int lfo_pmd = fm.lfo_pmd;
    if (ImGui::SliderInt("lfo_pmd", &lfo_pmd, 0, 99)) ui_set_param(app, "fm.lfo_pmd", (float)lfo_pmd);
    int lfo_amd = fm.lfo_amd;
    if (ImGui::SliderInt("lfo_amd", &lfo_amd, 0, 99)) ui_set_param(app, "fm.lfo_amd", (float)lfo_amd);
    int lfo_sync = fm.lfo_sync;
    if (ImGui::Checkbox("lfo_sync", (bool *)&lfo_sync)) ui_set_param(app, "fm.lfo_sync", (float)lfo_sync);

    ImGui::End();
}

/* ---------- FM 算子面板（6 行） ---------- */
static void panel_fm_ops(AppState *app)
{
    setup_window("FM Operators");
    ImGui::Begin("FM Operators");
    UiSnapshot &s = app->snap_cache;

    if (ImGui::BeginTable("fm_ops", 9, ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Op");
        ImGui::TableSetupColumn("Wave");
        ImGui::TableSetupColumn("Coarse");
        ImGui::TableSetupColumn("Fine");
        ImGui::TableSetupColumn("Detune");
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("Tone");
        ImGui::TableSetupColumn("VelSens");
        ImGui::TableSetupColumn("KRS/PMS");
        ImGui::TableHeadersRow();

        for (int op = 0; op < 6; op++) {
            ImGui::TableNextRow();
            const FmOpParams &p = s.fm.ops[op];
            char key[64];
            auto mk = [&](const char *suffix) { std::snprintf(key, sizeof(key), "fm.op%d.%s", op, suffix); return key; };

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("OP%d", op + 1);

            ImGui::TableSetColumnIndex(1);
            int waveform = p.waveform;
            ImGui::PushID(op * 100 + 1);
            if (ImGui::Combo("", &waveform, vco_wave_names, VCO_NUM_WAVEFORMS)) ui_set_param(app, mk("waveform"), (float)waveform);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(2);
            float coarse = p.coarse;
            ImGui::PushID(op * 100 + 2);
            if (ImGui::SliderFloat("", &coarse, 0.0f, 31.0f)) ui_set_param(app, mk("coarse"), coarse);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(3);
            float fine = p.fine;
            ImGui::PushID(op * 100 + 3);
            if (ImGui::SliderFloat("", &fine, 0.0f, 99.0f)) ui_set_param(app, mk("fine"), fine);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(4);
            int detune = p.detune;
            ImGui::PushID(op * 100 + 4);
            if (ImGui::SliderInt("", &detune, -7, 7)) ui_set_param(app, mk("detune"), (float)detune);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(5);
            int level = p.output_level;
            ImGui::PushID(op * 100 + 5);
            if (ImGui::SliderInt("", &level, 0, 99)) ui_set_param(app, mk("output_level"), (float)level);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(6);
            float tone = p.tone_cutoff;
            ImGui::PushID(op * 100 + 6);
            if (ImGui::SliderFloat("", &tone, 0.0f, 20000.0f, "%.0f")) ui_set_param(app, mk("tone"), tone);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(7);
            float vs = p.vel_sens;
            ImGui::PushID(op * 100 + 7);
            if (ImGui::SliderFloat("", &vs, 0.0f, 1.0f)) ui_set_param(app, mk("vel_sens"), vs);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(8);
            int krs = p.krs;
            int pms = p.pms;
            ImGui::PushID(op * 100 + 8);
            if (ImGui::SliderInt("krs", &krs, 0, 7)) ui_set_param(app, mk("krs"), (float)krs);
            ImGui::PopID();
            ImGui::PushID(op * 100 + 9);
            if (ImGui::SliderInt("pms", &pms, 0, 7)) ui_set_param(app, mk("pms"), (float)pms);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    /* TBENCH-FIX-1: 算子包络控件组 —— 算子选择 + eg_mode 切换 +
     * 当前算子 EG (DX7=4R4L 八件 / EXT=legacy 五参五件, 随 eg_mode
     * 切换显示)。读写走 kcapi (ui_set_param), 数据源 snap_cache.fm。 */
    {
        ImGui::SeparatorText("Operator EG (TBENCH-FIX-1)");
        const FmParams &fmp = s.fm;

        /* eg_mode 切换件 (同页): 0=EXT (legacy 五参), 1=DX7 (4R4L) */
        int egm = fmp.eg_mode;
        const char *egm_names[] = { "EXT (legacy 5-参 EG)", "DX7 (4R4L)" };
        if (ImGui::Combo("eg_mode", &egm, egm_names, 2))
            ui_set_param(app, "fm.eg_mode", (float)egm);

        /* 算子选择 (显示 1~6, 内部 0~5) */
        static int sel_op = 0;
        ImGui::SliderInt("operator", &sel_op, 1, 6);
        int op = sel_op - 1;
        const FmOpParams &op_params = fmp.ops[op];
        char key[64];
        auto mkop = [&](const char *suffix) {
            std::snprintf(key, sizeof(key), "fm.op%d.%s", op, suffix);
            return key;
        };

        if (fmp.eg_mode == 1) {
            /* DX7 语义: 4R4L (R=趋向速率 0~99, L=电平目标 0~99, L4=0 衰减完) */
            int r[4], l[4];
            for (int k = 0; k < 4; k++) { r[k] = op_params.dx_r[k]; l[k] = op_params.dx_l[k]; }
            char lbl[8];
            for (int k = 0; k < 4; k++) {
                std::snprintf(lbl, sizeof(lbl), "R%d", k + 1);
                char kk[64];
                std::snprintf(kk, sizeof(kk), "fm.op%d.r%d", op, k + 1);
                if (ImGui::SliderInt(lbl, &r[k], 0, 99)) ui_set_param(app, kk, (float)r[k]);
            }
            for (int k = 0; k < 4; k++) {
                std::snprintf(lbl, sizeof(lbl), "L%d", k + 1);
                char kk[64];
                std::snprintf(kk, sizeof(kk), "fm.op%d.l%d", op, k + 1);
                if (ImGui::SliderInt(lbl, &l[k], 0, 99)) ui_set_param(app, kk, (float)l[k]);
            }
        } else {
            /* EXT 语义: legacy 五参 EG (0~99, rate 99=最快; FM-PEG-FIX 后
             * PEG 的 rate=0 为冻结, 算子 EG 的 rate=0 仍 = 20s 慢速) */
            float ar = op_params.eg_ar, d1r = op_params.eg_d1r,
                  d1l = op_params.eg_d1l, d2r = op_params.eg_d2r,
                  rr = op_params.eg_rr;
            if (ImGui::SliderFloat("AR",  &ar,  0.0f, 99.0f)) ui_set_param(app, mkop("eg_ar"),  ar);
            if (ImGui::SliderFloat("D1R", &d1r, 0.0f, 99.0f)) ui_set_param(app, mkop("eg_d1r"), d1r);
            if (ImGui::SliderFloat("D1L", &d1l, 0.0f, 99.0f)) ui_set_param(app, mkop("eg_d1l"), d1l);
            if (ImGui::SliderFloat("D2R", &d2r, 0.0f, 99.0f)) ui_set_param(app, mkop("eg_d2r"), d2r);
            if (ImGui::SliderFloat("RR",  &rr,  0.0f, 99.0f)) ui_set_param(app, mkop("eg_rr"),  rr);
        }
    }
    ImGui::End();
}

// ---------- Modulation Matrix panel ----------
struct PortList {
    std::vector<const char *> ctrl_src;
    std::vector<const char *> ctrl_dst;
    std::vector<const char *> audio_src;
    std::vector<const char *> audio_dst;
};

static PortList build_port_list()
{
    PortList pl;
    int n = kcapi_num_ports();
    for (int i = 0; i < n; i++) {
        const char *name = kcapi_port_name(i);
        if (!name) continue;
        int kind = kcapi_port_kind(i);
        int adst = kcapi_port_adst(i);
        if (kind == 0) { // VMX_PORT_READ
            pl.ctrl_src.push_back(name);
        } else if (kind == 1 || kind == 2) { // DST_MOD or DST_PARAM
            pl.ctrl_dst.push_back(name);
        } else if (kind == 3) { // AUDIO_BUF
            if (adst == 0)
                pl.audio_src.push_back(name);
            else
                pl.audio_dst.push_back(name);
        }
    }
    return pl;
}

static void draw_port_combo(const char *label, const char **names, int count, int *selected)
{
    if (*selected < 0 || *selected >= count)
        *selected = 0;
    ImGui::Combo(label, selected, names, count);
}

static void panel_matrix(AppState *app)
{
    setup_window("Modulation Matrix");
    ImGui::Begin("Modulation Matrix");

    static PortList pl = build_port_list();

    // Control cables
    ImGui::SeparatorText("Control Cables");
    {
        static int src_sel = 0;
        static int dst_sel = 0;
        static float amount = 0.0f;

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        draw_port_combo("src", pl.ctrl_src.data(), (int)pl.ctrl_src.size(), &src_sel);
        ImGui::SameLine();
        draw_port_combo("dst", pl.ctrl_dst.data(), (int)pl.ctrl_dst.size(), &dst_sel);
        ImGui::SameLine();
        ImGui::SliderFloat("amt", &amount, -1.0f, 1.0f);
        ImGui::PopItemWidth();

        if (ImGui::Button("Connect Ctrl")) {
            ui_cable_ctrl_connect(app, pl.ctrl_src[src_sel], pl.ctrl_dst[dst_sel], amount);
        }
    }

    ImGui::BeginChild("ctrl_list", ImVec2(0, 120), true);
    int nctrl = kcapi_ctrl_cable_count(app->synth);
    for (int i = 0; i < nctrl; i++) {
        int id = 0, active = 0;
        const char *src_name = "?";
        const char *dst_name = "?";
        float amount = 0.0f;
        kcapi_ctrl_cable_info(app->synth, i, &id, &src_name, &dst_name, &amount, &active);
        if (!active) continue;
        ImGui::Text("id=%d %s -> %s %.3f", id, src_name, dst_name, amount);
        ImGui::SameLine();
        char btn[32];
        std::snprintf(btn, sizeof(btn), "X##c%d", id);
        if (ImGui::SmallButton(btn)) ui_cable_disconnect_ctrl(app, id);
    }
    ImGui::EndChild();

    // Audio cables
    ImGui::SeparatorText("Audio Cables");
    {
        static int src_sel = 0;
        static int dst_sel = 0;
        static float amount = 0.0f;

        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
        draw_port_combo("src", pl.audio_src.data(), (int)pl.audio_src.size(), &src_sel);
        ImGui::SameLine();
        draw_port_combo("dst", pl.audio_dst.data(), (int)pl.audio_dst.size(), &dst_sel);
        ImGui::SameLine();
        ImGui::SliderFloat("amt", &amount, -1.0f, 1.0f);
        ImGui::PopItemWidth();

        if (ImGui::Button("Connect Audio")) {
            ui_cable_audio_connect(app, pl.audio_src[src_sel], pl.audio_dst[dst_sel], amount);
        }
    }

    ImGui::BeginChild("audio_list", ImVec2(0, 120), true);
    int active_count = 0;
    int naudio = kcapi_audio_cable_count(app->synth);
    for (int i = 0; i < naudio; i++) {
        int id = 0, active = 0, dormant = 0;
        const char *src_name = "?";
        const char *dst_name = "?";
        float amount = 0.0f;
        kcapi_audio_cable_info(app->synth, i, &id, &src_name, &dst_name, &amount, &active, &dormant);
        if (!active) continue;
        if (!dormant) active_count++;
        const char *st = dormant ? "DORMANT" : "ACTIVE";
        ImGui::Text("id=%d [%s] %s -> %s %.3f", id, st, src_name, dst_name, amount);
        ImGui::SameLine();
        char btn[32];
        std::snprintf(btn, sizeof(btn), "X##a%d", id);
        if (ImGui::SmallButton(btn)) ui_cable_disconnect_audio(app, id);
    }
    ImGui::EndChild();
    ImGui::Text("Active audio cables: %d / 8", active_count);

    ImGui::End();
}

/* ---------- 示波器 ---------- */
static void panel_scope(AppState *app)
{
    setup_window("Scope");
    ImGui::Begin("Scope");

    float samples[SCOPE_SIZE];
    int pos = app->scope_write_pos.load(std::memory_order_relaxed);
    for (int i = 0; i < SCOPE_SIZE; i++) {
        int idx = (pos + i) & (SCOPE_SIZE - 1);
        samples[i] = app->scope_buffer[idx].load(std::memory_order_relaxed);
    }

    ImGui::PlotLines("", samples, SCOPE_SIZE, 0, nullptr, -1.5f, 1.5f, ImVec2(0, 120));
    ImGui::End();
}

/* ---------- 16 步音序器 ---------- */
static void panel_seq(AppState *app)
{
    setup_window("Seq");
    ImGui::Begin("Seq");
    seq_panel(app);
    ImGui::End();
}

/* ---------- Preset / Action / Offline / Status ---------- */
static void panel_tools(AppState *app)
{
    setup_window("Tools");
    ImGui::Begin("Tools");

    /* Preset */
    ImGui::SeparatorText("Preset");
    static char preset_path[256] = "preset.snp";
    ImGui::InputText("path", preset_path, sizeof(preset_path));
    if (ImGui::Button("Save Preset")) ui_save_preset(app, preset_path);
    ImGui::SameLine();
    if (ImGui::Button("Load Preset")) ui_load_preset(app, preset_path);
    ImGui::SameLine();
    if (ImGui::Button("Dump Text")) {
        char txt[256];
        std::snprintf(txt, sizeof(txt), "preset.txt");
        ui_save_preset_text(app, txt);
    }

    /* 动作录制 */
    ImGui::SeparatorText("Action Log");
    if (app->recording) {
        if (ImGui::Button("Stop Record")) ui_stop_record(app);
    } else {
        if (ImGui::Button("Start Record")) ui_start_record(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Replay")) ui_replay_log(app);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) ui_clear_log(app);
    ImGui::Text("Events: %zu", app->action_log.size());

    static char log_path[256] = "action_log.txt";
    ImGui::InputText("log", log_path, sizeof(log_path));
    if (ImGui::Button("Save Log")) ui_save_log(app, log_path);
    ImGui::SameLine();
    if (ImGui::Button("Load Log")) ui_load_log(app, log_path);

    /* 布局 */
    ImGui::SeparatorText("Layout");
    if (ImGui::Button("Reset Layout")) reset_layout();

    /* 离线渲染 */
    ImGui::SeparatorText("Offline Render");
    static float offline_seconds = 2.0f;
    static char wav_path[256] = "offline.wav";
    ImGui::InputFloat("seconds", &offline_seconds, 0.5f, 1.0f);
    ImGui::InputText("wav", wav_path, sizeof(wav_path));
    if (ImGui::Button("Render WAV")) {
        ui_render_offline(app, offline_seconds, wav_path);
    }

    ImGui::End();
}

static void status_bar(AppState *app)
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - UI_STATUSBAR_HEIGHT));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, UI_STATUSBAR_HEIGHT));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("StatusBar", nullptr, flags);

    float load = app->cpu_load_percent.load(std::memory_order_relaxed);
    ImGui::Text("Load: ");
    ImGui::SameLine();
    ImVec4 color = load > 80.0f ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
                                : ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImGui::TextColored(color, "%.1f%%", load);

    ImGui::SameLine();
    // DEFENSE-1: 快照 float 无界检, 越界显 "?" (audit 期 :1036→:1046 漂移落点)
    const char *mode_name = defense1_engine_mode_name(app->snap_cache.engine_mode);
    ImGui::Text("| Mode: %s", mode_name ? mode_name : "?");

    int active_audio = 0;
    int naudio = kcapi_audio_cable_count(app->synth);
    for (int i = 0; i < naudio; i++) {
        int id = 0, active = 0, dormant = 0;
        const char *src = "?"; const char *dst = "?"; float amt = 0.0f;
        kcapi_audio_cable_info(app->synth, i, &id, &src, &dst, &amt, &active, &dormant);
        if (active && !dormant) active_audio++;
    }
    ImGui::SameLine();
    ImGui::Text("| Audio cables: %d/8", active_audio);

    float peak = app->peak_output.load(std::memory_order_relaxed);
    ImGui::SameLine();
    ImGui::Text("| Peak: ");
    ImGui::SameLine();
    ImVec4 peak_color = peak > 1.5f
                            ? ImVec4(1.0f, 0.0f, 0.0f, 1.0f)
                            : (peak > 1.0f ? ImVec4(1.0f, 1.0f, 0.0f, 1.0f)
                                           : ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
    ImGui::TextColored(peak_color, "%.3f", peak);

    /* 最近触发：note 名 + 音高号 + velocity */
    ImGui::SameLine();
    if (app->last_trig_note >= 0) {
        char name[8];
        ui_note_name(app->last_trig_note, name, sizeof(name));
        ImGui::Text("| Last: %s (%d) vel=%d", name, app->last_trig_note, app->last_trig_vel);
    } else {
        ImGui::Text("| Last: -");
    }

    ImGui::End();
}

/* ---------- 主帧 ---------- */
bool ui_panels_frame(AppState *app)
{
    if (!app)
        return false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    /* 键盘/MIDI */
    midi_poll(app);
    keyboard_poll(g_window, app);

    /* 16 步音序器走带 (窗口关闭也照常走) */
    seq_tick(app, glfwGetTime());

    /* 面板 */
    panel_engine(app);
    panel_vco(app, 0);
    panel_vco(app, 1);
    panel_vco(app, 2);
    panel_vcf(app);
    panel_vca(app);
    panel_env(app, 0);
    panel_env(app, 1);
    panel_env(app, 2);
    panel_lfo(app, 0);
    panel_lfo(app, 1);
    panel_lfo(app, 2);
    panel_fm(app);
    panel_fm_ops(app);
    panel_matrix(app);
    panel_scope(app);
    panel_seq(app);
    panel_tools(app);
    panel_keyboard(app);
    status_bar(app);

    /* 全部面板提交后统一钳制：底边不许越过键盘条保留区 */
    clamp_panels_to_workarea(app);

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(g_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(g_window);
    return !glfwWindowShouldClose(g_window);
}

} /* namespace gp1_ui */
