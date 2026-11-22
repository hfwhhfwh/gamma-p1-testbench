/*
 * logpath1_test.c — LOG-PATH-1 自证 (2026-11-17, 测试台侧)
 *
 * 覆盖 LOG-PATH-1 WP1 的库层判据:
 *   T1  alog_start("/no/such/dir/x.log") == -1, stderr 含路径与 errno
 *       原因; 失败后续 alog_log/alog_stop 全 no-op 不崩溃
 *   T2  成功路径 stderr 打 "[alog] logging to /<绝对路径>"
 *   T3  alog_start 幂等 (二次调用不重复写 session 头);
 *       alog_stop 幂等 (重复调用文件不增长)
 *   T4  alog_start(NULL) 回退默认 "testbench_action.log" (库层默认;
 *       main 层 GP1_LOG_PATH 回退由 L4 手工实跑覆盖)
 *   T5  失败后再成功: 新文件 session 头恰一次 (失败不留脏状态)
 *   T6  格式 v1 冻结: 脚本化事件序列逐行 split('|') 恰 5 段, op 全
 *       合法 (含新 op=signal), 日志文件内零 "[alog]" 错误行
 *
 * 口径: 只链库层 (action_log.o + CORE_OBJS), 不启 synth; stderr 经
 * freopen 重定向到临时文件后读回比对。用法: tests/ 下运行
 * ./logpath1_test (产物全在系统临时目录, 不污染仓库)。
 *
 * 变更摘要 (2026-11-17 TBENCH-WIN-1 WP3a):
 *   POSIX 专用件全部改走平台层 (src/platform.h): mkdtemp//tmp/realpath
 *   /getcwd/chdir/unlink -> plat_mkdtemp/plat_absolute_path/plat_getcwd/
 *   plat_chdir/plat_unlink (临时目录取 TMPDIR, 语义一致); 顶部
 *   _DARWIN_C_SOURCE/_POSIX_C_SOURCE 与 <unistd.h> 随之移除。判据
 *   T1~T6 数值与口径逐项不变。
 */

#include "action_log.h"
#include "platform.h"   /* TBENCH-WIN-1: 临时目录/绝对路径/工作目录助手 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...) do {                                     \
    g_checks++;                                                   \
    if (cond) { printf("  PASS "); printf(__VA_ARGS__); }         \
    else      { printf("  FAIL "); printf(__VA_ARGS__); g_fails++; } \
    printf("\n");                                                 \
} while (0)

