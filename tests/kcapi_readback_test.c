/* TBENCH-FIX-2 —— kcapi_get_param 读回面扩展验收件
 *
 * 验收 (任务书 WP3/WP4):
 *  WP3a: 374 口 [LEGATO-FIX-1: 373->374] 逐个 alog_set_param(0.37)
 *        用户路径写入 + 立即
 *        kcapi_get_param 读回; DST_PARAM 299 口 [LEGATO-FIX-1: 298->299,
 *        +voice.legato_mode @373] 必须 299/299 返回 0
 *        且数值一致 (容差沿用 ENV-DIAG wp1: 5%+0.02, int 口含截断
 *        容差; 15 口特殊语义按"当前生效值"口径, 逐口注释)。
 *        同时统计写入侧 stderr (格式冻结的 "未知参数名...未写入"
 *        行, 只允许出现在 43 个不可写口) 与读回侧 stderr (必须 0 行)。
 *  WP3b: 374 口只读扫描 (只 kcapi_get_param 不写入) stderr 零行。
 *  WP3c: kcapi_get_param("fm.not_exist")/("zzz.foo") -1 + 恰一行
 *        stderr (格式不变)。
 *  WP3d: 只读性 —— 快照后渲染 64 块 A; 恢复快照; 10 口读回; 渲染
 *        64 块 B; A==B 逐样本 cmp=0。
 *  WP3e: 重名组 8 组写哪口读哪口。
 *  WP4 : alog 拧 20 个非 fm 参数 (每个两写形成 old->new 链), 日志
 *        old 列全部真实数值 (无 "?"/"?!"), stderr 零行。
 *
 * 纪律: 只走 kcapi/alog 用户路径写入; 内核结构读取为只读观测;
 * 零警告 (-Wall -Wextra -Werror -std=c11 -pedantic); CSV 带口径
 * 表头; 种子固定 (同 ENV-DIAG 口径)。
 *
 * 口径: sr=48000, 块 64; 写入代表值 0.37 (int 口截断为 0 仍非默认);
 * stderr 用 freopen 分阶段捕获到 tbfix2_out/ 下文件。
 *
 * 变更摘要 (2026-10-07 TBENCH-FIX-3):
 *  WP5 新增三段 (产物写 tbfix3_out/, 既有 WP3a~WP4 段逐位不动):
 *   T-A (P-08/P-09): 四别名 vcf.soft_clip/vcf.smoothing/vcf.drive_comp/
 *        env0.sustain 写 0.37 同名读回一致 (int 截断容差照 WP3a 先例),
 *        alog 两写旧值链无 "?", 写读全程 stderr 零行;
 *   T-B (P-33): fm.frequency 写 440 读回==440 (S4 声明走"读真值"
 *        路线), 写 220 复验非恒定;
 *   T-C (P-48): 连线→断开→kcapi_ctrl_cable_info 读 inactive 槽,
 *        src/dst=="?" 且 active==0; active 槽行为不变用例保留。
 *
 * 变更摘要 (2026-10-25 TBENCH-INFRA-1):
 *  P-27: :186 wp_readback_after.csv fopen 补 if(!f) 检查 (守卫全部
 *     fprintf/fclose, FAIL 计入 g_fails, 不新增 CHECK 项, 基线 18 不变)。
 *  P-35: 十处 freopen 全量加固 —— 新增 capture_stderr()/restore_stderr()
 *     包装 (返回值检查); 捕获失败时对依赖捕获文件的判据 (WP3a2/WP3a3/
 *     WP3b/WP3c/WP4b/T-A3) 置 SKIP (SKIP_CHECK, 不计 PASS 不算 FAIL),
 *     汇总行改 "%d 项检查, %d 项 FAIL, %d 项 SKIP"; /dev/tty 恢复失败
 *     保持重定向不动并在末尾打印 "WARN: stderr 未能恢复重定向"。
 *
 * 变更摘要 (2026-11-17 LOG-PATH-1, 判据连带, 全打标记):
 *   alog_start 成功路径新增 1 行 stderr 落点提示 (任务书 WP1a 规格,
 *   捕获窗口内 alog_start 亦打), 三个 stderr 精确行数判据各 +1 行
 *   (判据强度不变, 仍精确计数): WP3a3 43->44, WP4b 0->1, T-A3 0->1。
 *   信息行文字同步 (预期 43->44)。
 *
 * 变更摘要 (2026-11-17 TBENCH-WIN-1 WP3a):
 *   restore_stderr 的 freopen("/dev/tty") 改走平台层 plat_reopen_stderr_tty
 *   (Windows 为 CONOUT$; POSIX 侧即原调用直转调); 捕获/恢复判据口径
 *   与 SKIP/WARN 行为不变。
 */

#include "synth.h"
#include "vmatrix.h"
#include "fm/fm.h"
#include "kernel_capi.h"
#include "action_log.h"
#include "platform.h"   /* TBENCH-WIN-1: restore_stderr 的 /dev/tty 封装 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR_F 48000.0f
#define BLK 64

