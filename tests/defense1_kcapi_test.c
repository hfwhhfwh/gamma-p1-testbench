/*
 * defense1_kcapi_test.c — DEFENSE-1 测试台侧验收 (kcapi 路径)
 *
 * 任务: DEFENSE-1 WP4+WP5    日期: 2026-10-27
 *
 * 覆盖:
 *   K1 (T-i kcapi 路径): dst_port=65535 恶意 V4 blob 经
 *      kcapi_snapshot_load_legacy -> 返回 -1 (转入项拒载上提透传);
 *      拒载后音频线表读回不被污染 (仍 1 条活跃线 vco0.buf->
 *      vca.gain_audio); 与合法加载孪生实例双路渲染逐位一致 (有限,
 *      零行为差)。
 *   K2 (T-d 回归): kcapi_set_param "voice.engine_mode"=99 读回=原值
 *      (越界钳位不落地); 合法 0~3 落地。完整覆盖见既有
 *      snapshot_kcapi_test (26 项全过, 本件重链后重跑, 引用)。
 *   K3 (快照回归): 合法 V5 经 kcapi_snapshot_save_v5/load_legacy 往返
 *      rc==0; V1 夹具 (fm/dx7_out/preset_v1.snp, 守门只读) rc==0。
 *      V1~V3 夹具全量覆盖见既有 snapshot_kcapi_test (引用)。
 *   K4 (UI/alog 等价路径引用): snapshot_ui_test 既有 T6 已断言
 *      "垃圾加载 alog 零 preset_load 行" (25 项全过, 引用不重造)。
 *
 * 纪律: 写入只走 kcapi 用户路径 (K1 blob 构造为测试攻击面模拟,
 *   偏移经 offsetof 计算); 零警告 (-Wall -Wextra -Werror -std=c11
 *   -pedantic); 手工件不进构建系统 (build_tbfix3_tests.sh 第 12 段);
 *   守门夹具一律只读。
 * 用法: cd gamma-p1-testbench/tests && ./defense1_kcapi_test
 */

#include "kernel_capi.h"
#include "synth.h"
#include "snapshot_v1.h"
#include "vmatrix.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { printf("  PASS "); printf(__VA_ARGS__); }        \
    else      { printf("  FAIL "); printf(__VA_ARGS__); g_fails++; } \
    printf("\n");                                                \
} while (0)

#define NB 32   /* 渲染块数 */

static long render_scan(kcapi_synth_t *s, float *out)
{
    long bad = 0;
    for (int b = 0; b < NB; b++) {
        kcapi_render_block(s, out + b * KCAPI_MAX_BLOCK, KCAPI_MAX_BLOCK);
        for (int i = 0; i < KCAPI_MAX_BLOCK; i++) {
            float v = out[b * KCAPI_MAX_BLOCK + i];
            if (!isfinite(v))
                bad++;
        }
    }
    return bad;
}

static int count_active_audio(kcapi_synth_t *s, int *first_id,
                              const char **first_dst, float *first_amt)
{
    int nact = 0, id = 0, act = 0, dorm = 0;
    const char *sn = NULL, *dn = NULL;
    float amt = 0.0f;
    int total = kcapi_audio_cable_count(s);
    for (int i = 0; i < total; i++) {
        kcapi_audio_cable_info(s, i, &id, &sn, &dn, &amt, &act, &dorm);
        if (act) {
            if (nact == 0 && first_id) {
                *first_id = id;
                *first_dst = dn;
                *first_amt = amt;
            }
            nact++;
        }
    }
    return nact;
}

