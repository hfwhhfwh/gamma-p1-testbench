// snapshot_ui_test.cpp — SNAPSHOT-FIX-1 S4/S5 UI 路径 headless 自证
// (测试台侧 WP3 自证件 5/6, 交付物)
//
// 走真实 UI preset 调用链 (ui_save_preset / ui_load_preset —— 面板
// preset 按钮的唯一下游), 验证:
//   T1 (S5): ui_save_preset 写出 5740B V5 文件 —— 头 4 字节
//      'G','P','1','S'、[4:6) version=5 小端 (字节 4==5, 字节 5==0)。
//   T2 (S4+S5 往返): 关键参数 (fm.algorithm / voice.engine_mode) 写入
//      -> 存 -> 改乱 -> ui_load_preset 读回一致; 成功才记 alog。
//   T3: V1 夹具 (gamma-p1/fm/dx7_out/preset_v1.snp 复制件, 原件不动)
//      经 ui_load_preset 加载成功, 关键参数读回正确 (重存 V5 ->
//      第二实例再载, 双实例同值)。
//   T4: V3 夹具 (gamma-p1/tests/fuzz_run1/sessionA.snp 复制件) 同上。
//   T5: 垃圾文件加载失败且不污染现状态 —— 5740B 伪 V5 (magic/version
//      对但 CRC 错) 与 5733B 未知尺寸两例: ui_load_preset 返回 false,
//      现状态参数读回不变, 且 alog 无对应 preset_load 行 (失败不记)。
//   T6: alog 全程 preset_load 计数与文件路径核对。
//
// 已知口径声明 (偏差): 恰好 5732B 的 V4 裸垃圾 blob 仍会被
// synth_restore_blob 的 V4 分支接受 (V4 无完整性校验, 内核侧设计,
// 本任务只许动测试台), 故 T5 用 5740B 坏 CRC 与 5733B 未知尺寸两例
// 覆盖"垃圾加载失败"路径。
//
// 用法: cd gamma-p1-testbench/tests && ./snapshot_ui_test
// 产物: sfix1_out/*.snp + sfix1_out/sfix1_action.log (目录须已存在)

#include "synth_ui.h"
#include "action_log.h"
#include "kernel_capi.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace gp1_ui;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { std::printf("  PASS "); std::printf(__VA_ARGS__); } \
    else      { std::printf("  FAIL "); std::printf(__VA_ARGS__); g_fails++; } \
    std::printf("\n");                                           \
} while (0)