static int g_checks = 0, g_fails = 0;
/* TBENCH-INFRA-1: P-35 skip 计数 —— stderr 捕获不可用时依赖捕获
 * 文件的判据置 SKIP (不计 PASS 也不算 FAIL), 汇总行单列 */
static int g_skips = 0;
#define SKIP_CHECK(name) do {                                    \
    g_skips++;                                                   \
    printf("  SKIP %s: stderr 捕获不可用\n", name);              \
} while (0)
#define CHECK(cond, ...) do {                                    \
    g_checks++;                                                  \
    if (cond) { printf("  PASS "); printf(__VA_ARGS__); }        \
    else      { printf("  FAIL "); printf(__VA_ARGS__); g_fails++; } \
    printf("\n");                                                \
} while (0)

/* ---- 15 口特殊语义清单 (ENV-DIAG-1 WP1c 结论; 逐口口径见报告) ---- */
static int is_special15(const char *name)
{
    static const char *const tab[15] = {
        "vco0.noise_seed", "vco1.noise_seed", "vco2.noise_seed",
        "lfo0.seed", "lfo1.seed", "lfo2.seed",
        "vcf.cutoff", "vcf.oversampling", "vca.drive",
        "fm.algorithm", "fm.dxlfo.wave", "vca.response",
        "lfo0.sync_mode", "lfo1.sync_mode", "lfo2.sync_mode"
    };
    for (int i = 0; i < 15; i++)
        if (strcmp(name, tab[i]) == 0) return 1;
    return 0;
}

static void fix_seeds(kcapi_synth_t *s)
{
    kcapi_set_param(s, "vco0.noise_seed", 111.0f);
    kcapi_set_param(s, "vco1.noise_seed", 222.0f);
    kcapi_set_param(s, "vco2.noise_seed", 333.0f);
    kcapi_set_param(s, "lfo0.seed", 444.0f);
    kcapi_set_param(s, "lfo1.seed", 555.0f);
    kcapi_set_param(s, "lfo2.seed", 666.0f);
    kcapi_set_param(s, "fm.seed", 777.0f);
}

static kcapi_synth_t *new_synth(void)
{
    kcapi_synth_t *s = kcapi_synth_create();
    kcapi_synth_init(s, SR_F);
    fix_seeds(s);
    return s;
}

/* 15 口"当前生效值"独立观测 (内核只读 getter/快照/字段; 与 kcapi
 * 读回路径互不为同一份代码, 用于交叉验证 landed 语义) */
static int special_effective(const kcapi_synth_t *s, const char *name,
                             float *out)
{
    const Synth *ks = (const Synth *)s;   /* 只读观测 */
    if (name[0] == 'v' && name[1] == 'c' && name[2] == 'o' &&
        strstr(name, ".noise_seed")) {
        VcoParams p;
        vco_get_params(&ks->voice.vco[name[3] - '0'], &p);
        *out = (float)p.noise_seed;
        return 1;
    }
    if (name[0] == 'l' && name[1] == 'f' && name[2] == 'o') {
        int i = name[3] - '0';
        if (strstr(name, ".seed")) {
            LfoParams p;
            lfo_get_params(&ks->voice.lfo[i], &p);
            *out = (float)p.seed;
            return 1;
        }
        if (strstr(name, ".sync_mode")) {
            *out = (float)kcapi_get_lfo_sync_mode(s, i);
            return 1;
        }
    }
    if (strcmp(name, "vcf.cutoff") == 0) {
        VcfParams p; vcf_get_params(&ks->voice.vcf, &p);
        *out = p.cutoff; return 1;
    }
    if (strcmp(name, "vcf.oversampling") == 0) {
        VcfParams p; vcf_get_params(&ks->voice.vcf, &p);
        *out = (float)p.oversampling; return 1;
    }
    if (strcmp(name, "vca.drive") == 0) {
        VcaParams p; vca_get_params(&ks->voice.vca, &p);
        *out = p.drive; return 1;
    }
    if (strcmp(name, "vca.response") == 0) {
        *out = (float)ks->voice.vca.response; return 1;
    }
    if (strcmp(name, "fm.algorithm") == 0) {
        FmParams fp; kcapi_get_fm_params(s, &fp);
        *out = (float)fp.algorithm; return 1;
    }
    if (strcmp(name, "fm.dxlfo.wave") == 0) {
        FmParams fp; kcapi_get_fm_params(s, &fp);
        *out = (float)fp.lfo_wave; return 1;   /* 编码映射: 读回=存储值 */
    }
    return 0;
}

/* 统计文本文件行数与含某子串的行数 */
static long file_lines(const char *path, const char *needle,
                       long *needle_lines)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[512];
    long n = 0, hit = 0;
    while (fgets(buf, sizeof(buf), f)) {
        n++;
        if (needle && strstr(buf, needle)) hit++;
    }
    fclose(f);
    if (needle_lines) *needle_lines = hit;
    return n;
}

