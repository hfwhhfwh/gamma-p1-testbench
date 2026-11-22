// synth_ui.cpp - UI bridge implementation (C++ side).
//
// 变更摘要 (2026-09-16 TBENCH-LOG-WIRE):
//   操作日志常驻接线: 本文件的 kcapi_* 调用点换为 action_log 的
//   alog_* 包装 (只挂不改; 包装内仍调原 kcapi_* 函数, 返回值/时序
//   语义不变)。ui_set_param / ui_note_on / ui_note_off /
//   ui_note_off_note / ui_cable_ctrl_connect / ui_cable_audio_connect /
//   ui_cable_disconnect_ctrl / ui_cable_disconnect_audio /
//   ui_save_preset / ui_load_preset 全覆盖; ui_replay_log 回放期间
//   日志 source 记 "replay" (g_replaying 静态标志)。
//   注意: ui_cable_set_amount_* 不在 WP1 清单内, 保持原样不记日志。
//
// 变更摘要 (2026-10-26 SNAPSHOT-FIX-1, 测试台侧 WP3):
//   S4 (ui_load_preset): 改为全文件读入 (seekg end -> tellg -> 按实际
//   尺寸 read), 调 kcapi_snapshot_load_legacy 按尺寸分发 V1~V5 (旧版
//   定长 5732 读入会拒旧版/截断 V5); 返回非 0 即失败 —— return false
//   且不记 alog、不 refresh_cache, 现状态不被污染; 成功才走原
//   alog_preset_load 时机。
//   S5 (ui_save_preset): 改调 kcapi_snapshot_save_v5 + kcapi_snapshot_
//   v5_size, 写出文件随之变 5740B V5 (8B 头 + 5732B V4 主体); 写盘
//   成功才 alog_preset_save (记录时机不变), 内核包装失败先行 false。

#include "synth_ui.h"
#include "action_log.h"   /* TBENCH-LOG-WIRE */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace gp1_ui {

/* TBENCH-LOG-WIRE: ui_replay_log 回放期间为 true, 日志 source 记
 * "replay"; 正常操作期间为 false ("user") */
static bool g_replaying = false;

/* TBENCH-LOG-WIRE: 当前操作来源 ("user"/"replay") */
static const char *alog_src(void) { return g_replaying ? "replay" : "user"; }

void ui_init(AppState *app)
{
    if (!app) return;
    kcapi_vco_bank_init();  // one-time fill of bandlimited tables
    app->synth = kcapi_synth_create();
    kcapi_synth_init(app->synth, KCAPI_SAMPLE_RATE);

    // Deterministic seeds for reproducible testing (cast to float to silence
    // implicit-conversion warnings; the kernel reinterprets as uint32_t.)
    kcapi_set_param(app->synth, "vco0.noise_seed", (float)0x12345678u);
    kcapi_set_param(app->synth, "vco1.noise_seed", (float)0x23456789u);
    kcapi_set_param(app->synth, "vco2.noise_seed", (float)0x3456789au);
    kcapi_set_param(app->synth, "lfo0.seed", (float)0x456789abu);
    kcapi_set_param(app->synth, "lfo1.seed", (float)0x56789abcu);
    kcapi_set_param(app->synth, "lfo2.seed", (float)0x6789abcdu);
    kcapi_set_param(app->synth, "fm.seed", (float)0x789abcdeu);

    /* TESTBENCH-P3-2: 测试台默认 patch —— 开机静音。
     * 默认建立控制线 env0.value -> vca.level (amount=1.0):
     * ENV0 空闲时输出 0, 参数桥把 vca.level 拉到 0 => 无声;
     * 点键 env0 触发 => 出声, 松键进入 release 收尾。
     * 走 ui_cable_ctrl_connect 正常入口, 在 Modulation Matrix 面板
     * 可见可删 (删除后恢复 VCA 常鸣)。这只是测试台默认接线,
     * 内核默认行为不变 (内核出厂线表为空)。 */
    ui_cable_ctrl_connect(app, "env0.value", "vca.level", 1.0f);
    /* 注意: "vca.level" 在端口表中有 DST_MOD (VcoMod) 与 DST_PARAM 两个
     * 端口, find_port 命中先出现的 DST_MOD —— 故该线走的是调制口:
     * 增益 = clamp(vca.level + env0.value, 0, 1) (vca.c 加法语义)。
     * vca.level 默认 1.0, 不置底则常鸣与包络叠加失效, 必须显式置 0
     * 才有"开机静音 + 点键发声"; 此后 VCA level 滑块相当于"地板"偏置。 */
    ui_set_param(app, "vca.level", 0.0f);

    ui_refresh_cache(app);
}

