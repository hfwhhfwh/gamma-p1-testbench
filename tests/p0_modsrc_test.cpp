// P0-MODSRC 验收用无头 UI 级测试 (不进交付, 验收证据)
// 覆盖任务 3.1: voice.velocity / voice.note / voice.gate 三个演奏状态
// 调制源端口的索引/kind 与读回值 (经 kcapi_get_mod_src_value 走内核
// vmatrix 施工同一 read_src 路径)。
#include "../src/synth_ui.h"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace gp1_ui;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("  [OK]   %s\n", name); \
    else { printf("  [FAIL] %s\n", name); g_fails++; } \
} while (0)

static int find_port(const char *name)
{
    int n = kcapi_num_ports();
    for (int i = 0; i < n; i++) {
        const char *p = kcapi_port_name(i);
        if (p && strcmp(p, name) == 0)
            return i;
    }
    return -1;
}

int main(void)
{
    static AppState app;
    ui_init(&app);

    /* 1) 端口表尾部追加: 总数 374, 三个新源在 312~314 且为 READ 口
     *    [PLAY-FEEL-1: 总数判据 315->325 (任务书授权尾部追加 10 口),
     *    312~314 索引判据原样保留]
     *    [HOUSEKEEPING-2: 总数判据 325->373 (任务书授权尾部追加 48 口),
     *    前 325 口判据原样保留]
     *    [LEGATO-FIX-1: 总数判据 373->374 (任务书授权尾部追加 1 口
     *    voice.legato_mode @373), 前 373 口判据原样保留] */
    int p_vel  = find_port("voice.velocity");
    int p_note = find_port("voice.note");
    int p_gate = find_port("voice.gate");
    printf("  [info] num_ports=%d velocity=%d note=%d gate=%d\n",
           kcapi_num_ports(), p_vel, p_note, p_gate);
    CHECK(kcapi_num_ports() == 374, "端口总数 374 (旧 373 + LEGATO-FIX-1 1)");
    CHECK(p_vel == 312 && p_note == 313 && p_gate == 314,
          "voice.velocity/note/gate 纯尾部追加 (312~314)");
    CHECK(kcapi_port_kind(p_vel) == 0 && kcapi_port_kind(p_note) == 0 &&
          kcapi_port_kind(p_gate) == 0, "三个新口均为 READ (kind=0)");

    float buf[64];

    /* 2) note_on(60, 64): velocity 源 0.5039±0.001, note 源 0.000,
     *    gate 源 1.000 (渲染一块后读回, 与施工同语义) */
    ui_note_on(&app, 60, 64);
    kcapi_render_block(app.synth, buf, 64);
    float v_vel  = kcapi_get_mod_src_value(app.synth, p_vel);
    float v_note = kcapi_get_mod_src_value(app.synth, p_note);
    float v_gate = kcapi_get_mod_src_value(app.synth, p_gate);
    printf("  [info] note_on(60,64): velocity=%.4f note=%.4f gate=%.4f\n",
           v_vel, v_note, v_gate);
    CHECK(fabsf(v_vel - 0.5039f) <= 0.001f,
          "note_on(60,64): velocity 源 = 0.5039±0.001");
    CHECK(v_note == 0.0f, "note_on(60,64): note 源 = 0.000 (C4=0)");
    CHECK(v_gate == 1.0f, "note_on(60,64): gate 源 = 1.000");

    /* 3) note_off: gate 源回 0.000 */
    ui_note_off(&app);
    kcapi_render_block(app.synth, buf, 64);
    v_gate = kcapi_get_mod_src_value(app.synth, p_gate);
    printf("  [info] note_off: gate=%.4f\n", v_gate);
    CHECK(v_gate == 0.0f, "note_off: gate 源 = 0.000");

    /* 4) note_on(72, 127): note 源 0.250, velocity 源 1.000 */
    ui_note_on(&app, 72, 127);
    kcapi_render_block(app.synth, buf, 64);
    v_note = kcapi_get_mod_src_value(app.synth, p_note);
    v_vel  = kcapi_get_mod_src_value(app.synth, p_vel);
    printf("  [info] note_on(72,127): note=%.4f velocity=%.4f\n", v_note, v_vel);
    CHECK(fabsf(v_note - 0.250f) < 1e-6f, "note_on(72): note 源 = 0.250");
    CHECK(v_vel == 1.0f, "note_on(72,127): velocity 源 = 1.000");
    ui_note_off(&app);

    /* 5) 越界/非 READ 口防御: 返回 0.0f
     *    [PLAY-FEEL-1: 越界索引 315 -> 325 (325 起才是越界)]
     *    [HOUSEKEEPING-2: 越界索引 325 -> 373 (373 起才是越界)]
     *    [LEGATO-FIX-1: 越界索引 373 -> 374 (374 起才是越界)] */
    CHECK(kcapi_get_mod_src_value(app.synth, -1) == 0.0f &&
          kcapi_get_mod_src_value(app.synth, 374) == 0.0f,
          "越界索引返回 0.0f");
    int p_dst = find_port("vca.level");
    CHECK(p_dst >= 0 && kcapi_get_mod_src_value(app.synth, p_dst) == 0.0f,
          "非 READ 口 (vca.level) 返回 0.0f");

    printf("\n==== 合计 %d 项检查, 失败 %d 项 ====\n", g_checks, g_fails);
    ui_shutdown(&app);
    return g_fails ? 1 : 0;
}
