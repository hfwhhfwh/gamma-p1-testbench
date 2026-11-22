// midi_route_test.cpp — LEGATO-FIX-1 WP6 验收: MIDI note_off 路由修复
// (不进构建系统, 手工编译, 同 log_wire_test.cpp 惯例)
//
// 背景: midi.cpp 修复前, note_on vel=0 (0x90 v=0) 与 note_off (0x80)
// 走 ui_note_off(app) —— synth_ui.cpp:120-124 清全栈 + 无条件
// note_off, 绕过音符栈: 连奏中松开任意一键 (哪怕非发声键) 都会把
// gate 拉 0, 第二个音被掐。修复后走 ui_note_off_note(app, note)
// (synth_ui.cpp:132 既有函数, 与 PC 键盘 keyboard.cpp 同一路由):
// 松开非栈顶键只出栈不发事件。
//
// 口径差异 (不一致项登记): 测试台是 RtMidi 桌面 app, 无头环境无真实
// MIDI 设备, midi_poll 无法被驱动; 本驱动直接调 ui_* 函数 ——
// ui_note_off_note(app, note) 即修复后 midi.cpp 的 0x80/vel=0 路由
// 目标 (逐字同一调用), ui_note_off 为修复前路由 (对照组)。
//
// 检查:
//  M1 修复后路由: ui_note_on(60)->ui_note_on(64)->ui_note_off_note(60)
//     (松开非栈顶 key1) -> gate 仍 1, 渲染 RMS 持续 >1e-3, alog 无
//     note_off 行;
//  M2 对照组: 同序列但走 ui_note_off (修复前路由) -> gate==0,
//     alog 有 note_off 行, 渲染随后归于无声。
//  M3 栈顶替换: on(60)->on(64)->ui_note_off_note(64) (松栈顶) ->
//     回退到 60 (note==60, gate==1, 持续发声)。
//
// 运行: 在 gamma-p1-testbench/tests/ 下 ./midi_route_test
//   (日志写 midi_route_out/, 目录须已存在; alog 追加模式, 重跑前
//   须删 midi_route_out/testbench_action.log)
//
// 变更摘要 (2026-10-25 TBENCH-INFRA-1):
//   P-56: M1/M2 的 alog note_off 计数由全文累计改为本次运行增量
//   (alog_start 前记录已有行数, count_note_off 跳过基线行) —— 连跑
//   两次结果一致, 不再依赖重跑前删日志; :105 清场保留, 双重保险。

#include "../src/synth_ui.h"
#include "../src/action_log.h"

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

#define BLK 64
#define WIN 256

/* 渲染 dur_s 秒, 返回尾 256 采样窗 RMS (double 累加器, 同内核测试先例) */
static double render_rms_tail(kcapi_synth_t *s, double dur_s)
{
    float buf[BLK], acc[WIN];
    int ai = 0;
    double last = 0.0;
    long total = (long)(dur_s * 48000.0);
    for (long sp = 0; sp < total; sp += BLK) {
        kcapi_render_block(s, buf, BLK);
        std::memcpy(acc + ai, buf, sizeof(buf));
        ai += BLK;
        if (ai >= WIN) {
            double sum = 0.0;
            for (int i = 0; i < WIN; i++)
                sum += (double)acc[i] * (double)acc[i];
            last = std::sqrt(sum / (double)WIN);
            ai = 0;
        }
    }
    return last;
}

static float read_gate(AppState *app)
{
    float g = -1.0f;
    kcapi_get_param(app->synth, "voice.gate", &g);   /* READ 口读回 */
    return g;
}

/* UI 侧当前发声键 (音符栈栈顶; 内核 voice.note 的 READ 口是归一化
 * 调制源 (note-60)/48, 不是音符号, 故音符判定走 UI 状态) */

/* 统计日志总行数 (文件不存在返回 0) */   // TBENCH-INFRA-1: P-56
static long log_line_count(const char *path)
{
    FILE *f = std::fopen(path, "r");
    if (!f) return 0;
    char line[512];
    long n = 0;
    while (std::fgets(line, sizeof(line), f)) n++;
    std::fclose(f);
    return n;
}

/* 统计日志中 note_off 行数 (skip_lines 之前的历史行不计,
 * 只算本次运行增量) */   // TBENCH-INFRA-1: P-56 全文累计 -> 增量
