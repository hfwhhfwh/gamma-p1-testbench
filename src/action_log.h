/*
 * action_log.h — 测试台操作日志模块 (NOISE-DIAG-1 WP-LOG)
 *
 * 定位: 合成器完工前的常驻监控调试工具。三硬性要求:
 *   1) 纯文本人眼可读 (打开就是对齐表格);
 *   2) 机器可解析 (竖线分隔, 一行一条, UTF-8);
 *   3) 常驻 (启动到退出全程记录)。
 * 日志同时是"可回放脚本": 离线重放可重现内核状态演化
 * (tests/noise_diag_replay.c); source 字段为 GLOBREC 预留。
 *
 * 格式 v1 (冻结):
 *   # gamma-p1 testbench action log v1
 *   # session_start <日期> sr=<采样率> build=<编译期>
 *   t_ms | op | target | old -> new | source          (表头注释行)
 *      0 | engine  | voice.engine_mode | 0 -> 1         | user
 *   1234 | param   | fm.peg_d1l        | 50.00 -> 70.00 | user
 *   1250 | note_on | 60                | vel=100        | user
 *   1310 | warn    | kcapi             | 未知参数名: fm.x | kernel
 * op ∈ param / note_on / note_off / cable+ / cable- / preset_load /
 *      preset_save / engine / warn / state / signal (LOG-PATH-1 新增
 *      op 类型: SIGINT/SIGTERM 优雅退出事件, target="SIGINT|SIGTERM",
 *      detail="graceful shutdown", source="system", 行结构不变)
 *
 * 挂钩纪律 (只挂不改): 内核与既有测试台函数一行不动; 本模块提供
 * alog_* 包装函数, 调用侧 (UI/脚本) 改调包装即完成挂钩。
 *
 * 落盘: 全静态环形缓冲 4096 条 + 每 64 条 flush + 退出 flush;
 * 文件默认 testbench_action.log, 每次启动追加 session 头, 不覆盖
 * 历史 session。host 侧模块: 允许 stdio。
 *
 * 变更摘要 (2026-11-17 LOG-PATH-1, 见 action_log.c 实现侧摘要):
 *   WP1: 打开/写盘失败 stderr 可见化 (P-28), 成功打印绝对落点;
 *   alog_stop 保持幂等 (g_fp==NULL 早退) 供 main.cpp atexit 收口。
 *   格式 v1 行结构冻结, op=signal 为唯一新增 op 类型。
 */

#ifndef ACTION_LOG_H
#define ACTION_LOG_H

#include "kernel_capi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 会话开始: 打开 (追加模式) 日志文件并写 session 头。
 * path==NULL 用默认 "testbench_action.log"。返回 0 成功, -1 失败
 * (fopen 失败时 stderr 打 "[alog] 无法打开日志 <路径>: <原因>",
 * 成功后 stderr 打 "[alog] logging to <绝对路径>"; 失败不崩溃,
 * 后续 alog_* 均为 no-op)。 [LOG-PATH-1] */
int  alog_start(const char *path);

/* 写一条日志 (op/target/detail/source 均为字符串; 本模块不负责语义) */
void alog_log(const char *op, const char *target,
              const char *detail, const char *source);

/* 周期状态摘要 (op=state): 引擎/算法/eg_mode/端口总数/活跃线缆数。
 * 由调用侧每 10s 调一次 (本模块不含定时器)。 */
void alog_state(kcapi_synth_t *s, const char *source);

/* ---- 挂钩包装 (只挂不改: 包装 = 读旧值/调原函数/写日志) ---- */

/* 参数变更: 旧值先经 kcapi_get_param 取 (取不到记 "?"), 调
 * kcapi_set_param, 写 op=param 行; 返回 kcapi_set_param 的返回码,
 * 非 0 (未知名) 时追加 op=warn 行镜像警告事件。
 * [TBENCH-FIX-2] 旧值取不到且该名为 DST_PARAM 口时记 "?!"
 * (异常审计标记); READ/AUDIO 口仍记 "?" (语义无旧值, 正常)。 */
int  alog_set_param(kcapi_synth_t *s, const char *name, float value,
                    const char *source);

/* 演奏事件 */
void alog_note_on(kcapi_synth_t *s, int note, int velocity, const char *source);
void alog_note_off(kcapi_synth_t *s, const char *source);

/* 连线 (ctrl=1 控制线 / 0 音频线); 返回内核线 id */
int  alog_cable_connect(kcapi_synth_t *s, int ctrl, const char *src_name,
                        const char *dst_name, float amount, const char *source);
void alog_cable_disconnect(kcapi_synth_t *s, int ctrl, int id, const char *source);

/* preset 加载/保存 (调用侧已完成实际操作后调用, 只记事件) */
void alog_preset_load(const char *path, const char *source);
void alog_preset_save(const char *path, const char *source);

/* 引擎切换 */
void alog_engine(kcapi_synth_t *s, int old_mode, int new_mode, const char *source);

/* 警告事件镜像 (如 kcapi 未知名日志的镜像行) */
void alog_warn(const char *msg, const char *source);

/* 立即 flush + 关闭 (退出时调用)。幂等: 重复调用/未开启均为 no-op,
 * 可安全挂 atexit 收口。 [LOG-PATH-1] */
void alog_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* ACTION_LOG_H */
