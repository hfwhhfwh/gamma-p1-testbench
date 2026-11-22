/* SNAPSHOT-FIX-1 —— snapshot_kcapi_test.c (测试台侧正式验收, 任务书 T-d)
 *
 * 覆盖:
 *   T-d (kcapi 路径): kcapi_set_param "voice.engine_mode"=99 写后
 *      kcapi_get_param 读回 = 原值 (越界忽略, 内核 voice_set_engine_mode
 *      钳位); 合法 0~3 正常落地。写入返回码 0=落地。
 *   T-g (kcapi 快照口): kcapi_snapshot_save_v5 / kcapi_snapshot_v5_size /
 *      kcapi_snapshot_load_legacy 往返:
 *      - v5_size==5740; save_v5 空指针 -1 / cap 不足 -1 / 正常 0;
 *        头 4 字节 'GP1S' + version=5;
 *      - V5 经 kcapi_snapshot_load_legacy 载入第二实例 rc==0, 参数读回
 *        一致 + 音频线表读回一致 + 双实例渲染逐位一致 (重发布 P-02 经
 *        kcapi 路径生效);
 *      - CRC 错 V5 拒载 rc==-1;
 *      - legacy V1 夹具 (fm/dx7_out/preset_v1.snp, 只读) rc==0;
 *      - legacy V2 夹具 (fm/dx7_out/preset_v2.snp, 只读) rc==0;
 *      - legacy V3 夹具 (tests/fuzz_run1/sessionA.snp, 只读) rc==0。
 *
 * 纪律: 只走 kcapi 用户路径; 只读观测 (kcapi_get_*); 零警告
 *   (-Wall -Wextra -Werror -std=c11 -pedantic); 手工件不进构建系统
 *   (build_tbfix3_tests.sh 第 10 段); 守门夹具一律只读不碰。
 * 用法: cd gamma-p1-testbench/tests && ./snapshot_kcapi_test
 */

#include "kernel_capi.h"

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
            if (isnan(v) || isinf(v))
                bad++;
        }
    }
    return bad;
}

static int load_file(const char *path, unsigned char *buf, long cap, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    *len = (long)fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return 0;
}