static int count_note_off(const char *path, long skip_lines)
{
    FILE *f = std::fopen(path, "r");
    if (!f) return skip_lines > 0 ? -1 : 0;
    char line[512];
    long nline = 0;
    int n = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (++nline <= skip_lines) continue;
        char op[32], cat[32];
        if (std::sscanf(line, "%*d | %31[^|] | %31[^|] |", op, cat) == 2) {
            /* op 段去首尾空格后比对 */
            char *o = op + std::strspn(op, " ");
            std::size_t L = std::strlen(o);
            while (L > 0 && o[L-1] == ' ') o[--L] = '\0';
            if (std::strcmp(o, "note_off") == 0) n++;
        }
    }
    std::fclose(f);
    return n;
}

int main(void)
{
    const char *logpath = "midi_route_out/testbench_action.log";
    std::remove(logpath);          /* alog 追加模式: 重跑前清场 */
    std::printf("== LEGATO-FIX-1 WP6: MIDI note_off 路由修复验收 ==\n\n");

    /* ---- M1: 修复后路由 (ui_note_off_note = midi.cpp 0x80/vel=0 现状) */
    {
        AppState app;
        long base1 = log_line_count(logpath);   // TBENCH-INFRA-1: P-56 增量基线
        alog_start(logpath);       /* 与 main.cpp 同: 先于 ui_init */
        ui_init(&app);             /* 内含默认接线 env0.value->vca.level */
        ui_note_on(&app, 60, 100);
        ui_note_on(&app, 64, 100);
        ui_note_off_note(&app, 60);   /* 松开非栈顶 key1 */
        float g = read_gate(&app);
        int n = app.current_note;
        double rms = render_rms_tail(app.synth, 0.5);
        std::printf("  信息  M1: 松非栈顶 60 后 gate=%.0f note=%d, "
                    "再渲染 0.5s 尾窗 RMS=%.6f\n", (double)g, n, rms);
        alog_stop();
        int noff = count_note_off(logpath, base1);   // TBENCH-INFRA-1: P-56 增量
        CHECK(g == 1.0f && n == 64 && rms > 1e-3 && noff == 0,
              "M1: 修复后路由 —— 松非栈顶键 gate 仍 1, note 恒 64, "
              "RMS %.4f>1e-3 (持续发声), alog note_off 行 %d==0",
              rms, noff);
        ui_shutdown(&app);
    }

    /* ---- M2: 对照组 (ui_note_off = 修复前路由) ---- */
    {
        AppState app;
        long base2 = log_line_count(logpath);   // TBENCH-INFRA-1: P-56 增量基线
        alog_start(logpath);
        ui_init(&app);
        ui_note_on(&app, 60, 100);
        ui_note_on(&app, 64, 100);
        ui_note_off(&app);            /* 修复前路由: 清全栈+无条件 off */
        float g = read_gate(&app);
        double rms = render_rms_tail(app.synth, 1.0);
        std::printf("  信息  M2: 对照组 ui_note_off 后 gate=%.0f, "
                    "再渲染 1.0s 尾窗 RMS=%.6f\n", (double)g, rms);
        alog_stop();
        int noff = count_note_off(logpath, base2);   // TBENCH-INFRA-1: P-56 增量
        CHECK(g == 0.0f && rms < 1e-3 && noff == 1,
              "M2: 对照组 (修复前路由) —— gate 被拉 0, 1.0s 后归于无声 "
              "(RMS %.2e<1e-3), alog note_off 行 %d==1", rms, noff);
        ui_shutdown(&app);
    }

    /* ---- M3: 栈顶替换 (松栈顶回退到仍按住键) ---- */
    {
        AppState app;
        alog_start(logpath);
        ui_init(&app);
        ui_note_on(&app, 60, 100);
        ui_note_on(&app, 64, 100);
        ui_note_off_note(&app, 64);   /* 松栈顶 key2 -> 回退 60 */
        float g = read_gate(&app);
        int n = app.current_note;
        double rms = render_rms_tail(app.synth, 0.5);
        std::printf("  信息  M3: 松栈顶 64 后 gate=%.0f note=%d, "
                    "再渲染 0.5s 尾窗 RMS=%.6f\n", (double)g, n, rms);
        alog_stop();
        CHECK(g == 1.0f && n == 60 && rms > 1e-3,
              "M3: 松栈顶键 —— 音符栈回退 note=60, gate 仍 1, "
              "RMS %.4f>1e-3 (legato 回退发声)", rms);
        ui_shutdown(&app);
    }

    std::printf("\n== 汇总: %d 项检查, %d 项 FAIL ==\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