void ui_shutdown(AppState *app)
{
    if (!app) return;
    kcapi_synth_destroy(app->synth);
    app->synth = nullptr;
}

void ui_refresh_cache(AppState *app)
{
    if (!app || !app->synth) return;
    for (int i = 0; i < 3; i++) {
        kcapi_get_vco_params(app->synth, i, &app->snap_cache.vco[i]);
        kcapi_get_lfo_params(app->synth, i, &app->snap_cache.lfo[i]);
        kcapi_get_env_voice_params(app->synth, i, &app->snap_cache.env_voice[i]);
        kcapi_get_env_shape_params(app->synth, i, &app->snap_cache.env_shape[i]);
        app->snap_cache.vco_mix[i] = kcapi_get_vco_mix(app->synth, i);
    }
    kcapi_get_vcf_params(app->synth, &app->snap_cache.vcf);
    kcapi_get_vca_params(app->synth, &app->snap_cache.vca);
    kcapi_get_fm_params(app->synth, &app->snap_cache.fm);
    app->snap_cache.engine_mode = kcapi_get_engine_mode(app->synth);
    app->snap_cache.sub_gain = kcapi_get_sub_gain(app->synth);
    app->snap_cache.fm_gain = kcapi_get_fm_gain(app->synth);
}

void ui_note_on(AppState *app, int note, int velocity)
{
    if (!app) return;
    if (note < 0 || note > 127) return;
    /* 音符栈：同键先去重再压栈（栈顶 = 最近按下 = 单声部内核实际发声键） */
    for (auto it = app->held_notes.begin(); it != app->held_notes.end(); ++it) {
        if (it->note == note) { app->held_notes.erase(it); break; }
    }
    HeldNote hn{note, velocity};
    app->held_notes.push_back(hn);
    alog_note_on(app->synth, note, velocity, alog_src());   /* TBENCH-LOG-WIRE */
    /* TESTBENCH-P3-3: Retrig LFOs on note 开启时, 3 个 LFO 相位归零
     * (与内核 sync_mode=KEY_SYNC 效果相同, 但由 UI 开关控制,
     *  不改动内核 LFO 的 sync_mode 参数) */
    if (app->retrig_lfos_on_note) {
        for (int i = 0; i < 3; i++)
            kcapi_lfo_set_phase(app->synth, i, 0.0f);
    }
    app->current_note = note;
    app->last_trig_note = note;
    app->last_trig_vel = velocity;
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 0, "", "", (float)note, (float)velocity};
        app->action_log.push_back(e);
    }
}

void ui_note_off(AppState *app)
{
    if (!app) return;
    app->held_notes.clear();
    alog_note_off(app->synth, alog_src());   /* TBENCH-LOG-WIRE */
    app->current_note = -1;
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 1, "", "", 0.0f, 0.0f};
        app->action_log.push_back(e);
    }
}

void ui_note_off_note(AppState *app, int note)
{
    if (!app) return;
    bool was_top = !app->held_notes.empty() && app->held_notes.back().note == note;
    for (auto it = app->held_notes.begin(); it != app->held_notes.end(); ++it) {
        if (it->note == note) { app->held_notes.erase(it); break; }
    }
    if (app->held_notes.empty()) {
        alog_note_off(app->synth, alog_src());   /* TBENCH-LOG-WIRE */
        app->current_note = -1;
    } else if (was_top) {
        /* 音符栈回退：松开的是发声键时，legato 回到最近一个仍按住的键 */
        const HeldNote &t = app->held_notes.back();
        alog_note_on(app->synth, t.note, t.velocity, alog_src());   /* TBENCH-LOG-WIRE */
        app->current_note = t.note;
    }
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 1, "", "", 0.0f, 0.0f};
        app->action_log.push_back(e);
    }
}

bool ui_is_note_held(const AppState *app, int note)
{
    if (!app) return false;
    for (const HeldNote &h : app->held_notes) {
        if (h.note == note) return true;
    }
    return false;
}

void ui_note_name(int note, char *buf, int buflen)
{
    static const char *names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    if (!buf || buflen <= 0) return;
    if (note < 0 || note > 127) {
        std::snprintf(buf, (size_t)buflen, "?");
        return;
    }
    std::snprintf(buf, (size_t)buflen, "%s%d", names[note % 12], note / 12 - 1);
}

void ui_pitch_bend(AppState *app, float val)
{
    if (app) kcapi_pitch_bend(app->synth, val);
}

