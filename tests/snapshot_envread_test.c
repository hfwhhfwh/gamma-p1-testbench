/* SNAPSHOT-FIX-1 —— S7 九读断口动态自证 (测试台侧 WP3 自证件 4)
 *
 * 验收 (任务书 WP3 自证 4):
 *   env0/1/2 × sustain_point / loop_start / loop_end 三个名字各写一个
 *   代表整数值, 随即 kcapi_get_param 读回, 9 例必须全部同名同值
 *   (写口径 = kcapi_set_param env 段 env_set_sustain_point /
 *   env_set_loop, 读口径 = kcapi_read_param_port SNAPSHOT-FIX-1 分支
 *   EnvShapeParams.sustain_point_index / loop_start_index /
 *   loop_end_index, 经别名闸门 kcapi_is_alias_read 路由)。
 *   附带口径交叉 3 例: 端口原名 envN.sustain_point_index 与别名
 *   envN.sustain_point 读同字段必须同值 (写分支 sustain_point 与
 *   端口 sustain_point_index 落点相同)。
 *
 * 纪律: 只走 kcapi 用户路径; 只读观测; 零警告 (-Wall -Wextra -Werror
 * -std=c11 -pedantic); 日志放 sfix1_out/ (由驱动脚本/手工先建目录)。
 */

#include "kernel_capi.h"

#include <stdio.h>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { printf("  PASS "); printf(__VA_ARGS__); }        \
    else      { printf("  FAIL "); printf(__VA_ARGS__); g_fails++; } \
    printf("\n");                                                \
} while (0)

int main(void)
{
    kcapi_vco_bank_init();
    kcapi_synth_t *s = kcapi_synth_create();
    kcapi_synth_init(s, 48000.0f);

    /* env0/1/2 × sustain_point/loop_start/loop_end 代表值 (setter
     * 原样存 int, 无钳位; 取值互不相同防串位) */
    static const char *const subs[3] = { "sustain_point", "loop_start", "loop_end" };
    static const int vals[3][3] = {
        { 2, 0, 1 },   /* env0 */
        { 1, 1, 3 },   /* env1 */
        { 3, 0, 2 },   /* env2 */
    };

    printf("== SNAPSHOT-FIX-1 S7: env 九读断口 写后读回 ==\n");
    for (int e = 0; e < 3; e++) {
        for (int k = 0; k < 3; k++) {
            char name[64];
            snprintf(name, sizeof(name), "env%d.%s", e, subs[k]);
            float w = (float)vals[e][k];
            float r = -999.0f;
            int rc_w = kcapi_set_param(s, name, w);
            int rc_r = kcapi_get_param(s, name, &r);
            CHECK(rc_w == 0 && rc_r == 0 && r == w,
                  "S7 %s 写 %.0f 读回 %.0f (rc_w=%d rc_r=%d)",
                  name, (double)w, (double)r, rc_w, rc_r);
        }
    }

    printf("== 口径交叉: 端口原名 sustain_point_index 与别名同字段 ==\n");
    for (int e = 0; e < 3; e++) {
        char alias[64], orig[64];
        snprintf(alias, sizeof(alias), "env%d.sustain_point", e);
        snprintf(orig, sizeof(orig), "env%d.sustain_point_index", e);
        float via_alias = -1.0f, via_port = -1.0f;
        int rc_a = kcapi_get_param(s, alias, &via_alias);
        int rc_p = kcapi_get_param(s, orig, &via_port);
        CHECK(rc_a == 0 && rc_p == 0 && via_alias == via_port,
              "S7 %s==%s: %.0f==%.0f", alias, orig,
              (double)via_alias, (double)via_port);
    }

    printf("%d 项检查, %d 项 FAIL\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
