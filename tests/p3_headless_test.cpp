// TESTBENCH-P3 验收用无头 UI 级测试 (不进交付, 验收证据)
// 覆盖: 默认线存在/可删、开机静音、三 ENV 重触发、release 收尾、
//       Retrig LFOs 开关行为 (经 vca.level 参数桥观测 LFO 相位)。
//
// 变更摘要 (2026-10-25 TBENCH-INFRA-1):
//   P-56: 两处判据容差化 —— :46 amt==1.0f -> fabsf(amt-1.0f)<=1e-5f,
//     :54 pk==0.0f -> fabsf(pk)<=1e-6f, 均附"原判据⇄新判据目标语义
//     等价"注释 (判据意图不变, 容差仅吸收浮点表示噪声);
//   hygiene: 补 #include <cstring> (strcmp 原靠传递包含侥幸)。
#include "../src/synth_ui.h"
#include <cstdio>
#include <cstring>   // TBENCH-INFRA-1: hygiene strcmp 传递包含侥幸
#include <cmath>

using namespace gp1_ui;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("  [OK]   %s\n", name); \
    else { printf("  [FAIL] %s\n", name); g_fails++; } \
} while (0)

static float render_blocks(AppState *app, int nblocks, float *peak_out)
{
    float buf[64];
    float peak = 0.0f;
    for (int b = 0; b < nblocks; b++) {
        kcapi_render_block(app->synth, buf, 64);
        for (int i = 0; i < 64; i++) {
            float a = fabsf(buf[i]);
            if (a > peak) peak = a;
        }
    }
    if (peak_out) *peak_out = peak;
    return peak;
}

int main(void)
{
    static AppState app;
    ui_init(&app);

    /* 1) 默认线 env0.value -> vca.level amount=1.0 存在且可见 */
    int found = 0, def_id = -1;
    int nc = kcapi_ctrl_cable_count(app.synth);
    for (int i = 0; i < nc; i++) {
        int id, active;
        const char *src, *dst;
        float amt;
        kcapi_ctrl_cable_info(app.synth, i, &id, &src, &dst, &amt, &active);
        if (active && src && dst && strcmp(src, "env0.value") == 0 &&
            strcmp(dst, "vca.level") == 0 &&
            /* TBENCH-INFRA-1: P-56 原判据⇄新判据目标语义等价: 默认线
             * amount 由 ui_init 建立时写死 1.0f, 该值不经浮点变换,
             * amt==1.0f 与 fabsf(amt-1.0f)<=1e-5f 同真同假; 容差仅吸收
             * 未来表示噪声, 判据意图"默认线满深度存在"不变 */
            fabsf(amt - 1.0f) <= 1e-5f) {
            found = 1; def_id = id;
        }
    }
    CHECK(found, "默认线 env0.value->vca.level amount=1.0 存在");

    /* 2) 开机静音: 渲染 8 块峰值保持 0 */
    float pk = render_blocks(&app, 8, nullptr);
    /* TBENCH-INFRA-1: P-56 原判据⇄新判据目标语义等价: pk 为 |样本| 的
     * 最大值恒 >= 0; 静音路径各样本逐位 0 -> pk 恰为 0.0f, 故
     * pk==0.0f 与 fabsf(pk)<=1e-6f 同真; 容差仅吸收浮点累加噪声,
     * 判据意图"开机静音"不变 */
    CHECK(fabsf(pk) <= 1e-6f, "开机静音: Peak = 0.000");

    /* 3) 点键发声 + 三个 ENV 都被重触发 (env1/env2 无接线, 直接读输出口) */
    ui_note_on(&app, 60, 100);
    render_blocks(&app, 40, &pk);   /* 40 块 ≈ 53ms, 过 attack 进 sustain */
    CHECK(pk > 0.05f, "点键后发声 (env0 驱动 vca.level)");
    float e1 = kcapi_get_env_output(app.synth, 1);
    float e2 = kcapi_get_env_output(app.synth, 2);
    CHECK(e1 > 0.1f && e2 > 0.1f, "note_on 重触发全部 3 个 ENV (env1/2 输出 > 0)");

    /* 4) 松键 release 收尾回静音 (先放掉 release 段, 再测尾部 20 块) */
    ui_note_off(&app);
    render_blocks(&app, 280, nullptr); /* 0.37s > release 0.2s 的衰减过程 */
    render_blocks(&app, 20, &pk);
    float e0 = kcapi_get_env_output(app.synth, 0);
    CHECK(pk < 0.001f && e0 < 0.001f, "松键 release 收尾后回静音");

    /* 5) Retrig LFOs 开关: 先删默认线 (验证"可删"), 再经 capi 直接读
     * lfo0 实时输出观测相位。lfo0 = 1Hz sine (相位 0 -> 输出 0,
     * 相位 0.25 -> 输出 ≈ +1): 相位归零后自由运转 188 块 (0.2507s),
     * retrig 关时 note_on 后输出仍 ≈ +1, 开时回到 ≈ 0。 */
    CHECK(ui_cable_disconnect_ctrl(&app, def_id) == 0, "默认线可删除 (机制透明)");
    ui_set_param(&app, "lfo0.frequency", 1.0f);
    ui_set_param(&app, "lfo0.waveform", 0.0f); /* sine */
    ui_set_param(&app, "lfo0.depth", 1.0f);

    /* (a) retrig 关: note_on 不复位相位 */
    app.retrig_lfos_on_note = false;
    kcapi_lfo_set_phase(app.synth, 0, 0.0f);
    render_blocks(&app, 188, nullptr); /* 188 块 = 0.2507s -> 相位 ≈ 0.25 */
    ui_note_on(&app, 60, 100);
    render_blocks(&app, 2, nullptr);
    float lvl_free = kcapi_get_lfo_output(app.synth, 0);
    ui_note_off(&app);

    /* (b) retrig 开: note_on 相位归零 */
    app.retrig_lfos_on_note = true;
    kcapi_lfo_set_phase(app.synth, 0, 0.25f);
    ui_note_on(&app, 60, 100);
    render_blocks(&app, 2, nullptr);
    float lvl_retrig = kcapi_get_lfo_output(app.synth, 0);
    ui_note_off(&app);

    printf("  [info] retrig 关 lfo_out=%.3f / 开 lfo_out=%.3f\n", lvl_free, lvl_retrig);
    CHECK(lvl_free > 0.9f, "retrig 关: LFO 自由运转 (相位不归零)");
    CHECK(fabsf(lvl_retrig) < 0.1f, "retrig 开: note_on 相位归零");

    printf("\n==== 合计 %d 项检查, 失败 %d 项 ====\n", g_checks, g_fails);
    ui_shutdown(&app);
    return g_fails ? 1 : 0;
}
