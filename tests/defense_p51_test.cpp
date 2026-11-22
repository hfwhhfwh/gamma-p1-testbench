/* DEFENSE-1: P-51 UI 显示越界双保险 —— 显示层探针自证件 (WP3)
 *
 * 模拟 engine_mode=99 / algorithm=99 / algorithm=0 注入 snap_cache 的显示
 * 口径, 直链 ui_panels.cpp 的两个界检查辅助 (defense1_engine_mode_name /
 * defense1_algo_slider_prep, 外部链接, 不实例化 ImGui, 不弹窗)。
 *
 * 断言 (0 FAIL 为通过):
 *   D1  engine_mode 0..3 名字逐字等于 {SUB_ONLY, FM_ONLY, SERIES, PARALLEL}
 *       (合法路径显示口径不变)
 *   D2  engine_mode = 99 / -1 / 4 → 返 NULL (不越界读名字数组, 调用侧显 "?")
 *   D3  algorithm 1..32  (自定义)  in_range=true,  display=algorithm-1
 *   D4  algorithm 33~64  (DX7 合法) in_range=true,  display=algorithm-1
 *       (Slider 量程 0..FM_NUM_ALGOS-1=0..63 下不再越程)
 *   D5  algorithm=99 → in_range=false, display 钳 63 (FM_NUM_ALGOS-1)
 *   D6  algorithm=0  → in_range=false, display 钳 0 (原 algo=-1 越下界口径)
 *   D7  FM_NUM_ALGOS==64 (fm.h:127, 与 ui_panels.cpp 同一宏口径)
 *
 * 只读辅助函数, 无任何写入路径 (写入语义不在本件覆盖, 由规格 S8 保证不动)。
 * 构建运行: cd tests && bash build_tbfix3_tests.sh && ./defense_p51_test
 */
#include <cstdio>
#include <cstring>

#include "fm.h" /* FM_NUM_ALGOS (fm.h:127) */

namespace gp1_ui {
/* 与 src/ui_panels.cpp 顶部 DEFENSE-1 辅助同签名 (直链该 TU) */
extern const char *defense1_engine_mode_name(float engine_mode);
extern bool defense1_algo_slider_prep(float algorithm, int *display_algo);
}

using gp1_ui::defense1_algo_slider_prep;
using gp1_ui::defense1_engine_mode_name;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char *label)
{
    if (ok) { g_pass++; printf("PASS  %s\n", label); }
    else    { g_fail++; printf("FAIL  %s\n", label); }
}

int main(void)
{
    static const char *kExpectedModes[] = {"SUB_ONLY", "FM_ONLY", "SERIES", "PARALLEL"};

    /* D1: 合法 engine_mode 显示口径逐字不变 */
    for (int i = 0; i < 4; i++) {
        char label[64];
        std::snprintf(label, sizeof(label), "D1 engine_mode=%d -> \"%s\"", i, kExpectedModes[i]);
        const char *name = defense1_engine_mode_name((float)i);
        check(name && std::strcmp(name, kExpectedModes[i]) == 0, label);
    }

    /* D2: 越界 engine_mode 返 NULL (不索引名字数组, 调用侧显 "?") */
    check(defense1_engine_mode_name(99.0f) == nullptr, "D2 engine_mode=99 -> NULL");
    check(defense1_engine_mode_name(-1.0f) == nullptr, "D2 engine_mode=-1 -> NULL");
    check(defense1_engine_mode_name(4.0f)  == nullptr, "D2 engine_mode=4 -> NULL");

    /* D3/D4: 合法 algorithm (1..FM_NUM_ALGOS) in_range, display=algorithm-1 */
    const int legal[] = {1, 2, 17, 32, 33, 48, 63, 64};
    for (size_t i = 0; i < sizeof(legal) / sizeof(legal[0]); i++) {
        int display = -999;
        const bool in_range = defense1_algo_slider_prep((float)legal[i], &display);
        char label[96];
        std::snprintf(label, sizeof(label),
                      "D%c algorithm=%d -> in_range + display=%d",
                      (legal[i] <= 32) ? '3' : '4', legal[i], legal[i] - 1);
        check(in_range && display == legal[i] - 1, label);
    }

    /* D5: algorithm=99 越上界 → in_range=false, display 钳 63 */
    {
        int display = -999;
        const bool in_range = defense1_algo_slider_prep(99.0f, &display);
        check(!in_range && display == FM_NUM_ALGOS - 1,
              "D5 algorithm=99 -> out-of-range, display clamped to FM_NUM_ALGOS-1");
    }

    /* D6: algorithm=0 → algo=-1 越下界 → in_range=false, display 钳 0 */
    {
        int display = -999;
        const bool in_range = defense1_algo_slider_prep(0.0f, &display);
        check(!in_range && display == 0,
              "D6 algorithm=0 -> out-of-range, display clamped to 0");
    }

    /* D7: 宏口径与内核 fm.h 一致 */
    check(FM_NUM_ALGOS == 64, "D7 FM_NUM_ALGOS == 64 (fm.h:127)");

    printf("----\nDEFENSE-1 P-51 显示层探针: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