void ui_mod_wheel(AppState *app, float val)
{
    if (app) kcapi_mod_wheel(app->synth, val);
}

void ui_aftertouch(AppState *app, float val)
{
    if (app) kcapi_aftertouch(app->synth, val);
}

void ui_breath(AppState *app, float val)
{
    if (app) kcapi_breath(app->synth, val);
}

void ui_set_param(AppState *app, const char *name, float value)
{
    if (!app || !app->synth || !name) return;
    alog_set_param(app->synth, name, value, alog_src());   /* TBENCH-LOG-WIRE */
    ui_refresh_cache(app);
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 2, name, "", value, 0.0f};
        app->action_log.push_back(e);
    }
}

int ui_cable_ctrl_connect(AppState *app, const char *src, const char *dst, float amount)
{
    if (!app || !app->synth) return -1;
    int id = alog_cable_connect(app->synth, 1, src, dst, amount, alog_src());   /* TBENCH-LOG-WIRE */
    ui_refresh_cache(app);
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 3, src ? src : "", dst ? dst : "", amount, (float)id};
        app->action_log.push_back(e);
    }
    return id;
}

int ui_cable_audio_connect(AppState *app, const char *src, const char *dst, float amount)
{
    if (!app || !app->synth) return -1;
    int id = alog_cable_connect(app->synth, 0, src, dst, amount, alog_src());   /* TBENCH-LOG-WIRE */
    ui_refresh_cache(app);
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 4, src ? src : "", dst ? dst : "", amount, (float)id};
        app->action_log.push_back(e);
    }
    return id;
}

int ui_cable_disconnect_ctrl(AppState *app, int id)
{
    if (!app || !app->synth) return -1;
    /* TBENCH-LOG-WIRE: alog_cable_disconnect 无返回值, 此处需保留
     * kcapi 返回码语义, 故直接调原函数 + 用公开 alog_log 记同格式
     * cable- 行 (与 alog_cable_disconnect 内部产出逐字一致) */
    int r = kcapi_cable_disconnect_ctrl(app->synth, id);
    {
        char det[32];
        std::snprintf(det, sizeof(det), "id=%d", id);
        alog_log("cable-", "ctrl", det, alog_src());
    }
    ui_refresh_cache(app);
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 3, "", "", 0.0f, (float)id};
        app->action_log.push_back(e);
    }
    return r;
}

int ui_cable_disconnect_audio(AppState *app, int id)
{
    if (!app || !app->synth) return -1;
    /* TBENCH-LOG-WIRE: 同 ctrl 断线 —— 保留 kcapi 返回码 + alog_log
     * 记同格式 cable- 行 */
    int r = kcapi_cable_disconnect_audio(app->synth, id);
    {
        char det[32];
        std::snprintf(det, sizeof(det), "id=%d", id);
        alog_log("cable-", "audio", det, alog_src());
    }
    ui_refresh_cache(app);
    if (app->recording) {
        ActionLogEntry e{(uint32_t)0, 4, "", "", 0.0f, (float)id};
        app->action_log.push_back(e);
    }
    return r;
}

int ui_cable_set_amount_ctrl(AppState *app, int id, float amount)
{
    if (!app || !app->synth) return -1;
    return kcapi_cable_set_amount_ctrl(app->synth, id, amount);
}

int ui_cable_set_amount_audio(AppState *app, int id, float amount)
{
    if (!app || !app->synth) return -1;
    return kcapi_cable_set_amount_audio(app->synth, id, amount);
}

void ui_start_record(AppState *app)
{
    if (app) app->recording = true;
}

void ui_stop_record(AppState *app)
{
    if (app) app->recording = false;
}

void ui_clear_log(AppState *app)
{
    if (app) app->action_log.clear();
}

bool ui_save_log(const AppState *app, const char *path)
{
    if (!app || !path) return false;
    std::ofstream f(path);
    if (!f.is_open()) return false;
    for (const auto &e : app->action_log) {
        f << e.sample_count << " " << e.op << " "
          << e.name0 << " " << e.name1 << " "
          << e.f0 << " " << e.f1 << "\n";
    }
    return true;
}

bool ui_load_log(AppState *app, const char *path)
{
    if (!app || !path) return false;
    std::ifstream f(path);
    if (!f.is_open()) return false;
    app->action_log.clear();
    ActionLogEntry e;
    while (f >> e.sample_count >> e.op >> e.name0 >> e.name1 >> e.f0 >> e.f1) {
        app->action_log.push_back(e);
    }
    return true;
}

