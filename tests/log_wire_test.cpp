// log_wire_test.cpp — TBENCH-LOG-WIRE WP3 自证 (无头驱动真实调用链)
//
// 走真实常驻接线代码路径 (ui_set_param/ui_note_on/ui_cable_* /
// ui_save_preset/ui_load_preset —— 即测试台 UI 旋钮/键盘/连线/preset
// 按钮的唯一下游), 产生格式 v1 日志并逐行核对; 再开第二会话验证
// 追加不覆盖。
//
// 口径差异 (不一致项登记): 测试台是 imgui 桌面 app, 无法真点击;
// 本驱动直接调 ui_* 函数 (与面板按钮同一批函数), 非测试桩。
// 引擎切换的面板调用点 (panel_engine 的 alog_engine) 被 ImGui 绑定,
// 无头环境直接调 alog_engine (与面板现调用的是同一包装)。
//
// 用法: 在 gamma-p1/tests/ 下运行 (日志写 log_wire_out/, 目录须已存在)
//
// 变更摘要 (2026-10-07 TBENCH-FIX-3):
//   P-16: alog_start 前加 std::remove(logpath) 清场 (照
//   midi_route_test.cpp:105 先例; alog 追加模式, 原二次运行
//   nsessions==4 撞 :162 写死判据 ==2, 不可重跑)。:162 判据
//   nsessions==2 不变, 清场后语义恢复正确。
//
// 变更摘要 (2026-10-25 TBENCH-INFRA-1):
//   P-56: line/buf 256 -> 1024 (现行最长日志行 94 字符, 余量充足);
//   fgets 循环内新增截断检测 (行尾无 '\n' 且未 EOF = 缓冲不足),
//   作为新 CHECK "WP3f" 计入汇总 —— 汇总基线 7 项 -> 8 项。

#include "synth_ui.h"
#include "action_log.h"

#include <cstdio>
#include <cstring>
#include <cmath>

using namespace gp1_ui;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { std::printf("  PASS "); std::printf(__VA_ARGS__); } \
    else      { std::printf("  FAIL "); std::printf(__VA_ARGS__); g_fails++; } \
    std::printf("\n");                                           \
} while (0)

