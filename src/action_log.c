/*
 * action_log.c — 测试台操作日志模块实现 (NOISE-DIAG-1 WP-LOG)
 *
 * host 侧代码: 允许 stdio/堆; 全静态环形缓冲 4096 条 + 每 64 条
 * flush + alog_stop flush。格式 v1 冻结 (见 action_log.h 头注释)。
 *
 * 变更摘要 (2026-09-25 TBENCH-FIX-2):
 *   alog_set_param 旧值列口径: kcapi_get_param 读旧值失败时, 若该名
 *   是 DST_PARAM 参数口 (kcapi_is_param_port) 属异常 —— 旧值列记
 *   "?!" (审计标记); READ/AUDIO 口语义无旧值仍记 "?" (正常情形;
 *   经 TBENCH-FIX-2 的 kcapi 读回面扩展后这两类失败都不再伴随
 *   stderr 刷屏)。格式 v1 行结构不变 (仅旧值列内容语义)。
 *
 * 变更摘要 (2026-11-17 LOG-PATH-1):
 *   WP1 (P-28 失败可见化): alog_start fopen 失败打 stderr
 *   ("[alog] 无法打开日志 <路径>: <strerror>" 后 return -1, 签名不变);
 *   成功 realpath 解析绝对路径打 "[alog] logging to <绝对路径>"。
 *   alog_flush_locked/alog_stop 检查 fputs/fflush/fclose 返回值,
 *   首次写盘失败打 "[alog] 写盘失败: <strerror>" (静态 sticky 标志
 *   防刷屏); alog_stop 幂等性不变 (g_fp==NULL 早退), 供 atexit 收口。
 *   日志文件内零新增错误行 (格式 v1 冻结)。
 *
 * 变更摘要 (TBENCH-WIN-1, 2026-11-17):
 *   WP1a 平台层接入: clock_gettime(CLOCK_MONOTONIC)/localtime_r/realpath
 *   三个调用点改走 src/platform.{h,c} (plat_monotonic_ms/plat_localtime/
 *   plat_absolute_path); POSIX 实现即原 API 直转调, Mac 行为逐位不变
 *   (默认即隐身); PATH_MAX 改 PLAT_PATH_MAX (同值)。格式 v1 行结构
 *   与全部判据口径零改动。
 */

#include "action_log.h"
#include "platform.h"   /* TBENCH-WIN-1: POSIX 调用点收进平台层 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- 静态状态 (无堆: 环形缓冲全静态) ---- */

#define ALOG_RING_CAP 4096
#define ALOG_LINE_CAP 192
#define ALOG_FLUSH_EVERY 64

static FILE *g_fp = NULL;
static char  g_ring[ALOG_RING_CAP][ALOG_LINE_CAP];
static int   g_head = 0;      /* 下一条写入位置 */
static int   g_count = 0;     /* 缓冲中未 flush 条数 */
static long  g_t0_ms = 0;     /* 会话起点 (monotonic ms) */
static long  g_lines = 0;     /* 本会话已写总行数 (统计用) */
static int   g_write_failed = 0;  /* LOG-PATH-1: 写盘失败 sticky 标志 (防刷屏) */

static long alog_now_ms(void)
{
    /* TBENCH-WIN-1: clock_gettime 收进平台层 (POSIX 行为逐位不变) */
    return plat_monotonic_ms();
}