int main(void)
{
    static unsigned char v5[5740];
    static float out_a[NB * KCAPI_MAX_BLOCK], out_b[NB * KCAPI_MAX_BLOCK];
    float r;
    int rc;

    printf("== SNAPSHOT-FIX-1 测试台验收: T-d kcapi + V5 快照口 ==\n");
    kcapi_vco_bank_init();
    kcapi_synth_t *s1 = kcapi_synth_create();
    kcapi_synth_t *s2 = kcapi_synth_create();
    kcapi_synth_init(s1, KCAPI_SAMPLE_RATE);
    kcapi_synth_init(s2, KCAPI_SAMPLE_RATE);

    /* ---------- T-d: engine_mode kcapi 写读 ---------- */
    printf("-- T-d: kcapi_set_param/get_param voice.engine_mode --\n");
    rc = kcapi_set_param(s1, "voice.engine_mode", 1.0f);
    CHECK(rc == 0 && kcapi_get_param(s1, "voice.engine_mode", &r) == 0 &&
          r == 1.0f, "T-d: 写 1 读回 1 (rc=%d)", rc);
    rc = kcapi_set_param(s1, "voice.engine_mode", 99.0f);
    CHECK(rc == 0 && kcapi_get_param(s1, "voice.engine_mode", &r) == 0 &&
          r == 1.0f, "T-d: 写 99 读回=原值 1 (越界忽略, rc=%d)", rc);
    for (int m = 0; m <= 3; m++) {
        kcapi_set_param(s1, "voice.engine_mode", (float)m);
        int ok = (kcapi_get_param(s1, "voice.engine_mode", &r) == 0) &&
                 ((int)r == m);
        CHECK(ok, "T-d: 合法 engine_mode=%d 读回落地", m);
    }

    /* ---------- T-g: V5 保存口 ---------- */
    printf("-- T-g: kcapi_snapshot_save_v5 / load_legacy 往返 --\n");
    kcapi_set_param(s1, "fm.algorithm", 5.0f);
    kcapi_set_param(s1, "vca.level", 0.42f);
    int cid = kcapi_cable_audio_connect(s1, "vco0.buf", "vca.gain_audio", 0.5f);
    CHECK(cid > 0, "T-g 准备: 音频线 connect id=%d", cid);

    CHECK(kcapi_snapshot_v5_size() == 5740u, "T-g: kcapi v5_size == 5740");
    rc = kcapi_snapshot_save_v5(s1, NULL, 5740);
    CHECK(rc == -1, "T-g: save_v5 空指针 buf rc==-1");
    rc = kcapi_snapshot_save_v5(s1, v5, 5739);
    CHECK(rc == -1, "T-g: save_v5 cap=5739 不足 rc==-1");
    rc = kcapi_snapshot_save_v5(s1, v5, sizeof(v5));
    CHECK(rc == 0, "T-g: save_v5 cap=5740 rc==0");
    CHECK(v5[0] == 'G' && v5[1] == 'P' && v5[2] == '1' && v5[3] == 'S' &&
          v5[4] == 5 && v5[5] == 0, "T-g: V5 头 magic+version 正确");

    /* V5 载入第二实例, 往返一致 */
    rc = kcapi_snapshot_load_legacy(s2, v5, 5740);
    CHECK(rc == 0, "T-g: load_legacy(V5) rc==0");
    float ra, rb;
    int ok_alg = (kcapi_get_param(s2, "fm.algorithm", &ra) == 0) && (ra == 5.0f);
    int ok_lvl = (kcapi_get_param(s2, "vca.level", &rb) == 0) && (rb == 0.42f);
    CHECK(ok_alg, "T-g: V5 往返 fm.algorithm=5 读回一致");
    CHECK(ok_lvl, "T-g: V5 往返 vca.level=0.42 读回一致");
    {
        /* kcapi_audio_cable_count 返回固定迭代空间 (VMX_MAX_ACABLES),
         * 活跃线需遍历 info 统计 */
        int nact = 0, id = 0, act = 0, dorm = 0;
        const char *sn = NULL, *dn = NULL;
        float amt = 0.0f;
        int total = kcapi_audio_cable_count(s2);
        for (int i = 0; i < total; i++) {
            kcapi_audio_cable_info(s2, i, &id, &sn, &dn, &amt, &act, &dorm);
            if (act)
                nact++;
        }
        CHECK(nact == 1, "T-g: V5 往返后活跃音频线 1 条 (遍历 %d 槽)", total);
        kcapi_audio_cable_info(s2, 0, &id, &sn, &dn, &amt, &act, &dorm);
        CHECK(id == cid && sn && dn && strcmp(sn, "vco0.buf") == 0 &&
              strcmp(dn, "vca.gain_audio") == 0 && amt == 0.5f && act == 1,
              "T-g: 线参数读回 (vco0.buf->vca.gain_audio, 0.5, active)");
    }
    kcapi_note_on(s1, 60, 100);
    kcapi_note_on(s2, 60, 100);
    long bad_a = render_scan(s1, out_a);
    long bad_b = render_scan(s2, out_b);
    CHECK(bad_a == 0 && bad_b == 0, "T-g: 双实例渲染 %d 块无 NaN/Inf", NB);
    CHECK(memcmp(out_a, out_b, sizeof(out_a)) == 0,
          "T-g: V5 往返双实例渲染逐位一致");
    kcapi_note_off(s1);
    kcapi_note_off(s2);

    /* CRC 错 V5 拒载 */
    v5[100] ^= 0x01;
    rc = kcapi_snapshot_load_legacy(s2, v5, 5740);
    CHECK(rc == -1, "T-g: V5 CRC 错拒载 rc==-1");
    v5[100] ^= 0x01;

    /* ---------- legacy V1/V2/V3 夹具加载回归 (只读原件) ---------- */
    printf("-- legacy 夹具: V1/V2/V3 加载回归 (原件只读) --\n");
    static unsigned char legacy[16384];
    long len = 0;
    rc = load_file("../../gamma-p1/fm/dx7_out/preset_v1.snp",
                   legacy, sizeof(legacy), &len);
    CHECK(rc == 0 && len == 5104, "夹具: preset_v1.snp 读入 %ld B (V1)", len);
    if (rc == 0) {
        int rr = kcapi_snapshot_load_legacy(s2, legacy, (int)len);
        CHECK(rr == 0, "夹具: V1 (5104B) load_legacy rc==0");
    }
    rc = load_file("../../gamma-p1/fm/dx7_out/preset_v2.snp",
                   legacy, sizeof(legacy), &len);
    CHECK(rc == 0 && len == 5716, "夹具: preset_v2.snp 读入 %ld B (V2)", len);
    if (rc == 0) {
        int rr = kcapi_snapshot_load_legacy(s2, legacy, (int)len);
        CHECK(rr == 0, "夹具: V2 (5716B) load_legacy rc==0");
    }
    rc = load_file("../../gamma-p1/tests/fuzz_run1/sessionA.snp",
                   legacy, sizeof(legacy), &len);
    CHECK(rc == 0 && len == 5728, "夹具: sessionA.snp 读入 %ld B (V3)", len);
    if (rc == 0) {
        int rr = kcapi_snapshot_load_legacy(s2, legacy, (int)len);
        CHECK(rr == 0, "夹具: V3 (5728B) load_legacy rc==0");
    }

    printf("== %s (%d 项检查, %d 项失败) ==\n",
           g_fails == 0 ? "PASS" : "FAIL", g_checks, g_fails);
    kcapi_synth_destroy(s1);
    kcapi_synth_destroy(s2);
    return g_fails == 0 ? 0 : 1;
}