/* TBENCH-INFRA-1: P-35 freopen 捕获/恢复加固。
 * capture_stderr: 返回值检查; 失败时 stderr 保持原状并显式报错,
 * 返回 0 —— 调用方须对依赖捕获文件的判据 SKIP_CHECK。
 * restore_stderr: fflush + 恢复 /dev/tty; 恢复失败保持重定向不动,
 * 计数供 main 末尾 "WARN: stderr 未能恢复重定向" 警示。 */
static int g_stderr_restore_fail = 0;

static int capture_stderr(const char *path)
{
    if (freopen(path, "w", stderr)) return 1;
    fprintf(stderr, "TBENCH-INFRA-1: freopen %s 失败, stderr 保持原状\n",
            path);
    return 0;
}

static void restore_stderr(void)
{
    /* TBENCH-WIN-1: /dev/tty 收进平台层 (Windows 为 CONOUT$);
     * 恢复失败保持重定向不动, 计数供 main 末尾警示 */
    if (!plat_reopen_stderr_tty())
        g_stderr_restore_fail++;   /* 保持重定向不动 */
}

/* ==================== WP3a: 374 口写读一致性 [LEGATO-FIX-1: 373->374] ==================== */

/* 写读一致性判定 (wp1 容差 / 15 口特殊语义=当前生效值) */
static int check_consistent(const kcapi_synth_t *s, const char *name, float rb)
{
    if (is_special15(name)) {
        float eff = 0.0f;
        return special_effective(s, name, &eff) &&
               fabsf(rb - eff) <= fabsf(eff) * 0.001f + 0.001f;
    }
    /* wp1 容差: 5%+0.02 / int 截断 (0.37 -> 0) */
    float tol = fabsf(0.37f) * 0.05f + 0.02f;
    return (fabsf(rb - 0.37f) <= tol || fabsf(rb) <= 0.02f ||
            rb == (float)(int)0.37f);
}

static void wp3a_survey(void)
{
    printf("== WP3a: 374 口 alog_set_param(0.37) + kcapi_get_param 读回 ==\n");  /* [LEGATO-FIX-1: 373->374] */
    int cap_ok = capture_stderr("tbfix2_out/wp3a_stderr.txt");   /* TBENCH-INFRA-1: P-35 返回值检查 */
    remove("tbfix2_out/wp3a_actions.log");   /* alog 追加模式: 重跑前清场 */
    alog_start("tbfix2_out/wp3a_actions.log");
    FILE *f = fopen("tbfix2_out/wp_readback_after.csv", "w");
    /* TBENCH-INFRA-1: P-27 fopen 检查 (守卫全部 fprintf/fclose) */
    if (!f) { printf("  FAIL WP3a: 无法写 tbfix2_out/wp_readback_after.csv\n"); g_fails++; }
    if (f) fprintf(f, "# 口径: sr=48000; 374 口逐个新实例 alog_set_param 写 0.37 "
               "(用户路径) 后立即 kcapi_get_param 读回; kind: 0=READ "
               "1=DST_MOD 2=DST_PARAM 3=AUDIO_BUF; consistent: DST_PARAM "
               "=读回一致 (wp1 容差 5%%+0.02/int 截断; 15 口特殊语义=当前 "
               "生效值), 重名 DST_MOD 行=解析到 DST_PARAM 双胞胎同口径, "
               "READ=有真实值, 其余=静默 -1\n");
    if (f) fprintf(f, "idx,name,kind,write_rc,read_rc,value,consistent\n");
    int n = kcapi_num_ports();
    int n_dp = 0, n_dp_ok = 0, n_dp_cons = 0;
    int n_dup = 0, n_dup_ok = 0;     /* 重名 DST_MOD 行 (解析到 DST_PARAM 双胞胎) */
    int n_read = 0, n_read_ok = 0;
    int n_silent = 0, n_bad = 0;
    for (int i = 0; i < n; i++) {
        const VmxPort *vp = vmatrix_port(i);   /* 只读端口表 */
        const char *name = vp->name;
        kcapi_synth_t *s = new_synth();
        int wrc = alog_set_param(s, name, 0.37f, "user");
        float rb = 0.0f;
        int rrc = kcapi_get_param(s, name, &rb);
        int cons = 0;
        if (vp->kind == VMX_PORT_DST_PARAM) {
            n_dp++;
            if (rrc == 0) {
                n_dp_ok++;
                cons = check_consistent(s, name, rb);
            }
            if (cons) n_dp_cons++; else n_bad++;
        } else if (vmatrix_find_param_port(name) >= 0) {
            /* 重名 DST_MOD 行: 写/读都经 find_param_port 落 DST_PARAM
             * 双胞胎 (写哪口读哪口), 同一致性口径 */
            n_dup++;
            if (rrc == 0)
                cons = check_consistent(s, name, rb);
            if (cons) n_dup_ok++; else n_bad++;
        } else if (vp->kind == VMX_PORT_READ) {
            cons = (rrc == 0);      /* READ 口接真实值 */
            n_read++;
            if (cons) n_read_ok++; else n_bad++;
        } else {
            cons = (rrc == -1);     /* DST_MOD-only/AUDIO_BUF: 静默 -1 */
            if (cons) n_silent++; else n_bad++;
        }
        if (f) fprintf(f, "%d,%s,%d,%d,%d,%.9g,%d\n", i, name, vp->kind,
                wrc, rrc, (double)rb, cons);
        kcapi_synth_destroy(s);
    }
    if (f) fclose(f);
    alog_stop();
    restore_stderr();   /* TBENCH-INFRA-1: P-35 恢复 + 返回值检查 */
    long rdback_lines = 0;
    /* TBENCH-INFRA-1: P-35 捕获失败时行数不取 (文件不存在) */
    long total = cap_ok ? file_lines("tbfix2_out/wp3a_stderr.txt",
                                     "(读回)", &rdback_lines) : -1;
    printf("  信息  DST_PARAM %d 口: 读回成功 %d, 一致 %d; 重名 DST_MOD "
           "%d 口一致 %d; READ %d 口真实值 %d; 静默 -1 %d 口; 异常 %d\n",
           n_dp, n_dp_ok, n_dp_cons, n_dup, n_dup_ok, n_read, n_read_ok,
           n_silent, n_bad);
    printf("  信息  stderr 捕获: %s总 %ld 行 (写入侧冻结格式, 预期 44 "
           "[LOG-PATH-1: 43+k]), "
           "读回侧 %ld 行 (必须 0)\n",
           cap_ok ? "" : "不可用 (SKIP), ", total, rdback_lines);
    CHECK(n_dp == 299 && n_dp_ok == 299 && n_dp_cons == 299,
          "WP3a: DST_PARAM 299 口读回 %d/299 成功 %d/299 一致 "
          "[LEGATO-FIX-1: 298->299, +voice.legato_mode] "
          "(15 口特殊语义=当前生效值, 明细 wp_readback_after.csv)",
          n_dp_ok, n_dp_cons);
    CHECK(n_dup == 28 && n_dup_ok == 28 && n_read == 17 &&
          n_read_ok == 17 && n_silent == 30 && n_bad == 0,
          "WP3a4: 其余 75 口面正确: 重名 DST_MOD %d/28 一致, READ %d/17 "
          "真实值, 静默 -1 %d/30, 异常 %d",
          n_dup_ok, n_read_ok, n_silent, n_bad);
    /* TBENCH-INFRA-1: P-35 捕获失败 -> 依赖捕获文件的判据置 SKIP */
    if (!cap_ok) {
        SKIP_CHECK("WP3a2");
        SKIP_CHECK("WP3a3");
    } else {
    CHECK(rdback_lines == 0,
          "WP3a2: 全程读回侧 stderr %ld 行 = 0 (旧值 \"?\"刷屏消除)",
          rdback_lines);
    CHECK(total == 44,
          "WP3a3: 写入侧 stderr %ld 行 = 44 (43 个不可写口的冻结格式行 "
          "+ 1 行 alog 成功落点提示 [LOG-PATH-1], 一行不多)", total);
    }
}