int main(void)
{
    static unsigned char blob[5740];
    static float out_a[NB * KCAPI_MAX_BLOCK], out_b[NB * KCAPI_MAX_BLOCK];

    printf("== DEFENSE-1 测试台侧验收 (kcapi 路径) ==\n");
    kcapi_vco_bank_init();

    /* ---------- K1: T-i kcapi 路径 (blob 层拒载上提) ---------- */
    printf("-- K1: 恶意 V4 blob 经 kcapi_snapshot_load_legacy 拒载 --\n");
    {
        kcapi_synth_t *s1 = kcapi_synth_create();
        kcapi_synth_t *s2 = kcapi_synth_create();
        kcapi_synth_init(s1, KCAPI_SAMPLE_RATE);
        kcapi_synth_init(s2, KCAPI_SAMPLE_RATE);
        int id = kcapi_cable_audio_connect(s1, "vco0.buf",
                                           "vca.gain_audio", 0.5f);
        CHECK(id > 0, "K1 准备: s1 音频线 connect 成功 (id=%d)", id);
        int id2 = kcapi_cable_audio_connect(s2, "vco0.buf",
                                            "vca.gain_audio", 0.5f);
        CHECK(id2 > 0, "K1 准备: s2 孪生音频线 connect 成功 (id=%d)", id2);
        int nac = count_active_audio(s1, NULL, NULL, NULL);
        CHECK(nac == 1, "K1 准备: s1 线表 1 条活跃音频线 (实际 %d)", nac);

        const int v4len = kcapi_snapshot_size();
        CHECK(v4len == (int)sizeof(SynthSnapshot),
              "K1 准备: kcapi_snapshot_size == sizeof(SynthSnapshot) (%d)", v4len);
        kcapi_snapshot_save(s1, blob);
        /* 攻击面模拟: 污染线表首项 dst_port (偏移经 offsetof, 不硬编) */
        const size_t off =
            offsetof(SynthSnapshot, vmatrix_snap) +
            offsetof(VmatrixSnapshot, acables[0]) +
            offsetof(VmxAcable, dst_port);
        unsigned char bad[5740];
        memcpy(bad, blob, sizeof(blob));
        {
            uint16_t poison = 65535;
            memcpy(bad + off, &poison, sizeof(poison));
        }
        int rc = kcapi_snapshot_load_legacy(s1, bad, v4len);
        CHECK(rc != 0, "K1: kcapi_snapshot_load_legacy(恶意 V4) 返回非 0 (实际 %d)",
              rc);
        CHECK(rc == -1, "K1: 拒载码 -1 (转入项透传, 实际 %d)", rc);
        /* 拒载后线表零污染 */
        {
            int cid = -1; const char *dn = NULL; float amt = 0.0f;
            int nact = count_active_audio(s1, &cid, &dn, &amt);
            CHECK(nact == 1, "K1: 拒载后 s1 活跃音频线仍 1 条 (零污染, 实际 %d)",
                  nact);
            CHECK(cid == id && dn && strcmp(dn, "vca.gain_audio") == 0 &&
                  amt == 0.5f,
                  "K1: 拒载后线表首项仍 vco0.buf->vca.gain_audio (0.5)");
        }
        /* 孪生对照: s1 拒载 / s2 合法, 双路渲染逐位一致 */
        rc = kcapi_snapshot_load_legacy(s2, blob, v4len);
        CHECK(rc == 0, "K1: 合法 blob 经 load_legacy rc==0 (孪生 s2)");
        kcapi_note_on(s1, 60, 100);
        kcapi_note_on(s2, 60, 100);
        long bad_a = render_scan(s1, out_a);
        long bad_b = render_scan(s2, out_b);
        CHECK(bad_a == 0 && bad_b == 0,
              "K1: 拒载/合法两路渲染 %d 块全有限", NB);
        CHECK(memcmp(out_a, out_b, sizeof(out_a)) == 0,
              "K1: 拒载后渲染与合法加载逐位一致 (kcapi 路径零行为差)");
        kcapi_note_off(s1);
        kcapi_note_off(s2);
        kcapi_synth_destroy(s1);
        kcapi_synth_destroy(s2);
    }

    /* ---------- K2: T-d 回归 (engine_mode 越界) ---------- */
    printf("-- K2: kcapi_set_param engine_mode=99 读回=原值 --\n");
    {
        kcapi_synth_t *s = kcapi_synth_create();
        kcapi_synth_init(s, KCAPI_SAMPLE_RATE);
        CHECK(kcapi_set_param(s, "voice.engine_mode", 1.0f) == 0,
              "K2: 合法 engine_mode=1 落地 rc=0");
        float rd = -1.0f;
        CHECK(kcapi_get_param(s, "voice.engine_mode", &rd) == 0 &&
              rd == 1.0f, "K2: 读回=1 (准备)");
        int rc = kcapi_set_param(s, "voice.engine_mode", 99.0f);
        CHECK(kcapi_get_param(s, "voice.engine_mode", &rd) == 0 &&
              rd == 1.0f,
              "K2: engine_mode=99 读回=原值 1 (越界忽略, setter rc=%d)", rc);
        for (int m = 0; m <= 3; m++) {
            kcapi_set_param(s, "voice.engine_mode", (float)m);
            float v = -1.0f;
            CHECK(kcapi_get_param(s, "voice.engine_mode", &v) == 0 &&
                  v == (float)m, "K2: 合法 engine_mode=%d 落地读回", m);
        }
        kcapi_synth_destroy(s);
    }

    /* ---------- K3: 合法快照回归 (V5 往返 / V1 夹具) ---------- */
    printf("-- K3: 合法 V5 往返 / V1 夹具加载回归 --\n");
    {
        kcapi_synth_t *s1 = kcapi_synth_create();
        kcapi_synth_t *s2 = kcapi_synth_create();
        kcapi_synth_init(s1, KCAPI_SAMPLE_RATE);
        kcapi_synth_init(s2, KCAPI_SAMPLE_RATE);
        kcapi_cable_audio_connect(s1, "vco0.buf", "vca.gain_audio", 0.5f);
        CHECK(kcapi_snapshot_v5_size() == 5740u,
              "K3: kcapi_snapshot_v5_size == 5740");
        CHECK(kcapi_snapshot_save_v5(s1, blob, sizeof(blob)) == 0,
              "K3: kcapi_snapshot_save_v5 rc==0");
        int rc = kcapi_snapshot_load_legacy(s2, blob,
                                            (int)kcapi_snapshot_v5_size());
        CHECK(rc == 0, "K3: V5 经 load_legacy 载入第二实例 rc==0");
        CHECK(count_active_audio(s2, NULL, NULL, NULL) == 1,
              "K3: V5 往返后活跃音频线 1 条 (重发布经 kcapi 路径生效)");
        /* V1 守门夹具 (只读) */
        {
            static unsigned char v1[6000];
            FILE *f = fopen("../../gamma-p1/fm/dx7_out/preset_v1.snp", "rb");
            long len = f ? (long)fread(v1, 1, sizeof(v1), f) : -1;
            if (f) fclose(f);
            CHECK(len == (long)kcapi_snapshot_v1_size(),
                  "K3: V1 夹具 preset_v1.snp 读取 (%ldB)", len);
            kcapi_synth_init(s2, KCAPI_SAMPLE_RATE);
            rc = kcapi_snapshot_load_legacy(s2, v1,
                                            kcapi_snapshot_v1_size());
            CHECK(rc == 0, "K3: 合法 V1 夹具经 load_legacy rc==0");
        }
        kcapi_synth_destroy(s1);
        kcapi_synth_destroy(s2);
    }

    printf("== %s (检查 %d 项, 失败 %d 项) ==\n",
           g_fails == 0 ? "PASS" : "FAIL", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
