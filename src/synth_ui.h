// synth_ui.h - UI layer bridge: unified parameter entry, action log, snapshot, offline render.
// Kernel access goes through kernel_capi.h (C wrapper); C++ code never includes synth.h.

#ifndef SYNTH_UI_H
#define SYNTH_UI_H

#include "kernel_capi.h"

#include <stdint.h>
#include <vector>
#include <string>
#include <atomic>

// Individual kernel module headers are C++-safe and only declare parameter structs/enums.
#include "vco.h"
#include "vcf.h"
#include "vca.h"
#include "lfo.h"
#include "env.h"
#include "fm.h"

namespace gp1_ui {

constexpr int SCOPE_SIZE = 4096;

/* 底部保留区：状态栏 + 键盘条（展开 210 / 收起 36） */
constexpr float UI_STATUSBAR_HEIGHT = 28.0f;
constexpr float UI_KEYBOARD_HEIGHT = 210.0f;
constexpr float UI_KEYBOARD_COLLAPSED_HEIGHT = 36.0f;

// Action log entry for record/playback
struct ActionLogEntry {
    uint32_t    sample_count;
    int         op;             // SynthLogOp
    std::string name0;
    std::string name1;
    float       f0;
    float       f1;
};

// Snapshot subset used by the UI for read-back.
struct UiSnapshot {
    VcoParams      vco[3];
    VcfParams      vcf;
    VcaParams      vca;
    LfoParams      lfo[3];
    EnvVoiceParams env_voice[3];
    EnvShapeParams env_shape[3];
    FmParams       fm;
    int            engine_mode;
    float          vco_mix[3];
    float          sub_gain;
    float          fm_gain;
};

// One physically-held key (any source: piano panel, PC keyboard, MIDI).
// The kernel is monophonic; the UI keeps a press-ordered stack so releasing
// the sounding note falls back to the most recent still-held note.
struct HeldNote {
    int note;
    int velocity;
};

// App state: kernel instance + UI helpers (heap allowed on UI side)
struct AppState {
    kcapi_synth_t *synth = nullptr;

    // Audio callback shared state (lock-free atomics)
    std::atomic<float> scope_buffer[SCOPE_SIZE];
    std::atomic<int>   scope_write_pos{0};
    std::atomic<float> cpu_load_percent{0.0f};
    std::atomic<float> peak_output{0.0f};

    // MIDI / keyboard
    int  octave_shift = 0;
    bool key_pressed[128] = {};
    int  current_note = -1;

    // Held-note stack (press order, back = most recent = sounding on mono kernel)
    std::vector<HeldNote> held_notes;

    // Keyboard panel state
    int  kb_base_note = 36;        // lowest displayed key (C2), C-aligned, octave +/- steps 12
    bool kb_pc_map_enabled = true; // computer-keyboard tracker mapping master switch
    bool kb_collapsed = false;     // keyboard strip collapsed to control row only

    // Status bar: most recent note_on
    int  last_trig_note = -1;
    int  last_trig_vel  = 0;

    /* TESTBENCH-P3-3: 开启时 ui_note_on 顺带把 3 个 LFO 相位归零
     * (Engine 面板开关, 默认关; 经 kcapi_lfo_set_phase, 内核不变) */
    bool retrig_lfos_on_note = false;

    // Action recording
    std::vector<ActionLogEntry> action_log;
    bool recording = false;

    // Parameter cache for UI read-back
    UiSnapshot snap_cache;

    // Master output processing (UI side)
    float master_level = 1.0f;
    bool  soft_clip_master = false;

    // Offline render
    std::vector<float> offline_buffer;
};

void ui_init(AppState *app);
void ui_shutdown(AppState *app);

/* 底部保留区高度（状态栏 + 键盘条，随键盘折叠联动） */
inline float ui_bottom_reserve(const AppState *app)
{
    return UI_STATUSBAR_HEIGHT +
           ((app && app->kb_collapsed) ? UI_KEYBOARD_COLLAPSED_HEIGHT : UI_KEYBOARD_HEIGHT);
}

// Unified UI parameter entry (action recording hook). Names align with PARAMETERS.txt.
void ui_set_param(AppState *app, const char *name, float value);

// Cable entry points (action recording hooks)
int  ui_cable_ctrl_connect(AppState *app, const char *src, const char *dst, float amount);
int  ui_cable_audio_connect(AppState *app, const char *src, const char *dst, float amount);
int  ui_cable_disconnect_ctrl(AppState *app, int id);
int  ui_cable_disconnect_audio(AppState *app, int id);
int  ui_cable_set_amount_ctrl(AppState *app, int id, float amount);
int  ui_cable_set_amount_audio(AppState *app, int id, float amount);

// Performance triggers
void ui_note_on(AppState *app, int note, int velocity);
void ui_note_off(AppState *app);                 // release everything (MIDI note_off / PANIC)
void ui_note_off_note(AppState *app, int note);  // release one key, stack fallback to previous held
bool ui_is_note_held(const AppState *app, int note);
void ui_note_name(int note, char *buf, int buflen); // e.g. "A4"
void ui_pitch_bend(AppState *app, float val);      // -1..+1
void ui_mod_wheel(AppState *app, float val);       // 0..1
void ui_aftertouch(AppState *app, float val);      // 0..1
void ui_breath(AppState *app, float val);          // 0..1

// Snapshot / Preset
bool ui_save_preset(const AppState *app, const char *path);
bool ui_load_preset(AppState *app, const char *path);
bool ui_save_preset_text(const AppState *app, const char *path);

// Offline render (WAV export)
bool ui_render_offline(AppState *app, float seconds, const char *wav_path);

// Action log
void ui_start_record(AppState *app);
void ui_stop_record(AppState *app);
void ui_clear_log(AppState *app);
bool ui_save_log(const AppState *app, const char *path);
bool ui_load_log(AppState *app, const char *path);
void ui_replay_log(AppState *app);

// Refresh parameter cache after bulk edits
void ui_refresh_cache(AppState *app);

// Write 32-bit float WAV
bool write_wav_float32(const char *path, const float *data, int n_samples, int sample_rate);

} // namespace gp1_ui

#endif // SYNTH_UI_H