/* 读整个文件到 buf (截断到 cap-1), 失败返回 -1 */
static long read_file(const char *path, char *buf, long cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long n = (long)fread(buf, 1, (size_t)cap - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

/* stderr 重定向到 path (读回比对用) */
static int stderr_to(const char *path)
{
    return freopen(path, "w", stderr) != NULL;
}

int main(void)
{
    /* TBENCH-WIN-1: 系统临时目录下建唯一目录 (POSIX 侧即 mkdtemp) */
    char dir[PLAT_PATH_MAX];
    if (!plat_mkdtemp("gp1lp1", dir, sizeof(dir))) {
        printf("plat_mkdtemp 失败\n");
        return 1;
    }

    char err1[512], err2[512], log1[512], log2[512], log3[512];
    snprintf(err1, sizeof(err1), "%s/err1.txt", dir);
    snprintf(err2, sizeof(err2), "%s/err2.txt", dir);
    snprintf(log1, sizeof(log1), "%s/a.log", dir);
    snprintf(log2, sizeof(log2), "%s/b.log", dir);
    snprintf(log3, sizeof(log3), "%s/c.log", dir);

    /* ---- T1: 打开失败可见化 (L2) ---- */
    CHECK(stderr_to(err1), "T1a: stderr 重定向可用");
    int rc_bad = alog_start("/no/such/dir/x.log");
    fflush(stderr);
    CHECK(rc_bad == -1, "T1b: alog_start(不可写路径) 返回 -1 (got %d)",
          rc_bad);
    {
        char buf[4096];
        long n = read_file(err1, buf, sizeof(buf));
        int has_path = n > 0 && strstr(buf, "/no/such/dir/x.log") != NULL;
        int has_errno = n > 0 && strstr(buf, strerror(ENOENT)) != NULL;
        CHECK(has_path, "T1c: stderr 含失败路径");
        CHECK(has_errno, "T1d: stderr 含 errno 原因 (%s)",
              strerror(ENOENT));
    }
    /* 失败后续 alog_* 全部 no-op 不崩溃 */
    alog_log("param", "x.y", "1 -> 2", "user");
    alog_state(NULL, "user");
    alog_note_on(NULL, 60, 100, "user");
    alog_stop();
    {
        char buf[64];
        CHECK(read_file("/no/such/dir/x.log", buf, sizeof(buf)) == -1,
              "T1e: 失败后日志文件不存在, alog_* 确为 no-op");
    }

    /* ---- T2: 成功路径打印绝对落点 (L3) ---- */
    CHECK(stderr_to(err2), "T2a: stderr 重定向可用");
    int rc_ok = alog_start(log1);
    CHECK(rc_ok == 0, "T2b: alog_start(合法路径) 返回 0 (got %d)", rc_ok);
    fflush(stderr);          /* freopen 后 stderr 为全缓冲, 先 flush 再读回 */
    {
        char buf[4096], rp[PLAT_PATH_MAX];
        long n = read_file(err2, buf, sizeof(buf));
        /* TBENCH-WIN-1: 原 realpath 调平台层 (POSIX 侧即 realpath) */
        const char *canon = plat_absolute_path(log1, rp, sizeof(rp)) ? rp : log1;
        /* TBENCH-WIN-1: 绝对路径前缀平台分支 —— POSIX 为 '/'; Windows
         * 为盘符 "<大写字母>:\"; Mac 判据 ("[alog] logging to /") 逐字
         * 不变, Windows 判据语义对齐 (同为"已解析到绝对路径") */
#ifdef _WIN32
        int has_abs = 0;
        const char *lt = n > 0 ? strstr(buf, "[alog] logging to ") : NULL;
        if (lt) {
            const char *q = lt + strlen("[alog] logging to ");
            has_abs = ((q[0] >= 'A' && q[0] <= 'Z') ||
                       (q[0] >= 'a' && q[0] <= 'z')) && q[1] == ':'
                      && (q[2] == '\\' || q[2] == '/');
        }
        CHECK(has_abs, "T2c: stderr 含 \"[alog] logging to <盘符>:\\\" (绝对路径)");
#else
        int has_abs = n > 0 && strstr(buf, "[alog] logging to /") != NULL;
        CHECK(has_abs, "T2c: stderr 含 \"[alog] logging to /\" (绝对路径)");
#endif
        int has_rp = n > 0 && strstr(buf, canon) != NULL;
        CHECK(has_rp, "T2d: stderr 绝对路径与 realpath 一致 (%s)", canon);
    }
    alog_log("param", "fm.peg_d1l", "50 -> 70", "user");
    alog_stop();
    {
        char buf[4096];
        long n = read_file(log1, buf, sizeof(buf));
        CHECK(n > 0 && strstr(buf, "# gamma-p1 testbench action log v1") &&
              strstr(buf, "# session_end"),
              "T2e: 文件含 v1 头与 session_end 尾行");
    }

    /* ---- T3: 幂等 (start 不重复写头 / stop 不增长) ---- */
    alog_start(log2);
    alog_start(log2);            /* 已开会话: no-op */
    alog_stop();
    {
        char buf[4096];
        long n = read_file(log2, buf, sizeof(buf));
        int nstart = 0;
        for (char *p = buf; (p = strstr(p, "# session_start")) != NULL;
             p++, nstart++) {}
        CHECK(n > 0 && nstart == 1,
              "T3a: 二次 alog_start session 头恰一次 (got %d)", nstart);
    }
    {
        char buf[4096];
        long n1 = read_file(log2, buf, sizeof(buf));
        alog_stop();             /* 重复 stop: no-op */
        alog_log("param", "x", "1 -> 1", "user");  /* 停后写: no-op */
        long n2 = read_file(log2, buf, sizeof(buf));
        CHECK(n1 == n2, "T3b: 重复 alog_stop/停后写日志文件不增长");
    }

    /* ---- T4: NULL 路径回退默认名 (库层默认) ---- */
    char cwd_save[PLAT_PATH_MAX];
    if (plat_getcwd(cwd_save, sizeof(cwd_save))) {
        char sub[PLAT_PATH_MAX];
        /* TBENCH-WIN-1: 原 mkdtemp/chdir 调平台层 */
        if (plat_mkdtemp("gp1lp1d", sub, sizeof(sub)) &&
            plat_chdir(sub)) {
            int rc_def = alog_start(NULL);
            char buf[4096];
            int exists = read_file("testbench_action.log", buf,
                                   sizeof(buf)) > 0;
            CHECK(rc_def == 0 && exists,
                  "T4a: alog_start(NULL) 回退默认 testbench_action.log "
                  "(rc=%d exists=%d)", rc_def, exists);
            alog_log("param", "vca.level", "0 -> 1", "user");
            alog_stop();
            exists = read_file("testbench_action.log", buf, sizeof(buf)) > 0
                     && strstr(buf, "# session_end") != NULL;
            CHECK(exists, "T4b: 默认名文件 session_end 收口");
            plat_unlink("testbench_action.log");   /* TBENCH-WIN-1: 原 unlink */
            if (!plat_chdir(cwd_save))
                printf("  信息  chdir 回原目录失败 (后续用绝对路径, 不影响)\n");
        } else {
            CHECK(0, "T4a: plat_mkdtemp/plat_chdir 失败");
        }
    } else {
        CHECK(0, "T4a: plat_getcwd 失败");
    }

    /* ---- T5: 失败后再成功不留脏状态 ---- */
    CHECK(alog_start("/no/such/dir/y.log") == -1, "T5a: 再次失败 == -1");
    CHECK(alog_start(log3) == 0, "T5b: 失败后 alog_start 成功 == 0");
    alog_stop();
    {
        char buf[4096];
        read_file(log3, buf, sizeof(buf));
        int nstart = 0;
        for (char *p = buf; (p = strstr(p, "# session_start")) != NULL;
             p++, nstart++) {}
        CHECK(nstart == 1, "T5c: 失败后成功文件 session 头恰一次 (got %d)",
              nstart);
    }

    /* ---- T6: 格式 v1 冻结 (含新 op=signal 行形; 零 "[alog]" 错误行) ---- */
    alog_start(log3);            /* 追加第二 session */
    alog_log("param", "fm.peg_d1l", "50 -> 70", "user");
    alog_log("note_on", "60", "vel=100", "user");
    alog_log("note_off", "-", "-", "user");
    alog_log("cable+", "lfo0.out->vcf.cutoff", "amount=0.3 id=1 ctrl", "user");
    alog_log("cable-", "ctrl", "id=1", "user");
    alog_log("engine", "voice.engine_mode", "0 -> 1", "user");
    alog_log("warn", "kcapi", "未知参数名: fm.x", "kernel");
    alog_log("state", "engine=1", "algo=1 eg_mode=0 ports=374", "user");
    alog_log("preset_load", "/tmp/x.snp", "-", "user");
    alog_log("preset_save", "/tmp/x.snp", "-", "user");
    alog_log("signal", "SIGINT|SIGTERM", "graceful shutdown", "system");
    alog_stop();
    {
        FILE *f = fopen(log3, "r");
        char line[1024];
        int fmt_ok = 1, op_ok = 1, no_errline = 1, signal_ok = 0;
        while (f && fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            if (strstr(line, "[alog]")) no_errline = 0;
            /* 机读 split('|'): 恰 5 段 —— 口径同既有 log_wire_test.cpp
             * (前 4 个分隔符切段, nf<5 封顶, 余者留在第 5 段)。op=signal
             * 行 target 按任务书规格为字面 "SIGINT|SIGTERM" (含 '|'),
             * 其第 5 段内含额外竖线, 与既有解析器行为一致 */
            char buf[1024];
            snprintf(buf, sizeof(buf), "%s", line);
            int nf = 1;
            for (char *q = buf; *q && nf < 5; q++)
                if (*q == '|') nf++;
            if (nf != 5) { fmt_ok = 0; continue; }
            /* op 列: 第 2 段 */
            char *p1 = strchr(buf, '|');
            if (!p1) { fmt_ok = 0; continue; }
            p1++;
            while (*p1 == ' ') p1++;
            static const char *ops[] = {
                "param", "note_on", "note_off", "cable+", "cable-",
                "engine", "warn", "state", "preset_load", "preset_save",
                "signal"
            };
            int known = 0;
            for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
                if (strncmp(p1, ops[i], strlen(ops[i])) == 0) known = 1;
            if (!known) op_ok = 0;
            if (strncmp(p1, "signal", 6) == 0 &&
                strstr(line, "SIGINT|SIGTERM") &&
                strstr(line, "graceful shutdown") &&
                strstr(line, "system"))
                signal_ok = 1;
        }
        if (f) fclose(f);
        CHECK(fmt_ok, "T6a: 全部事件行 split('|') 恰 5 段 (格式 v1 冻结)");
        CHECK(op_ok, "T6b: 全部 op ∈ 合法集合 (含 signal)");
        CHECK(no_errline, "T6c: 日志文件内零 \"[alog]\" 错误行");
        CHECK(signal_ok, "T6d: op=signal 行 target/detail/source 列正确");
    }

    printf("\n== 汇总: %d 项检查, %d 项 FAIL ==\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
