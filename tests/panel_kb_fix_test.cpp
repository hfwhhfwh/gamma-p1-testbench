// panel_kb_fix_test.cpp — TBENCH-FIX-3 T-E: 键盘面板挂音 (P-36) 逻辑级
// 回归 + P-07 vco_mix 滑条写名验收 + 修复源码守卫
// (不进构建系统, 手工编译, 同 midi_route_test.cpp 惯例)
//
// 口径差异 (不一致项登记): panel_keyboard.cpp / ui_panels.cpp 被 ImGui
// 绑定, 无头环境无法真点击; 本测试
//   (a) 逻辑级模拟修复后 Hide/Octave 处理序列 —— 与 panel_keyboard.cpp
//       三处修复点逐字同一调用 (ui_note_off_note(app, mouse_key) 后
//       置 -1), 验证该序列在真实音符栈上消除挂音, 并对照修复前序列
//       (直接置 -1) 证明缺陷真实存在;
//   (b) P-07 走真实 ui_set_param 写 voice.vco_mix0/1/2 (即修复后滑条
//       生成的写名), 读回一致且 stderr 零 "未知参数名" 行;
//   (c) 源码守卫: 修复标记原位 grep 审计 (三处 P-36 补 off 点 / P-07
//       写名生成 / P-14/P-15 登记), 防回退。
//
// 运行: 在 gamma-p1-testbench/tests/ 下 ./panel_kb_fix_test
//   (产物写 tbfix3_out/, 目录须已存在)
//
// 变更摘要 (2026-10-25 TBENCH-INFRA-1):
//   P-35: E3 两处 freopen 加固 —— 捕获返回值检查 (失败 fprintf(stderr)
//   报错 + E3 判据置 SKIP, 不计 PASS 不算 FAIL, 汇总行单列 SKIP 数);
//   /dev/tty 恢复失败保持重定向不动 + 末尾 "WARN: stderr 未能恢复
//   重定向" 警示。全 tty 环境 SKIP==0, 既有 4 项基线不变。
//
// 变更摘要 (2026-11-17 TBENCH-WIN-1 WP3a):
//   E3 的 freopen("/dev/tty") 改走平台层 plat_reopen_stderr_tty
//   (Windows 为 CONOUT$; POSIX 侧即原调用直转调); 捕获/恢复判据口径
//   与 SKIP/WARN 行为不变。

#include "../src/synth_ui.h"
#include "../src/action_log.h"
#include "../src/platform.h"   // TBENCH-WIN-1: /dev/tty 封装

#include <cstdio>
#include <cstring>
#include <cmath>

using namespace gp1_ui;

static int g_checks = 0, g_fails = 0;
// TBENCH-INFRA-1: P-35 skip 计数与 /dev/tty 恢复失败标记
static int g_skips = 0;
static int g_stderr_restored = 1;
#define SKIP_CHECK(name) do {                                    \
    g_skips++;                                                   \
    std::printf("  SKIP %s: stderr 捕获不可用\n", name);         \
} while (0)
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { std::printf("  PASS "); std::printf(__VA_ARGS__); } \
    else      { std::printf("  FAIL "); std::printf(__VA_ARGS__); g_fails++; } \
    std::printf("\n");                                           \
} while (0)

/* 统计文本文件中某子串出现次数 (源码守卫用) */
static int count_substr(const char *path, const char *needle)
{
    FILE *f = std::fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int n = 0;
    const size_t nl = std::strlen(needle);
    while (std::fgets(line, sizeof(line), f)) {
        for (char *p = line; (p = std::strstr(p, needle)) != NULL; p += nl)
            n++;
    }
    std::fclose(f);
    return n;
}

