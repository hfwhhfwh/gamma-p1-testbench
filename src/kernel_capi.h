// kernel_capi.h - C wrapper around the gamma-p1 kernel.
// Compiled as C11; exposed to C++ via extern "C".
// This avoids including kernel headers (synth.h/voice.h/vmatrix.h) directly in C++,
// which is necessary because voice.h's forward declaration of struct Vmatrix is
// incompatible with vmatrix.h's anonymous-struct typedef in C++ mode.
//
// 变更摘要 (2026-09-15 TBENCH-FIX-1):
//   1) kcapi_set_param 返回码 void -> int (0=落地, -1=未知名; 既有调用方
//      忽略返回值, 源码兼容): "fm." 分发表补齐算子 EG (DX7 4R4L /
//      legacy 五参) / eg_mode / dxlfo.* / dxpeg.* / ratio; 未知名兜底
//      转发 synth_set_param, 仍不认识的一行调试日志 + 返回 -1,
//      杜绝静默丢弃 (FM-DIAG-1 实测缺口: fm.op0.r4=42 写入被静默丢);
//   2) 新增 kcapi_get_param: "fm." 参数读回 (写入能读回才算通),
//      未知名返回 -1 + 日志。
// 变更摘要 (2026-09-25 TBENCH-FIX-2):
//   kcapi_get_param 读回面扩到全端口表 (非 fm.* 名: DST_PARAM 读模块
//   只读快照/当前生效值, READ 口走 vmatrix_read_src_value 现成快照,
//   DST_MOD/AUDIO_BUF 静默 -1, 真正未知名 -1 + 一行 stderr);
//   新增 kcapi_is_param_port (按名查 DST_PARAM 口)。只增不删。
//
// 变更摘要 (2026-10-26 SNAPSHOT-FIX-1, 测试台侧 WP3):
//   S5: 快照区新增 V5 保存口 —— kcapi_snapshot_save_v5 (内部先存
//   5732B V4 主体再 synth_snapshot_wrap_v5 包成 5740B, cap<5740 或
//   空指针返回 -1, 成功 0) 与 kcapi_snapshot_v5_size (透传内核
//   synth_snapshot_v5_size)。只增不删, 既有 kcapi_snapshot_* 语义不变。

#ifndef KERNEL_CAPI_H
#define KERNEL_CAPI_H

#include <stdint.h>
#include <stddef.h>   /* SNAPSHOT-FIX-1: size_t (V5 快照口声明) */