/* ==================== WP3b: 374 口只读扫描 stderr 零行 [LEGATO-FIX-1] ==================== */

static void wp3b_readonly_scan(void)
{
    printf("== WP3b: 374 口只读扫描 (不写入) stderr 零行 ==\n");   /* [LEGATO-FIX-1: 373->374] */
    kcapi_synth_t *s = new_synth();
    int cap_ok = capture_stderr("tbfix2_out/wp3b_stderr.txt");   /* TBENCH-INFRA-1: P-35 返回值检查 */
    int n = kcapi_num_ports();
    int n_ok = 0, n_silent_fail = 0;
    for (int i = 0; i < n; i++) {
        const VmxPort *vp = vmatrix_port(i);
        float v = 0.0f;
        if (kcapi_get_param(s, vp->name, &v) == 0) n_ok++;
        else n_silent_fail++;
    }
    restore_stderr();   /* TBENCH-INFRA-1: P-35 恢复 + 返回值检查 */
    kcapi_synth_destroy(s);
    /* TBENCH-INFRA-1: P-35 捕获失败时行数不取 (文件不存在) */
    long lines = cap_ok ? file_lines("tbfix2_out/wp3b_stderr.txt", NULL, NULL) : -1;
    printf("  信息  374 口只读: 成功 %d, 静默 -1 %d; stderr %ld 行\n",
           n_ok, n_silent_fail, lines);
    /* TBENCH-INFRA-1: P-35 捕获失败 -> 依赖捕获文件的判据置 SKIP */
    if (!cap_ok) SKIP_CHECK("WP3b");
    else
    CHECK(lines == 0,
          "WP3b: 374 口只读扫描 stderr %ld 行 = 0 (存证 "
          "wp3b_stderr.txt)", lines);
}

/* ==================== WP3c: 未知名 -1 + 恰一行 stderr ==================== */