int main(void)
{
    const char *logpath = "tbfix3_out/panel_kb_fix_actions.log";
    std::remove(logpath);          /* alog 追加模式: 重跑前清场 */
    std::printf("== TBENCH-FIX-3 T-E: 键盘面板挂音 (P-36) + P-07 验收 ==\n\n");

    /* ---- E1: P-36 修复后序列 —— Hide/Octave 置 -1 前补 off ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        /* 鼠标按住琴键 (panel_keyboard 非 Hold 路径的 ui_ 调用) */
        ui_note_on(&app, 60, 100);
        int mouse_key = 60;
        /* 修复后 Hide/Octave 处理序列 (与 panel_keyboard.cpp 三处
         * TBENCH-FIX-3: P-36 修复点逐字同一调用) */
        if (mouse_key >= 0) ui_note_off_note(&app, mouse_key);
        mouse_key = -1;
        std::printf("  信息  E1: 修复序列后 held(60)=%d 栈深=%zu "
                    "current_note=%d\n",
                    (int)ui_is_note_held(&app, 60), app.held_notes.size(),
                    app.current_note);
        CHECK(!ui_is_note_held(&app, 60) && app.held_notes.empty() &&
              app.current_note == -1,
              "E1: P-36 —— Hide/Octave 置 -1 前补 off, 原音不挂住");
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- E2: 对照组 —— 修复前序列 (直接置 -1) 证明缺陷真实存在 ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        ui_note_on(&app, 60, 100);
        /* 修复前: 静默丢弃跟踪, 不发 off */
        int mouse_key = -1;
        (void)mouse_key;
        bool stuck = ui_is_note_held(&app, 60);
        std::printf("  信息  E2: 修复前序列 held(60)=%d (挂住)\n", (int)stuck);
        CHECK(stuck, "E2: 对照组 —— 修复前序列音符挂住 (缺陷复现)");
        ui_note_off(&app);   /* 清理 */
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- E3: P-07 —— 三 vco_mix 滑条写名落地 + stderr 零行 ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        /* 修复后滑条生成的写名 (ui_panels.cpp TBENCH-FIX-3: P-07
         * snprintf "voice.vco_mix%d", idx), 写三个区分值 */
        // TBENCH-INFRA-1: P-35 freopen 捕获加固 (返回值检查)
        int cap_ok = 1;
        if (!std::freopen("tbfix3_out/t_e_stderr.txt", "w", stderr)) {
            cap_ok = 0;
            std::fprintf(stderr,
                         "TBENCH-INFRA-1: freopen tbfix3_out/t_e_stderr.txt 失败\n");
        }
        ui_set_param(&app, "voice.vco_mix0", 0.11f);
        ui_set_param(&app, "voice.vco_mix1", 0.55f);
        ui_set_param(&app, "voice.vco_mix2", 0.99f);
        float rb[3] = {-1.0f, -1.0f, -1.0f};
        int rc[3];
        rc[0] = kcapi_get_param(app.synth, "voice.vco_mix0", &rb[0]);
        rc[1] = kcapi_get_param(app.synth, "voice.vco_mix1", &rb[1]);
        rc[2] = kcapi_get_param(app.synth, "voice.vco_mix2", &rb[2]);
        std::fflush(stderr);
        // TBENCH-INFRA-1: P-35 /dev/tty 恢复加固 (失败保持重定向不动);
        // TBENCH-WIN-1: 改走平台层 (Windows 为 CONOUT$)
        if (!plat_reopen_stderr_tty())
            g_stderr_restored = 0;
        long elines = cap_ok ? 0 : -1;
        if (cap_ok) {
            FILE *f = std::fopen("tbfix3_out/t_e_stderr.txt", "r");
            char line[512];
            while (f && std::fgets(line, sizeof(line), f)) elines++;
            if (f) std::fclose(f);
        }
        std::printf("  信息  E3: 读回 %.4g/%.4g/%.4g rc=%d/%d/%d, "
                    "stderr %ld 行\n",
                    (double)rb[0], (double)rb[1], (double)rb[2],
                    rc[0], rc[1], rc[2], elines);
        // TBENCH-INFRA-1: P-35 捕获失败 -> E3 判据置 SKIP
        if (!cap_ok) {
            SKIP_CHECK("E3");
        } else {
        CHECK(rc[0] == 0 && rc[1] == 0 && rc[2] == 0 &&
              std::fabs(rb[0] - 0.11f) < 1e-4f &&
              std::fabs(rb[1] - 0.55f) < 1e-4f &&
              std::fabs(rb[2] - 0.99f) < 1e-4f && elines == 0,
              "E3: P-07 —— 三滑条写名 voice.vco_mix0/1/2 落地且读回一致, "
              "无 \"未知参数名\" stderr (%ld 行)", elines);
        }
        alog_stop();
        ui_shutdown(&app);
    }

    /* ---- E4: 源码守卫 —— 修复标记原位 grep 审计 (防回退) ---- */
    {
        int n_p36 = count_substr("../src/panel_keyboard.cpp",
                                 "TBENCH-FIX-3: P-36 置 -1 前补 off");
        int n_p07 = count_substr("../src/ui_panels.cpp",
                                 "TBENCH-FIX-3: P-07 写名按 idx 生成");
        int n_bare = count_substr("../src/ui_panels.cpp",
                                  "\"voice.vco_mix\", mix");
        int n_p14 = count_substr("../src/keyboard.cpp", "TBENCH-FIX-3: P-14");
        int n_p15 = count_substr("../src/keyboard.cpp", "TBENCH-FIX-3: P-15");
        std::printf("  信息  E4: P-36 补 off 点 %d/3, P-07 写名修复 %d, "
                    "裸名残留 %d, P-14 标记 %d, P-15 标记 %d\n",
                    n_p36, n_p07, n_bare, n_p14, n_p15);
        CHECK(n_p36 == 3 && n_p07 >= 1 && n_bare == 0 &&
              n_p14 >= 1 && n_p15 >= 1,
              "E4: 源码守卫 —— P-36 三处补 off / P-07 裸名清零 / "
              "P-14/P-15 登记标记在位");
    }

    std::printf("\n== 汇总: %d 项检查, %d 项 FAIL, %d 项 SKIP ==\n",
                g_checks, g_fails, g_skips);
    // TBENCH-INFRA-1: P-35 /dev/tty 恢复失败警示 (保持重定向不动)
    if (!g_stderr_restored)
        std::printf("WARN: stderr 未能恢复重定向\n");
    return g_fails ? 1 : 0;
}