int alog_start(const char *path)
{
    if (g_fp) return 0;                 /* 已开会话: 幂等 */
    const char *p = path ? path : "testbench_action.log";
    g_fp = fopen(p, "a");
    if (!g_fp) {
        /* LOG-PATH-1 WP1a: 打开失败可见化 (P-28), 失败不崩溃 */
        fprintf(stderr, "[alog] 无法打开日志 %s: %s\n", p, strerror(errno));
        return -1;
    }
    g_t0_ms = alog_now_ms();
    g_head = g_count = 0;
    g_lines = 0;
    g_write_failed = 0;

    time_t wall = time(NULL);
    struct tm tmv;
    plat_localtime(&wall, &tmv);   /* TBENCH-WIN-1: 原 localtime_r */
    char date[64];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(g_fp, "# gamma-p1 testbench action log v1\n");
    fprintf(g_fp, "# session_start %s sr=%d build=%s %s\n",
            date, (int)KCAPI_SAMPLE_RATE, __DATE__, __TIME__);
    fprintf(g_fp, "# t_ms | op | target | old -> new | source\n");
    fflush(g_fp);
    /* LOG-PATH-1 WP1a: 成功打绝对落点 (便于定位日志写在哪) */
    char rp[PLAT_PATH_MAX];   /* TBENCH-WIN-1: PATH_MAX 收进平台层 */
    if (plat_absolute_path(p, rp, sizeof(rp)))   /* 原 realpath */
        fprintf(stderr, "[alog] logging to %s\n", rp);
    else
        fprintf(stderr, "[alog] logging to %s\n", p);
    return 0;
}

/* 把环形缓冲内容写盘 (不清计数器以外状态) */
static void alog_flush_locked(void)
{
    if (!g_fp) return;
    int start = (g_head - g_count + ALOG_RING_CAP) % ALOG_RING_CAP;
    int bad = 0;
    for (int i = 0; i < g_count; i++)
        if (fputs(g_ring[(start + i) % ALOG_RING_CAP], g_fp) == EOF)
            bad = 1;
    if (fflush(g_fp) != 0)
        bad = 1;
    /* LOG-PATH-1 WP1c: 写盘失败可见化, sticky 标志防刷屏 */
    if (bad && !g_write_failed) {
        g_write_failed = 1;
        fprintf(stderr, "[alog] 写盘失败: %s\n", strerror(errno));
    }
    g_count = 0;
    g_head = 0;
}

void alog_log(const char *op, const char *target,
              const char *detail, const char *source)
{
    if (!g_fp) return;
    long t = alog_now_ms() - g_t0_ms;
    /* 对齐表格 (人读) + 竖线分隔 (机读): 宽度钉死, 超长截断防错位。
     * op 列宽 12 (最长 preset_save); CJK detail 视觉宽度按字节数对齐,
     * 机读 split('|') 不受影响 */
    char line[ALOG_LINE_CAP];
    snprintf(line, sizeof(line), "%8ld | %-12.12s | %-24.24s | %-32.32s | %.8s\n",
             t, op ? op : "?", target ? target : "?",
             detail ? detail : "-", source ? source : "user");
    memcpy(g_ring[g_head], line, strlen(line) + 1);
    g_head = (g_head + 1) % ALOG_RING_CAP;
    g_count++;
    g_lines++;
    if (g_count >= ALOG_FLUSH_EVERY)
        alog_flush_locked();
    /* 溢出保护: 环形缓冲满 (4096 未 flush 不会发生, flush 每 64 触发) */
}

void alog_state(kcapi_synth_t *s, const char *source)
{
    if (!s) return;
    char detail[64], target[48];
    int ncable = 0;
    int nid = 0, act = 0;
    const char *sn = NULL, *dn = NULL;
    float amt = 0.0f;
    for (int i = 0; i < kcapi_ctrl_cable_count(s); i++) {
        kcapi_ctrl_cable_info(s, i, &nid, &sn, &dn, &amt, &act);
        if (act) ncable++;
    }
    snprintf(target, sizeof(target), "engine=%d", kcapi_get_engine_mode(s));
    float algo = -1.0f, egm = -1.0f;
    kcapi_get_param(s, "fm.algorithm", &algo);    /* TBENCH-FIX-1 读回口 */
    kcapi_get_param(s, "fm.eg_mode", &egm);
    snprintf(detail, sizeof(detail), "algo=%d eg_mode=%d ports=%d cables=%d",
             (int)algo, (int)egm, kcapi_num_ports(), ncable);
    alog_log("state", target, detail, source);
}