void ui_replay_log(AppState *app)
{
    if (!app || !app->synth) return;
    g_replaying = true;   /* TBENCH-LOG-WIRE: 回放期间日志 source=replay */
    for (const auto &e : app->action_log) {
        switch (e.op) {
        case 0: ui_note_on(app, (int)e.f0, (int)e.f1); break;
        case 1: ui_note_off(app); break;
        case 2: ui_set_param(app, e.name0.c_str(), e.f0); break;
        case 3:
            if (e.name0.empty() && e.name1.empty())
                ui_cable_disconnect_ctrl(app, (int)e.f1);
            else
                ui_cable_ctrl_connect(app, e.name0.c_str(), e.name1.c_str(), e.f0);
            break;
        case 4:
            if (e.name0.empty() && e.name1.empty())
                ui_cable_disconnect_audio(app, (int)e.f1);
            else
                ui_cable_audio_connect(app, e.name0.c_str(), e.name1.c_str(), e.f0);
            break;
        }
    }
    g_replaying = false;   /* TBENCH-LOG-WIRE */
    ui_refresh_cache(app);
}

bool ui_save_preset(const AppState *app, const char *path)
{
    if (!app || !app->synth || !path) return false;
    // SNAPSHOT-FIX-1 S5: 写 V5 (5740B = 8B 头 + 5732B V4 主体)
    size_t sz = kcapi_snapshot_v5_size();
    std::vector<char> buf(sz);
    if (kcapi_snapshot_save_v5(app->synth, buf.data(), buf.size()) != 0)
        return false;
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(buf.data(), (std::streamsize)sz);
    if (f.good())
        alog_preset_save(path, alog_src());   /* TBENCH-LOG-WIRE: 成功才记 */
    return f.good();
}

bool ui_load_preset(AppState *app, const char *path)
{
    if (!app || !app->synth || !path) return false;
    // SNAPSHOT-FIX-1 S4: 全文件读入 (实际尺寸), 按尺寸分发 V1~V5,
    // 不再定长 5732 拒旧版/截断 V5
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    std::streampos end = f.tellg();
    if (end <= 0) return false;
    std::vector<char> buf((size_t)end);
    f.seekg(0, std::ios::beg);
    f.read(buf.data(), end);
    if (!f.good()) return false;
    if (kcapi_snapshot_load_legacy(app->synth, buf.data(), (int)buf.size()) != 0)
        return false;   // SNAPSHOT-FIX-1 S4: 失败不记 alog, 不 refresh, 现状态不动
    ui_refresh_cache(app);
    alog_preset_load(path, alog_src());   /* TBENCH-LOG-WIRE: 成功才记 */
    return true;
}

static void dump_float(std::ostringstream &oss, const char *name, float v)
{
    oss << name << "=" << v << "\n";
}

static void dump_int(std::ostringstream &oss, const char *name, int v)
{
    oss << name << "=" << v << "\n";
}