/* 复制夹具到 sfix1_out/ (原件只读, 一律不碰) */
static bool copy_file(const char *src, const char *dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) return false;
    std::vector<char> buf((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    std::ofstream out(dst, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(buf.data(), (std::streamsize)buf.size());
    return out.good();
}

static long file_size(const char *path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return -1;
    return (long)f.tellg();
}

static float get_param(kcapi_synth_t *s, const char *name)
{
    float v = -999.0f;
    if (kcapi_get_param(s, name, &v) != 0) return -999.0f;
    return v;
}

/* T3/T4 共用: 夹具复制件加载 + 关键参数读回正确性 (重存 V5 -> 第二
 * 实例再载, 双实例 fm.algorithm 同值) */
static void fixture_check(const char *copied, const char *resaved,
                          AppState *app, const char *tag)
{
    bool ok = ui_load_preset(app, copied);
    float algo = get_param(app->synth, "fm.algorithm");
    float eng  = get_param(app->synth, "voice.engine_mode");
    CHECK(ok, "%s: ui_load_preset 成功", tag);
    CHECK(algo >= 0.0f && algo < 32.0f, "%s: fm.algorithm=%.0f 读回 (合理域)", tag, (double)algo);
    CHECK(eng >= 0.0f && eng < 4.0f, "%s: voice.engine_mode=%.0f 读回 (合理域)", tag, (double)eng);

    bool ok_save = ui_save_preset(app, resaved);
    CHECK(ok_save, "%s: 重存 V5 成功", tag);

    AppState app2;
    ui_init(&app2);
    bool ok2 = ui_load_preset(&app2, resaved);
    float algo2 = get_param(app2.synth, "fm.algorithm");
    CHECK(ok2 && algo2 == algo, "%s: 第二实例载 V5 algo 一致 (%.0f==%.0f)",
          tag, (double)algo2, (double)algo);
    ui_shutdown(&app2);
}

int main(void)
{
    const char *logpath = "sfix1_out/sfix1_action.log";
    std::remove(logpath);

    AppState app;
    alog_start(logpath);
    ui_init(&app);

    /* ---- T1+T2: V5 保存规格 + UI 往返 ---- */
    ui_set_param(&app, "fm.algorithm", 5.0f);
    ui_set_param(&app, "voice.engine_mode", 1.0f);
    bool saved = ui_save_preset(&app, "sfix1_out/roundtrip_v5.snp");
    CHECK(saved, "T1: ui_save_preset 成功");
    long sz = file_size("sfix1_out/roundtrip_v5.snp");
    CHECK(sz == 5740, "T1: 文件尺寸 5740 (实际 %ld)", sz);

    std::ifstream f("sfix1_out/roundtrip_v5.snp", std::ios::binary);
    std::vector<char> head(8);
    f.read(head.data(), 8);
    f.close();
    CHECK(head[0] == 'G' && head[1] == 'P' && head[2] == '1' && head[3] == 'S',
          "T1: 头 4 字节 'GP1S'");
    CHECK((unsigned char)head[4] == 5 && (unsigned char)head[5] == 0,
          "T1: version 字段=5 (小端, [4]=5 [5]=0)");

    ui_set_param(&app, "fm.algorithm", 1.0f);
    ui_set_param(&app, "voice.engine_mode", 0.0f);
    bool loaded = ui_load_preset(&app, "sfix1_out/roundtrip_v5.snp");
    CHECK(loaded, "T2: ui_load_preset (V5) 成功");
    CHECK(get_param(app.synth, "fm.algorithm") == 5.0f &&
          get_param(app.synth, "voice.engine_mode") == 1.0f,
          "T2: 关键参数读回一致 (algo=5 engine=1)");

    /* ---- T3: V1 夹具 (复制件) ---- */
    CHECK(copy_file("../../gamma-p1/fm/dx7_out/preset_v1.snp", "sfix1_out/tmp_v1.snp"),
          "T3: V1 夹具复制件就位");
    fixture_check("sfix1_out/tmp_v1.snp", "sfix1_out/tmp_v1_resave.snp",
                  &app, "T3");

    /* ---- T4: V3 夹具 (复制件) ---- */
    CHECK(copy_file("../../gamma-p1/tests/fuzz_run1/sessionA.snp", "sfix1_out/tmp_v3.snp"),
          "T4: V3 夹具复制件就位");
    fixture_check("sfix1_out/tmp_v3.snp", "sfix1_out/tmp_v3_resave.snp",
                  &app, "T4");

    /* ---- T5: 垃圾加载失败不污染现状态 ---- */
    ui_set_param(&app, "fm.algorithm", 7.0f);
    {
        /* 5740B 伪 V5: magic/version 对, 主体全零 -> CRC 必不符 */
        std::vector<char> junk(5740, 0);
        junk[0] = 'G'; junk[1] = 'P'; junk[2] = '1'; junk[3] = 'S';
        junk[4] = 5;
        std::ofstream jf("sfix1_out/garbage_bad_crc.snp", std::ios::binary);
        jf.write(junk.data(), (std::streamsize)junk.size());
        jf.close();
        bool bad = ui_load_preset(&app, "sfix1_out/garbage_bad_crc.snp");
        CHECK(!bad, "T5a: 5740B 坏 CRC 伪 V5 加载失败");
        CHECK(get_param(app.synth, "fm.algorithm") == 7.0f,
              "T5a: 失败后现状态未被污染 (algo 仍 7)");
    }
    {
        std::vector<char> junk(5733, static_cast<char>(-86));
        std::ofstream jf("sfix1_out/garbage_bad_size.snp", std::ios::binary);
        jf.write(junk.data(), (std::streamsize)junk.size());
        jf.close();
        bool bad = ui_load_preset(&app, "sfix1_out/garbage_bad_size.snp");
        CHECK(!bad, "T5b: 5733B 未知尺寸垃圾加载失败");
        CHECK(get_param(app.synth, "fm.algorithm") == 7.0f,
              "T5b: 失败后现状态未被污染 (algo 仍 7)");
    }

    /* ---- T6: alog 计数/路径核对 (失败不记 preset_load) ---- */
    alog_stop();
    FILE *lf = std::fopen(logpath, "r");
    int n_load = 0, n_save = 0, n_garbage = 0;
    char line[512];
    while (lf && std::fgets(line, sizeof(line), lf)) {
        if (std::strstr(line, "preset_load")) n_load++;
        if (std::strstr(line, "preset_save")) n_save++;
        if (std::strstr(line, "garbage_bad_crc") || std::strstr(line, "garbage_bad_size"))
            n_garbage++;
    }
    if (lf) std::fclose(lf);
    CHECK(n_load == 5, "T6: alog preset_load 恰 5 行 (成功 5 次; 实际 %d)", n_load);
    CHECK(n_save == 3, "T6: alog preset_save 恰 3 行 (实际 %d)", n_save);
    CHECK(n_garbage == 0, "T6: 垃圾路径零 preset_load 行 (失败不记; 实际 %d)", n_garbage);

    ui_shutdown(&app);
    std::printf("%d 项检查, %d 项 FAIL\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