int main(void)
{
    const char *logpath = "log_wire_out/testbench_action.log";
    std::remove(logpath);          /* TBENCH-FIX-3: P-16 alog 追加模式, 重跑前清场 (照 midi_route_test.cpp:105 先例) */

    /* ---- 会话 1: 22+ 操作走真实调用链 ---- */
    AppState app;
    alog_start(logpath);          /* 与 main.cpp 同: 先于 ui_init */
    ui_init(&app);                /* 内含默认接线 (会被记录) */

    /* 参数 ×6 (含 fm. 新口/PEG/env/未知名) */
    ui_set_param(&app, "fm.peg_d1l", 70.0f);
    ui_set_param(&app, "fm.op0.r4", 42.0f);
    ui_set_param(&app, "env0.release", 2.0f);
    ui_set_param(&app, "vca.level", 0.5f);
    ui_set_param(&app, "lfo0.frequency", 3.0f);
    ui_set_param(&app, "fm.not_exist", 1.0f);   /* 未知名 -> param+warn 两行 */

    /* 音符 ×4 */
    ui_note_on(&app, 60, 100);
    ui_note_off(&app);
    ui_note_on(&app, 64, 90);
    ui_note_off_note(&app, 64);

    /* 连线 ×3 (2 连 1 断) */
    int id1 = ui_cable_ctrl_connect(&app, "lfo0.out", "vcf.cutoff", 0.3f);
    ui_cable_ctrl_connect(&app, "env0.value", "vca.level", 0.8f);
    ui_cable_disconnect_ctrl(&app, id1);

    /* 引擎 ×2 (面板调用点所调的同一包装) */
    alog_engine(app.synth, 0, 1, "user");
    alog_engine(app.synth, 1, 0, "user");

    /* preset 存取 ×2 */
    bool saved = ui_save_preset(&app, "log_wire_out/wp3.snp");
    bool loaded = ui_load_preset(&app, "log_wire_out/wp3.snp");

    /* 心跳摘要 ×1 */
    alog_state(app.synth, "user");

    alog_stop();
    ui_shutdown(&app);
    CHECK(saved && loaded, "WP3: preset 存/取成功");

    /* ---- 逐行核对 ---- */
    FILE *f = std::fopen(logpath, "r");
    CHECK(f != NULL, "WP3: 日志文件存在");
    int nops = 0, mono_ok = 1, fmt_ok = 1, fields_ok = 1;
    int n_param = 0, n_note_on = 0, n_note_off = 0, n_cablep = 0,
        n_cablem = 0, n_engine = 0, n_preset = 0, n_warn = 0, n_state = 0;
    long t_prev = -1;
    char line[1024];   // TBENCH-INFRA-1: P-56 256 -> 1024 (防日志行超缓冲)
    int found_peg_oldnew = 0, found_r4_oldnew = 0;
    int truncated = 0;   // TBENCH-INFRA-1: P-56 截断检测
    while (f && std::fgets(line, sizeof(line), f)) {
        // TBENCH-INFRA-1: P-56 行尾无 '\n' 且未 EOF = 缓冲不足 -> 截断
        if (!std::strchr(line, '\n') && !std::feof(f)) truncated = 1;
        if (line[0] == '#') continue;
        if (!std::strchr(line, '|')) continue;
        /* split('|') 机读: 5 段 */
        char buf[1024];   // TBENCH-INFRA-1: P-56 随 line 同步放大
        std::snprintf(buf, sizeof(buf), "%s", line);
        char *fld[5];
        int nf = 0;
        char *p = buf;
        fld[nf++] = p;
        for (char *q = buf; *q && nf < 5; q++)
            if (*q == '|') { *q = '\0'; fld[nf++] = q + 1; }
        if (nf != 5) { fmt_ok = 0; continue; }
        for (int i = 0; i < 5; i++) {
            while (*fld[i] == ' ') std::memmove(fld[i], fld[i] + 1, std::strlen(fld[i]));
        }
        long t = std::atol(fld[0]);
        if (t < t_prev) mono_ok = 0;
        t_prev = t;
        nops++;
        if (std::strncmp(fld[1], "param", 5) == 0) n_param++;
        else if (std::strncmp(fld[1], "note_on", 7) == 0) n_note_on++;
        else if (std::strncmp(fld[1], "note_off", 8) == 0) n_note_off++;
        else if (std::strncmp(fld[1], "cable+", 6) == 0) n_cablep++;
        else if (std::strncmp(fld[1], "cable-", 6) == 0) n_cablem++;
        else if (std::strncmp(fld[1], "engine", 6) == 0) n_engine++;
        else if (std::strncmp(fld[1], "preset_", 7) == 0) n_preset++;
        else if (std::strncmp(fld[1], "warn", 4) == 0) n_warn++;
        else if (std::strncmp(fld[1], "state", 5) == 0) n_state++;
        else fields_ok = 0;
        /* 旧值正确性抽查 (kcapi_get_param 真旧值); 注意 buf 已被
         * 就地切分只剩段 0, 名称匹配用原始行 line */
        if (std::strstr(line, "fm.peg_d1l") && std::strstr(line, "50 -> 70"))
            found_peg_oldnew = 1;
        if (std::strstr(line, "fm.op0.r4") && std::strstr(line, "99 -> 42"))
            found_r4_oldnew = 1;
    }
    if (f) std::fclose(f);
    // TBENCH-INFRA-1: P-56 新增判据 WP3f —— 汇总基线 7 -> 8 项
    CHECK(!truncated,
          "WP3f: 无日志行超缓冲 (截断检测: 行尾无换行且未 EOF 即超缓冲)");
    std::printf("  信息  会话1: 日志条数=%d (param=%d note_on=%d note_off=%d "
                "cable+=%d cable-=%d engine=%d preset=%d warn=%d state=%d)\n",
                nops, n_param, n_note_on, n_note_off, n_cablep, n_cablem,
                n_engine, n_preset, n_warn, n_state);
    CHECK(nops >= 20, "WP3a: 日志条数 %d >= 20", nops);
    CHECK(n_param >= 6 && n_note_on >= 2 && n_note_off >= 2 &&
          n_cablep >= 2 && n_cablem >= 1 && n_engine >= 2 &&
          n_preset >= 2 && n_warn >= 1 && n_state >= 1,
          "WP3b: 各类操作计数达标 (含 param/note/cable/engine/preset/"
          "warn/state)");
    CHECK(mono_ok && fmt_ok && fields_ok,
          "WP3c: 时间单调 + split('|') 5 段 + op 全部合法");
    CHECK(found_peg_oldnew && found_r4_oldnew,
          "WP3d: 旧值=真实旧值 (fm.peg_d1l \"50 -> 70\", "
          "fm.op0.r4 \"99 -> 42\")");

    /* ---- 会话 2: 追加不覆盖 ---- */
    long size1 = 0;
    f = std::fopen(logpath, "r");
    if (f) { std::fseek(f, 0, SEEK_END); size1 = std::ftell(f); std::fclose(f); }
    {
        AppState app2;
        alog_start(logpath);
        ui_init(&app2);
        ui_set_param(&app2, "fm.op0.r4", 70.0f);
        alog_stop();
        ui_shutdown(&app2);
    }
    f = std::fopen(logpath, "r");
    int nsessions = 0;
    long size2 = 0;
    if (f) {
        while (std::fgets(line, sizeof(line), f))
            if (std::strstr(line, "# session_start")) nsessions++;
        std::fseek(f, 0, SEEK_END);
        size2 = std::ftell(f);
        std::fclose(f);
    }
    std::printf("  信息  追加验证: 会话数=%d, 大小 %ld -> %ld\n",
                nsessions, size1, size2);
    CHECK(nsessions == 2 && size2 > size1,
          "WP3e: 二次启动追加新 session 头, 历史不覆盖");

    std::printf("\n== 汇总: %d 项检查, %d 项 FAIL ==\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