int alog_set_param(kcapi_synth_t *s, const char *name, float value,
                   const char *source)
{
    if (!s || !name) return -1;
    char detail[64];
    float oldv = 0.0f;
    int has_old = (kcapi_get_param(s, name, &oldv) == 0);
    int rc = kcapi_set_param(s, name, value);
    if (has_old)
        snprintf(detail, sizeof(detail), "%.4g -> %.4g", (double)oldv, (double)value);
    else if (kcapi_is_param_port(name))
        /* TBENCH-FIX-2: DST_PARAM 口读旧值失败属异常, 记 "?!" 供审计
         * (格式 v1 行结构不变, 仅旧值列内容) */
        snprintf(detail, sizeof(detail), "?! -> %.4g", (double)value);
    else
        snprintf(detail, sizeof(detail), "? -> %.4g", (double)value);
    alog_log("param", name, detail, source);
    if (rc != 0) {
        char w[96];
        snprintf(w, sizeof(w), "未知参数名: %s", name);
        alog_warn(w, "kernel");
    }
    return rc;
}

void alog_note_on(kcapi_synth_t *s, int note, int velocity, const char *source)
{
    if (!s) return;
    char target[16], detail[24];
    snprintf(target, sizeof(target), "%d", note);
    snprintf(detail, sizeof(detail), "vel=%d", velocity);
    kcapi_note_on(s, note, velocity);
    alog_log("note_on", target, detail, source);
}

void alog_note_off(kcapi_synth_t *s, const char *source)
{
    if (!s) return;
    kcapi_note_off(s);
    alog_log("note_off", "-", "-", source);
}

int alog_cable_connect(kcapi_synth_t *s, int ctrl, const char *src_name,
                       const char *dst_name, float amount, const char *source)
{
    if (!s) return -1;
    int id = ctrl ? kcapi_cable_ctrl_connect(s, src_name, dst_name, amount)
                  : kcapi_cable_audio_connect(s, src_name, dst_name, amount);
    char target[64], detail[48];
    snprintf(target, sizeof(target), "%s->%s",
             src_name ? src_name : "?", dst_name ? dst_name : "?");
    /* detail 带 ctrl/audio 标记: 回放器据此分流 */
    snprintf(detail, sizeof(detail), "amount=%.3g id=%d %s",
             (double)amount, id, ctrl ? "ctrl" : "audio");
    alog_log("cable+", target, detail, source);
    return id;
}

void alog_cable_disconnect(kcapi_synth_t *s, int ctrl, int id, const char *source)
{
    if (!s) return;
    if (ctrl) kcapi_cable_disconnect_ctrl(s, id);
    else      kcapi_cable_disconnect_audio(s, id);
    char detail[24];
    snprintf(detail, sizeof(detail), "id=%d", id);
    alog_log("cable-", ctrl ? "ctrl" : "audio", detail, source);
}

void alog_preset_load(const char *path, const char *source)
{
    alog_log("preset_load", path ? path : "?", "-", source);
}

void alog_preset_save(const char *path, const char *source)
{
    alog_log("preset_save", path ? path : "?", "-", source);
}

void alog_engine(kcapi_synth_t *s, int old_mode, int new_mode, const char *source)
{
    if (!s) return;
    char detail[32];
    snprintf(detail, sizeof(detail), "%d -> %d", old_mode, new_mode);
    kcapi_set_param(s, "voice.engine_mode", (float)new_mode);
    alog_log("engine", "voice.engine_mode", detail, source);
}

void alog_warn(const char *msg, const char *source)
{
    alog_log("warn", "kcapi", msg ? msg : "?", source);
}

void alog_stop(void)
{
    if (!g_fp) return;
    alog_flush_locked();
    fprintf(g_fp, "# session_end lines=%ld\n", g_lines);
    /* LOG-PATH-1 WP1c: 最终 flush/fclose 检查, 失败可见化 */
    int bad = 0;
    if (fflush(g_fp) != 0)
        bad = 1;
    if (fclose(g_fp) != 0)
        bad = 1;
    g_fp = NULL;
    if (bad)
        fprintf(stderr, "[alog] 写盘失败: %s\n", strerror(errno));
}
