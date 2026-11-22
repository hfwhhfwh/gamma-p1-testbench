/*
 * win_render_dump.c — TBENCH-WIN-1 WP4 跨平台渲染基线 dump 工具
 *
 * 用途 (不是第 14 件测试, 不进 13 件清单, 不参与 ctest 判据):
 *   用 kcapi 用户路径渲染一段确定性乐句 (默认 patch 复位态 +
 *   voice.engine_mode=1 (FM) + note_on 60/vel 100 + 参数扰动 +
 *   note_off 后尾音), 以 %.9g 逐样本写 CSV。Mac 侧产物存
 *   tests/baseline_win/mac_ref/render_dump.csv (建档后冻结), 搭档在
 *   Windows 侧跑本工具产出 baseline_win/render_dump_win.csv,
 *   verify_win.ps1 以 1e-6 容差逐项比对出报告。
 *
 * 设计要点:
 *   - 全部经 kcapi 公共口 (与测试件同口径), 不触内核内部;
 *   - 渲染前显式复位参数 (defense1_kcapi_test 先例), 避免静态默认漂移;
 *   - 输出路径由 argv[1] 给定 (相对/绝对均可), 便于两侧同脚本调用;
 *   - 判据语义: 跨平台浮点一致性 (MSVC /fp:precise vs clang -O2,
 *     同为 IEEE754 单精度, sinf/exp2f 等 UCRT 与 libm 实现差即
 *     本工具要量化的对象; 阈值 1e-6, 超出者在报告逐项分析)。
 *
 * 用法: win_render_dump <输出.csv>
 */

#include "kernel_capi.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NB_ON   150   /* note_on 期间块数 (~0.2s) */
#define NB_OFF  50    /* note_off 后尾音块数 */
#define NTOT    (NB_ON + NB_OFF)

static int is_nonfinite(float v)
{
    return isnan(v) || isinf(v);
}

int main(int argc, char **argv)
{
    static float out[NTOT * KCAPI_MAX_BLOCK];
    const char *path = (argc > 1) ? argv[1] : "render_dump.csv";
    int i, rc = 0;

    kcapi_vco_bank_init();
    kcapi_synth_t *s = kcapi_synth_create();
    if (!s) {
        fprintf(stderr, "win_render_dump: kcapi_synth_create 失败\n");
        return 1;
    }
    kcapi_synth_init(s, KCAPI_SAMPLE_RATE);

    /* 确定性参数面 (复位态 + FM 引擎 + 少量用户路径扰动) */
    kcapi_set_param(s, "voice.engine_mode", 1.0f);   /* FM */
    kcapi_set_param(s, "fm.algorithm", 5.0f);
    kcapi_set_param(s, "vca.level", 0.8f);
    kcapi_set_param(s, "vcf.cutoff", 0.66f);

    kcapi_note_on(s, 60, 100);
    for (int b = 0; b < NTOT; b++) {
        if (b == NB_ON)
            kcapi_note_off(s);
        kcapi_render_block(s, out + b * KCAPI_MAX_BLOCK, KCAPI_MAX_BLOCK);
    }

    for (i = 0; i < NTOT * KCAPI_MAX_BLOCK; i++) {
        if (is_nonfinite(out[i])) {
            fprintf(stderr, "win_render_dump: 样本 %d 非有限值\n", i);
            rc = 1;
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "win_render_dump: 无法写 %s\n", path);
        return 1;
    }
    fprintf(f, "# TBENCH-WIN-1 render dump: engine=1 fm.algo=5 vca=0.8 "
               "vcf.cutoff=0.66 note=60 vel=100 blocks=%d sr=48000\n", NTOT);
    for (i = 0; i < NTOT * KCAPI_MAX_BLOCK; i++)
        fprintf(f, "%.9g\n", (double)out[i]);
    fclose(f);
    printf("win_render_dump: %d 样本 -> %s (rc=%d)\n",
           NTOT * KCAPI_MAX_BLOCK, path, rc);
    return rc;
}