static void wp3c_unknown(void)
{
    printf("== WP3c: 未知名探测 ==\n");
    kcapi_synth_t *s = new_synth();
    int cap_ok = capture_stderr("tbfix2_out/wp3c_stderr.txt");   /* TBENCH-INFRA-1: P-35 返回值检查 */
    float v = 0.0f;
    int rc1 = kcapi_get_param(s, "fm.not_exist", &v);
    int rc2 = kcapi_get_param(s, "zzz.foo", &v);
    restore_stderr();   /* TBENCH-INFRA-1: P-35 恢复 + 返回值检查 */
    kcapi_synth_destroy(s);
    /* TBENCH-INFRA-1: P-35 捕获失败时行数不取 (文件不存在) */
    long lines = cap_ok ? file_lines("tbfix2_out/wp3c_stderr.txt", "(读回)", NULL) : -1;
    printf("  信息  fm.not_exist rc=%d, zzz.foo rc=%d; stderr %ld 行 "
           "(读回格式)\n", rc1, rc2, lines);
    /* TBENCH-INFRA-1: P-35 捕获失败 -> 依赖捕获文件的判据置 SKIP */
    if (!cap_ok) SKIP_CHECK("WP3c");
    else
    CHECK(rc1 == -1 && rc2 == -1 && lines == 2,
          "WP3c: 未知名读回 -1 + 恰一行 stderr (格式不变; 两探测 "
          "%ld 行)", lines);
}

/* ==================== WP3d: 读回无副作用 (渲染逐位一致) ==================== */

static void wp3d_side_effect(void)
{
    printf("== WP3d: 只读性 (读回实例 vs 对照实例渲染 cmp=0) ==\n");
    static const char *const probe[10] = {
        "vcf.cutoff", "vca.level", "env0.attack", "lfo0.frequency",
        "vco0.waveform", "voice.porta_time", "vca.response",
        "lfo1.sync_mode", "env0.value", "voice.gate"
    };
    /* 同种子新实例渲染轨迹逐位一致 (FUZZ-DIAG 双跑逐位一致先例);
     * A 组中途做 10 口读回, B 组不读 —— A==B 即读回无副作用;
     * C/D 对为实例决定性对照 (排除实例间漂移误判) */
    kcapi_synth_t *A = new_synth();
    kcapi_synth_t *B = new_synth();
    kcapi_synth_t *C = new_synth();
    kcapi_synth_t *D = new_synth();
    float buf[BLK];
    kcapi_note_on(A, 60, 100); kcapi_note_on(B, 60, 100);
    kcapi_note_on(C, 60, 100); kcapi_note_on(D, 60, 100);
    for (int b = 0; b < 100; b++) {
        kcapi_render_block(A, buf, BLK);
        kcapi_render_block(B, buf, BLK);
        kcapi_render_block(C, buf, BLK);
        kcapi_render_block(D, buf, BLK);
    }
    static float outA[BLK * 64], outB[BLK * 64];
    static float outC[BLK * 64], outD[BLK * 64];
    float v = 0.0f;
    for (int i = 0; i < 10; i++)
        (void)kcapi_get_param(A, probe[i], &v);  /* A 组 10 口读回 */
    for (int b = 0; b < 64; b++) {
        kcapi_render_block(A, buf, BLK);
        memcpy(outA + b * BLK, buf, sizeof(buf));
        kcapi_render_block(B, buf, BLK);
        memcpy(outB + b * BLK, buf, sizeof(buf));
        kcapi_render_block(C, buf, BLK);
        memcpy(outC + b * BLK, buf, sizeof(buf));
        kcapi_render_block(D, buf, BLK);
        memcpy(outD + b * BLK, buf, sizeof(buf));
    }
    int cmp_ab = memcmp(outA, outB, sizeof(outA));
    int cmp_cd = memcmp(outC, outD, sizeof(outC));
    printf("  信息  读回组 vs 对照 memcmp=%d; 决定性对照对 memcmp=%d\n",
           cmp_ab, cmp_cd);
    CHECK(cmp_ab == 0 && cmp_cd == 0,
          "WP3d: 10 口读回 (DST_PARAM/特殊口/READ 口) 无副作用 "
          "(读回组 cmp=%d, 对照对 cmp=%d, 均=0)", cmp_ab, cmp_cd);
    kcapi_synth_destroy(A);
    kcapi_synth_destroy(B);
    kcapi_synth_destroy(C);
    kcapi_synth_destroy(D);
}

/* ==================== WP3e: 重名组写哪口读哪口 ==================== */