bool ui_save_preset_text(const AppState *app, const char *path)
{
    if (!app || !path) return false;
    ui_refresh_cache(const_cast<AppState *>(app));
    const UiSnapshot &s = app->snap_cache;
    std::ostringstream oss;

    oss << "# gamma-p1-testbench text preset dump\n";
    dump_int(oss, "engine_mode", s.engine_mode);
    dump_float(oss, "sub_gain", s.sub_gain);
    dump_float(oss, "fm_gain", s.fm_gain);
    for (int i = 0; i < 3; i++) {
        char k[32]; std::snprintf(k, sizeof(k), "vco_mix%d", i);
        dump_float(oss, k, s.vco_mix[i]);
    }

    for (int i = 0; i < 3; i++) {
        oss << "[vco" << i << "]\n";
        dump_float(oss, "frequency", s.vco[i].frequency);
        dump_int(oss, "waveform", s.vco[i].waveform);
        dump_float(oss, "fine_tune", s.vco[i].fine_tune_cents);
        dump_float(oss, "pulse_width", s.vco[i].pulse_width);
        dump_float(oss, "level", s.vco[i].level);
        dump_int(oss, "sync_enabled", s.vco[i].sync_enabled);
        dump_int(oss, "morph_mode", s.vco[i].morph_mode);
    }

    oss << "[vcf]\n";
    dump_int(oss, "mode", s.vcf.mode);
    dump_int(oss, "svf_type", s.vcf.svf_type);
    dump_float(oss, "cutoff", s.vcf.cutoff);
    dump_float(oss, "resonance", s.vcf.resonance);
    dump_float(oss, "drive", s.vcf.drive_db);
    dump_int(oss, "oversampling", s.vcf.oversampling);
    dump_int(oss, "soft_clip", s.vcf.soft_clip_on);
    dump_int(oss, "smoothing", s.vcf.smooth_on);

    oss << "[vca]\n";
    dump_float(oss, "level", s.vca.level);
    dump_float(oss, "drive", s.vca.drive);
    dump_int(oss, "curve", (int)s.vca.curve);
    dump_float(oss, "smooth_ms", s.vca.smooth_ms);

    for (int i = 0; i < 3; i++) {
        oss << "[lfo" << i << "]\n";
        dump_int(oss, "waveform", s.lfo[i].waveform);
        dump_float(oss, "frequency", s.lfo[i].frequency);
        dump_float(oss, "depth", s.lfo[i].depth);
        dump_float(oss, "offset", s.lfo[i].offset);
        dump_float(oss, "fade_time", s.lfo[i].fade_time);
        // sync_mode is runtime state, not part of LfoParams snapshot.
    }

    for (int i = 0; i < 3; i++) {
        oss << "[env" << i << "]\n";
        dump_float(oss, "predelay", s.env_voice[i].predelay);
        dump_int(oss, "mode", (int)s.env_shape[i].mode);
        dump_float(oss, "attack", s.env_shape[i].attack_time);
        dump_float(oss, "hold", s.env_shape[i].hold_time);
        dump_float(oss, "decay", s.env_shape[i].decay_time);
        dump_float(oss, "sustain", s.env_shape[i].sustain_level);
        dump_float(oss, "release", s.env_shape[i].release_time);
    }

    oss << "[fm]\n";
    dump_int(oss, "algorithm", s.fm.algorithm);
    dump_int(oss, "transpose", s.fm.transpose);
    dump_int(oss, "bend_range", s.fm.bend_range);
    dump_int(oss, "porta_on", s.fm.porta_on);
    dump_int(oss, "porta_rate", s.fm.porta_rate);
    for (int i = 0; i < 6; i++) {
        const FmOpParams &op = s.fm.ops[i];
        oss << "[fm.op" << i << "]\n";
        dump_float(oss, "ar", op.eg_ar);
        dump_float(oss, "d1r", op.eg_d1r);
        dump_float(oss, "d1l", op.eg_d1l);
        dump_float(oss, "d2r", op.eg_d2r);
        dump_float(oss, "rr", op.eg_rr);
        dump_int(oss, "output_level", op.output_level);
        dump_float(oss, "coarse", op.coarse);
        dump_float(oss, "fine", op.fine);
        dump_int(oss, "detune", op.detune);
        dump_int(oss, "waveform", op.waveform);
        dump_float(oss, "tone", op.tone_cutoff);
        dump_float(oss, "vel_sens", op.vel_sens);
    }

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << oss.str();
    return true;
}

bool ui_render_offline(AppState *app, float seconds, const char *wav_path)
{
    if (!app || !app->synth || seconds <= 0.0f || !wav_path) return false;
    int n_samples = (int)(seconds * KCAPI_SAMPLE_RATE);
    app->offline_buffer.resize((size_t)n_samples);
    kcapi_render_offline(app->synth, app->offline_buffer.data(), n_samples);
    return write_wav_float32(wav_path, app->offline_buffer.data(), n_samples, (int)KCAPI_SAMPLE_RATE);
}

bool write_wav_float32(const char *path, const float *data, int n_samples, int sample_rate)
{
    if (!path || !data || n_samples <= 0) return false;

    struct WavHeader {
        char riff[4];
        uint32_t file_size;
        char wave[4];
        char fmt[4];
        uint32_t fmt_size;
        uint16_t format;
        uint16_t channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits;
        char data[4];
        uint32_t data_size;
    };

    WavHeader h;
    memcpy(h.riff, "RIFF", 4);
    h.file_size = 36 + (uint32_t)n_samples * sizeof(float);
    memcpy(h.wave, "WAVE", 4);
    memcpy(h.fmt, "fmt ", 4);
    h.fmt_size = 16;
    h.format = 3;
    h.channels = 1;
    h.sample_rate = (uint32_t)sample_rate;
    h.byte_rate = (uint32_t)sample_rate * sizeof(float);
    h.block_align = sizeof(float);
    h.bits = 32;
    memcpy(h.data, "data", 4);
    h.data_size = (uint32_t)n_samples * sizeof(float);

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char *>(&h), sizeof(h));
    f.write(reinterpret_cast<const char *>(data), (size_t)n_samples * sizeof(float));
    return f.good();
}

} // namespace gp1_ui