#ifdef __cplusplus
extern "C" {
#endif

/* Sample rate and block size constants exposed to C++ without including voice.h. */
#define KCAPI_SAMPLE_RATE 48000.0f
#define KCAPI_MAX_BLOCK 64
#define KCAPI_ENGINE_PARALLEL 3

/* Opaque handle to the kernel Synth instance. C++ never sees the definition. */
typedef struct kcapi_synth kcapi_synth_t;

// Lifecycle
/* Must be called once before kcapi_synth_init (fills VCO bandlimited tables). */
void   kcapi_vco_bank_init(void);
kcapi_synth_t *kcapi_synth_create(void);
void   kcapi_synth_destroy(kcapi_synth_t *s);
void   kcapi_synth_init(kcapi_synth_t *s, float sample_rate);

// Performance
void kcapi_note_on(kcapi_synth_t *s, int note, int velocity);
void kcapi_note_off(kcapi_synth_t *s);
void kcapi_pitch_bend(kcapi_synth_t *s, float val);
void kcapi_mod_wheel(kcapi_synth_t *s, float val);
void kcapi_aftertouch(kcapi_synth_t *s, float val);
void kcapi_breath(kcapi_synth_t *s, float val);

// Parameters and cables
/* [TBENCH-FIX-1] 返回 0=参数落地, -1=未知名 (已打日志); 旧版为 void,
 * 既有调用方忽略返回值, 源码兼容。 */
int  kcapi_set_param(kcapi_synth_t *s, const char *name, float value);
/* [TBENCH-FIX-1] 按名读回 (当前覆盖 "fm." 全参数面; 未知名 -1 + 日志)。
 * 读回值写 *out; 返回 0=成功, -1=未知名。 */
/* [TBENCH-FIX-2] 读回面扩到全端口表: "fm." 分支原样在最前; 非 fm.* 名
 * 经 vmatrix_find_param_port 同一套解析 (写哪口读哪口) —— DST_PARAM
 * 口读当前生效值 (模块只读快照/getter), READ 口走 vmatrix_read_src_value
 * 现成快照, DST_MOD/AUDIO_BUF 口返回 -1 且静默 (不刷 stderr), 真正
 * 未知名 -1 + 一行 stderr (格式不变)。 */
int  kcapi_get_param(const kcapi_synth_t *s, const char *name, float *out);
/* [TBENCH-FIX-2] 按名查是否为 DST_PARAM 参数口 (1=是); 供 alog 区分
 * "DST_PARAM 读旧值失败=异常" 与 "READ 口语义无旧值"。 */
int  kcapi_is_param_port(const char *name);
int  kcapi_cable_ctrl_connect(kcapi_synth_t *s, const char *src, const char *dst, float amount);
int  kcapi_cable_audio_connect(kcapi_synth_t *s, const char *src, const char *dst, float amount);
int  kcapi_cable_disconnect_ctrl(kcapi_synth_t *s, int id);
int  kcapi_cable_disconnect_audio(kcapi_synth_t *s, int id);
int  kcapi_cable_set_amount_ctrl(kcapi_synth_t *s, int id, float amount);
int  kcapi_cable_set_amount_audio(kcapi_synth_t *s, int id, float amount);

// Rendering
void kcapi_render_block(kcapi_synth_t *s, float *out, int n);

// Snapshot (opaque byte blob of fixed size)
int  kcapi_snapshot_size(void);
void kcapi_snapshot_save(const kcapi_synth_t *s, void *buf);
void kcapi_snapshot_load(kcapi_synth_t *s, const void *buf);

// Legacy (V1, pre-DX7) snapshot load — additive entry, kernel untouched.
// Accepts a V1 blob (kcapi_snapshot_v1_size() bytes) and converts it
// field-wise via gamma-p1/snapshot_v1.h (new DX7 fields default to neutral,
// so a V1 preset renders bit-identically to when it was saved).
// A V2-sized blob is forwarded to kcapi_snapshot_load for convenience.
// Returns 0 on success, -1 on bad args/unknown size.
int  kcapi_snapshot_v1_size(void);
int  kcapi_snapshot_load_legacy(kcapi_synth_t *s, const void *buf, int len);

/* [SNAPSHOT-FIX-1] V5 快照保存 (5740B = 8B 头 [magic'GP1S'+version=5
 * +CRC16] + 5732B V4 主体) —— save_v5 内部先存 V4 主体再 wrap;
 * cap 不足/空指针返回 -1, 成功 0; size 透传内核。只增不删。 */
size_t kcapi_snapshot_v5_size(void);
int  kcapi_snapshot_save_v5(const kcapi_synth_t *s, void *buf, size_t cap);

// Vmatrix port table access
int         kcapi_num_ports(void);
const char *kcapi_port_name(int idx);
int         kcapi_port_kind(int idx);
int         kcapi_port_adst(int idx);

// Cable table iteration for UI display
int kcapi_ctrl_cable_count(const kcapi_synth_t *s);
void kcapi_ctrl_cable_info(const kcapi_synth_t *s, int idx, int *id, const char **src_name,
                           const char **dst_name, float *amount, int *active);
int kcapi_audio_cable_count(const kcapi_synth_t *s);
void kcapi_audio_cable_info(const kcapi_synth_t *s, int idx, int *id, const char **src_name,
                            const char **dst_name, float *amount, int *active, int *dormant);

// Parameter snapshot getters (params pointers point to module Params structs)
void kcapi_get_vco_params(const kcapi_synth_t *s, int idx, void *params);
void kcapi_get_vcf_params(const kcapi_synth_t *s, void *params);
void kcapi_get_vca_params(const kcapi_synth_t *s, void *params);
void kcapi_get_lfo_params(const kcapi_synth_t *s, int idx, void *params);
void kcapi_get_env_shape_params(const kcapi_synth_t *s, int idx, void *params);
void kcapi_get_env_voice_params(const kcapi_synth_t *s, int idx, void *params);
void kcapi_get_fm_params(const kcapi_synth_t *s, void *params);

// Voice-level getters
float kcapi_get_vco_mix(const kcapi_synth_t *s, int idx);
float kcapi_get_sub_gain(const kcapi_synth_t *s);
float kcapi_get_fm_gain(const kcapi_synth_t *s);
int   kcapi_get_engine_mode(const kcapi_synth_t *s);
float kcapi_get_peak_output(const kcapi_synth_t *s);

// Offline render into caller-owned buffer (samples = seconds * 48000)
void kcapi_render_offline(const kcapi_synth_t *s, float *out, int n_samples);

// LFO runtime controls
void kcapi_lfo_set_phase(kcapi_synth_t *s, int idx, float phase);
void kcapi_lfo_fade_release(kcapi_synth_t *s, int idx);
int  kcapi_get_lfo_sync_mode(const kcapi_synth_t *s, int idx);

/* [TESTBENCH-P3 2026-08-17 纯追加] ENV 面板包络图形用只读封装。
 * 折点/贝塞尔几何不入快照 (见 env.h MODCOV-4 说明), 仅供 UI 绘图读取;
 * 实时输出/播放位置为音频线程状态的宽松读取 (显示用途, 允许一块滞后)。 */

/* 折点模式: 返回折点数 (<=max_n 时填满数组); xs/ys 为折点坐标,
 * seg_curves[i] 为第 i 段 (点 i -> i+1) 的 EnvCurve, 可为 NULL。 */
int kcapi_get_env_bk(const kcapi_synth_t *s, int idx, int max_n,
                     float *xs, float *ys, int *seg_curves);
/* 贝塞尔模式: 返回节点数; 数组收节点坐标与左右控制柄 (相对 dx/dy)
 * 及自动手柄标记, 对应 env.h EnvBezNode 字段。 */
int kcapi_get_env_bez(const kcapi_synth_t *s, int idx, int max_n,
                      float *xs, float *ys,
                      float *hlx, float *hly, float *hrx, float *hry,
                      int *auto_l, int *auto_r);
/* 该 ENV 当前输出值 (voice 缓存的上一块末样值, 0..1)。 */
float kcapi_get_env_output(const kcapi_synth_t *s, int idx);
/* 该 LFO 当前输出值 (voice 缓存的上一块末样值)。 */
float kcapi_get_lfo_output(const kcapi_synth_t *s, int idx);
/* 返回 EnvState; lut_frac = 当前 LUT 归一化位置 (0..1),
 * lut_dur = 活缓冲烘焙总时长 (秒)。显示用途。 */
int kcapi_get_env_runtime(const kcapi_synth_t *s, int idx,
                          float *lut_frac, float *lut_dur);

/* [P0-MODSRC-1 2026-08-28 纯追加] 读取任意 READ 调制源端口当前归一化值
 * (经内核 vmatrix 施工同一读取路径 read_src, 含 voice.velocity/note/gate
 * 归一化); idx 越界或非 READ 口返回 0.0f。观测/测试用途, 非音频路径。 */
float kcapi_get_mod_src_value(const kcapi_synth_t *s, int idx);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_CAPI_H