static void wp3e_dup_names(void)
{
    printf("== WP3e: 重名组抽查 (8 组) ==\n");
    /* 重名组: 同名 DST_MOD + DST_PARAM 口并存; 写入经
     * vmatrix_find_param_port 落 DST_PARAM, 读回必须同口 */
    static const struct { const char *name; float wv; } tab[8] = {
        { "vco0.level", 0.55f },      /* + DST_MOD vco0.level */
        { "vco1.waveform", 2.0f },    /* + DST_MOD vco1.waveform */
        { "vco2.fine_tune", 33.0f },  /* + DST_MOD vco2.fine_tune */
        { "lfo0.frequency", 4.5f },   /* + DST_MOD lfo0.frequency */
        { "vcf.cutoff", 1234.0f },    /* + DST_MOD vcf.cutoff */
        { "vcf.resonance", 0.66f },   /* + DST_MOD vcf.resonance */
        { "vca.level", 0.44f },       /* + DST_MOD vca.level */
        { "fm.algorithm", 7.0f },     /* + DST_MOD fm.algorithm */
    };
    kcapi_synth_t *s = new_synth();
    int ok = 0;
    for (int i = 0; i < 8; i++) {
        int pidx = vmatrix_find_param_port(tab[i].name);
        int wrc = kcapi_set_param(s, tab[i].name, tab[i].wv);
        float rb = 0.0f;
        int rrc = kcapi_get_param(s, tab[i].name, &rb);
        int good = (pidx >= 0 &&
                    vmatrix_port(pidx)->kind == VMX_PORT_DST_PARAM &&
                    wrc == 0 && rrc == 0 &&
                    fabsf(rb - tab[i].wv) <= fabsf(tab[i].wv) * 0.01f + 0.5f);
        printf("  信息  %-16s pidx=%d kind=%d 写 %.4g 读 %.4g %s\n",
               tab[i].name, pidx, pidx >= 0 ? vmatrix_port(pidx)->kind : -1,
               (double)tab[i].wv, (double)rb, good ? "OK" : "BAD");
        if (good) ok++;
    }
    kcapi_synth_destroy(s);
    CHECK(ok == 8,
          "WP3e: 重名组 %d/8 写哪口读哪口 (find_param_port 解析为 "
          "DST_PARAM, 读回=写入值)", ok);
}

/* ==================== WP4: alog 旧值链 ==================== */

static void wp4_alog_chain(void)
{
    printf("== WP4: alog 拧 20 个非 fm 参数 (旧值链) ==\n");
    static const char *const params[20] = {
        "vcf.cutoff", "vcf.resonance", "vcf.drive", "vca.level",
        "vca.smooth_ms", "env0.attack", "env0.decay", "env0.sustain_level",
        "env0.release", "env0.hold", "lfo0.frequency", "lfo0.waveform",
        "lfo1.depth", "vco0.waveform", "vco0.frequency", "vco1.level",
        "vco2.pulse_width", "voice.porta_time", "voice.master_tune",
        "voice.engine_mode"
    };
    int cap_ok = capture_stderr("tbfix2_out/wp4_stderr.txt");   /* TBENCH-INFRA-1: P-35 返回值检查 */
    remove("tbfix2_out/wp4_actions.log");   /* alog 追加模式: 重跑前清场 */
    alog_start("tbfix2_out/wp4_actions.log");
    kcapi_synth_t *s = new_synth();
    char exp_old[20][24];   /* 第二写预期 old = 第一写后生效值 (同 %.4g) */
    for (int i = 0; i < 20; i++) {
        alog_set_param(s, params[i], 0.31f + (float)i, "user");   /* 第一写 */
        float eff = 0.0f;
        if (kcapi_get_param(s, params[i], &eff) == 0)
            snprintf(exp_old[i], sizeof(exp_old[i]), "%.4g", (double)eff);
        else
            snprintf(exp_old[i], sizeof(exp_old[i]), "?");
        alog_set_param(s, params[i], 0.77f + (float)i, "user");   /* 第二写 */
    }
    kcapi_synth_destroy(s);
    alog_stop();
    restore_stderr();   /* TBENCH-INFRA-1: P-35 恢复 + 返回值检查 */
    /* TBENCH-INFRA-1: P-35 捕获失败时行数不取 (文件不存在) */
    long elines = cap_ok ? file_lines("tbfix2_out/wp4_stderr.txt", NULL, NULL) : -1;
    /* 解析日志: param 行 old 列不得为 "?"/"?!"; 第二写 old=第一写
     * 后生效值 (int/钳位类按生效值口径, 非写入回显) */
    FILE *f = fopen("tbfix2_out/wp4_actions.log", "r");
    int param_lines = 0, q_lines = 0, chain_ok = 0;
    char line[512];
    char prev_target[64] = "";
    while (f && fgets(line, sizeof(line), f)) {
        char op[32], target[64], detail[128];
        if (sscanf(line, "%*d | %31[^|] | %63[^|] | %127[^|] |",
                   op, target, detail) != 3)
            continue;
        /* 去首尾空格 */
        char *o = op + strspn(op, " ");
        char *t = target + strspn(target, " ");
        char *d = detail + strspn(detail, " ");
        size_t L = strlen(t);
        while (L > 0 && t[L-1] == ' ') t[--L] = '\0';
        if (strncmp(o, "param", 5) != 0) continue;
        param_lines++;
        if (d[0] == '?') {
            q_lines++;
        } else if (strcmp(t, prev_target) == 0) {
            /* 同一参第二写: old 必须等于第一写后的生效值 */
            char oldv[64];
            int k = -1;
            for (int j = 0; j < 20; j++)
                if (strcmp(t, params[j]) == 0) k = j;
            if (k >= 0 && sscanf(d, "%63s ->", oldv) == 1 &&
                strcmp(oldv, exp_old[k]) == 0)
                chain_ok++;
        }
        snprintf(prev_target, sizeof(prev_target), "%s", t);
    }
    if (f) fclose(f);
    printf("  信息  param 行 %d (40 行=20 参×2 写), \"?\"行 %d, "
           "链吻合 %d/20, stderr %ld 行\n",
           param_lines, q_lines, chain_ok, elines);
    CHECK(param_lines == 40 && q_lines == 0 && chain_ok == 20,
          "WP4: 20 参旧值链全真实数值 (param 行 %d/40, \"?\" %d, "
          "链 %d/20)", param_lines, q_lines, chain_ok);
    /* TBENCH-INFRA-1: P-35 捕获失败 -> 依赖捕获文件的判据置 SKIP */
    if (!cap_ok) SKIP_CHECK("WP4b");
    else
    CHECK(elines == 1, "WP4b: WP4 全程 stderr %ld 行 = 1 (0 行刷屏 "
          "+ 1 行 alog 成功落点提示 [LOG-PATH-1])", elines);
}

/* ==================== TBENCH-FIX-3 WP5: 别名写读 / fm.frequency / cable_info ==================== */

/* TBENCH-FIX-3 T-A (P-08/P-09): 四别名写 0.37 同名读回一致 (容差照
 * WP3a 先例, int 口 0.37->0); alog 两写形成 old->new 链, 日志旧值
 * 不得为 "?"/"?!"; 写读全程 stderr 零行 (freopen 捕获同 WP3a 先例) */
static void wp5a_alias_readback(void)
{
    printf("== TBENCH-FIX-3 T-A: 别名写读一致 + 日志旧值真实 + stderr 零行 ==\n");
    static const char *const alias[4] = {
        "vcf.soft_clip", "vcf.smoothing", "vcf.drive_comp", "env0.sustain"
    };
    int cap_ok = capture_stderr("tbfix3_out/t_a_stderr.txt");   /* TBENCH-INFRA-1: P-35 返回值检查 */
    remove("tbfix3_out/t_a_actions.log");   /* alog 追加模式: 重跑前清场 */
    alog_start("tbfix3_out/t_a_actions.log");
    kcapi_synth_t *s = new_synth();
    int ok = 0;
    for (int i = 0; i < 4; i++) {
        alog_set_param(s, alias[i], 0.37f, "user");
        float rb = 0.0f;
        int rrc = kcapi_get_param(s, alias[i], &rb);
        int good = (rrc == 0) && check_consistent(s, alias[i], rb);
        printf("  信息  别名 %-15s 写 0.37 读回 %.4g rc=%d %s\n",
               alias[i], (double)rb, rrc, good ? "OK" : "BAD");
        if (good) ok++;
        alog_set_param(s, alias[i], 0.91f, "user");   /* 第二写: 旧值链 */
    }
    kcapi_synth_destroy(s);
    alog_stop();
    restore_stderr();   /* TBENCH-INFRA-1: P-35 恢复 + 返回值检查 */
    /* TBENCH-INFRA-1: P-35 捕获失败时行数不取 (文件不存在) */
    long elines = cap_ok ? file_lines("tbfix3_out/t_a_stderr.txt", NULL, NULL) : -1;
    /* 日志核对: 8 行 param (4 别名×2 写), old 列无 "?"/"?!" */
    FILE *f = fopen("tbfix3_out/t_a_actions.log", "r");
    int param_lines = 0, q_lines = 0;
    char line[512];
    while (f && fgets(line, sizeof(line), f)) {
        char op[32], target[64], detail[128];
        if (sscanf(line, "%*d | %31[^|] | %63[^|] | %127[^|] |",
                   op, target, detail) != 3)
            continue;
        char *o = op + strspn(op, " ");
        char *d = detail + strspn(detail, " ");
        if (strncmp(o, "param", 5) != 0) continue;
        param_lines++;
        if (d[0] == '?') q_lines++;
    }
    if (f) fclose(f);
    printf("  信息  param 行 %d (8=4 别名×2 写), \"?\"行 %d, stderr %ld 行\n",
           param_lines, q_lines, elines);
    CHECK(ok == 4, "T-A1: 四别名写 0.37 同名读回一致 %d/4 (P-08/P-09)", ok);
    CHECK(param_lines == 8 && q_lines == 0,
          "T-A2: 别名 alog 旧值全真实数值 (param 行 %d/8, \"?\" %d)",
          param_lines, q_lines);
    /* TBENCH-INFRA-1: P-35 捕获失败 -> 依赖捕获文件的判据置 SKIP */
    if (!cap_ok) SKIP_CHECK("T-A3");
    else
    CHECK(elines == 1, "T-A3: 别名写读全程 stderr %ld 行 = 1 (0 行刷屏 "
          "+ 1 行 alog 成功落点提示 [LOG-PATH-1])", elines);
}

/* TBENCH-FIX-3 T-B (P-33): fm.frequency 写 440 读回==440; 写 220 复验
 * 非恒定 (S4 声明走"读真值"路线: base_freq 运行状态只读观测) */
static void wp5b_fm_frequency(void)
{
    printf("== TBENCH-FIX-3 T-B: fm.frequency 读真值 (P-33) ==\n");
    kcapi_synth_t *s = new_synth();
    int wrc = kcapi_set_param(s, "fm.frequency", 440.0f);
    float rb = -1.0f;
    int rrc = kcapi_get_param(s, "fm.frequency", &rb);
    printf("  信息  写 440: wrc=%d rrc=%d 读回 %.4g\n", wrc, rrc, (double)rb);
    CHECK(wrc == 0 && rrc == 0 && fabsf(rb - 440.0f) <= 0.5f,
          "T-B1: fm.frequency 写 440 读回 %.4g == 440 (rc=0)", (double)rb);
    kcapi_set_param(s, "fm.frequency", 220.0f);
    rb = -1.0f;
    rrc = kcapi_get_param(s, "fm.frequency", &rb);
    printf("  信息  写 220: rrc=%d 读回 %.4g\n", rrc, (double)rb);
    CHECK(rrc == 0 && fabsf(rb - 220.0f) <= 0.5f,
          "T-B2: fm.frequency 写 220 读回 %.4g == 220 (非恒定假值)", (double)rb);
    kcapi_synth_destroy(s);
}

/* TBENCH-FIX-3 T-C (P-48): 连线→断开→kcapi_ctrl_cable_info 读该槽,
 * src/dst=="?" 且 active==0 (无幽灵连线); active 槽行为不变用例保留 */
static void wp5c_cable_info(void)
{
    printf("== TBENCH-FIX-3 T-C: cable_info inactive 槽无幽灵连线 (P-48) ==\n");
    kcapi_synth_t *s = new_synth();
    int id = kcapi_cable_ctrl_connect(s, "lfo0.out", "vcf.cutoff", 0.5f);
    CHECK(id >= 0, "T-C1: 连线 lfo0.out->vcf.cutoff 成功 (id=%d)", id);
    int slot = -1, active_ok = 0;
    int nc = kcapi_ctrl_cable_count(s);
    for (int i = 0; i < nc; i++) {
        int cid = -1, act = 0;
        const char *sn = NULL, *dn = NULL;
        float amt = 0.0f;
        kcapi_ctrl_cable_info(s, i, &cid, &sn, &dn, &amt, &act);
        if (cid == id && act) {
            slot = i;
            if (sn && dn && strcmp(sn, "lfo0.out") == 0 &&
                strcmp(dn, "vcf.cutoff") == 0)
                active_ok = 1;
        }
    }
    CHECK(slot >= 0 && active_ok,
          "T-C2: active 槽行为不变 (slot=%d, src/dst 实名可读)", slot);
    kcapi_cable_disconnect_ctrl(s, id);
    int cid = -1, act = -1;
    const char *sn = NULL, *dn = NULL;
    float amt = 0.0f;
    if (slot >= 0)
        kcapi_ctrl_cable_info(s, slot, &cid, &sn, &dn, &amt, &act);
    printf("  信息  断开后槽 %d: active=%d src=%s dst=%s\n",
           slot, act, sn ? sn : "(null)", dn ? dn : "(null)");
    CHECK(slot >= 0 && act == 0 && sn && dn &&
          strcmp(sn, "?") == 0 && strcmp(dn, "?") == 0,
          "T-C3: inactive 槽 src/dst==\"?\" 且 active==0 (幽灵连线消除)");
    kcapi_synth_destroy(s);
}

/* ==================== main ==================== */

int main(void)
{
    printf("== TBENCH-FIX-2 验收: kcapi_get_param 读回面扩展 ==\n");
    printf("口径: sr=%d Hz, 块 %d; 写入代表值 0.37; 种子固定\n\n",
           (int)SR_F, BLK);
    kcapi_vco_bank_init();

    wp3a_survey();
    wp3b_readonly_scan();
    wp3c_unknown();
    wp3d_side_effect();
    wp3e_dup_names();
    wp4_alog_chain();
    wp5a_alias_readback();   /* TBENCH-FIX-3: T-A */
    wp5b_fm_frequency();     /* TBENCH-FIX-3: T-B */
    wp5c_cable_info();       /* TBENCH-FIX-3: T-C */

    printf("\n== 汇总: %d 项检查, %d 项 FAIL, %d 项 SKIP ==\n",
           g_checks, g_fails, g_skips);
    /* TBENCH-INFRA-1: P-35 /dev/tty 恢复失败警示 (保持重定向不动) */
    if (g_stderr_restore_fail > 0)
        printf("WARN: stderr 未能恢复重定向 (%d 次)\n", g_stderr_restore_fail);
    return g_fails ? 1 : 0;
}
