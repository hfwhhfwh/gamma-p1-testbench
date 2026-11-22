// kernel_capi.c - C11 implementation of the kernel wrapper.
// This is the only file in the project that includes the kernel headers;
// it compiles as C, avoiding the C++ struct/typedef conflict.
//
// 变更摘要 (2026-09-15 TBENCH-FIX-1):
//   1) kcapi_set_param "fm." 分发表补齐: 算子 DX7 4R4L (r1~r4/l1~l4)、
//      legacy 五参 EG (eg_ar/eg_d1r/eg_d1l/eg_d2r/eg_rr)、ratio,
//      全局 eg_mode/dxlfo.wave/speed/delay/pmd/amd/sync/dxpeg.r1~r4/
//      l1~l4 (FM-DIAG-1 实测这些名字此前被静默丢弃);
//   2) 兜底: 所有前缀分支的未知名 -> kcapi_forward_param 转发
//      synth_set_param (HOUSEKEEPING-2 已修按名 DST_PARAM 专查);
//      转发也认不出的 -> stderr 一行 "未知参数名" 日志 + 返回 -1,
//      杜绝静默丢弃; 返回码 void -> int (0=落地, -1=未知名);
//   3) 新增 kcapi_get_param: "fm." 全参数面读回 (经 fm_get_params
//      快照字段), 未知名 -1 + 日志。
//   既有可写参数的分支顺序与调用路径不变 (默认即隐身)。
//
// 变更摘要 (2026-09-25 TBENCH-FIX-2):
//   1) kcapi_get_param 读回面扩展到全端口表 (治 ENV-DIAG-1 L4 确诊的
//      报错刷屏 + 日志旧值 "?"): 既有 "fm." 分支原样保留在最前;
//      非 fm.* 名新增 kcapi_get_param_nonfm 分发 —— 经
//      vmatrix_find_param_port 同一套解析 (写哪口读哪口),
//      DST_PARAM 口走模块只读快照/getter 取当前生效值
//      (kcapi_read_param_port, 移植自 ENV-DIAG-1 勘察件
//      env_diag_test.c 的 port_readback 全覆盖只读路径);
//      READ 口走现成 vmatrix_read_src_value 快照 (P0-MODSRC 同路径);
//      DST_MOD/AUDIO_BUF 口语义无参数值: 返回 -1 且【静默】(正常
//      操作不刷屏); 真正未知名: -1 + 一行 stderr (格式不变,
//      诊断价值保留)。
//   2) 新增 kcapi_is_param_port (按名查 DST_PARAM 口), 供 alog
//      区分"读旧值失败=异常 (DST_PARAM)"与"语义无旧值 (READ 口)"。
//   只增不删: 既有签名/分支/日志文案不变; 全部读回为只读观测
//   (内核文件零改动, 不加锁/堆/stdio 进取值路径)。
//
// 变更摘要 (2026-09-28 LEGATO-FIX-1):
//   kcapi_read_param_port 的 voice. 分支加一行 voice.legato_mode 读回
//   (任务书授权破例, 只此一处; 写路径不经此处 —— 写入经
//   kcapi_forward_param -> synth_set_param -> 端口 373 setter, 读回
//   经本行, 写哪口读哪口)。其余分支/文案零改动。
//
// 变更摘要 (2026-10-07 TBENCH-FIX-3):
//   kcapi 别名写通读断 + 假读回 + 幽灵连线修补 (全在测试台侧, 内核
//   零改动; 只【补】读分支, 既有分支判定顺序/返回码/stderr 文案不变):
//   1) P-08: 补三个别名读分支 (与写分支同名同口径) —— vcf.soft_clip
//      读 p.soft_clip_on、vcf.smoothing 读 p.smooth_on、envN.sustain
//      读 p.sustain_level; 别名经 kcapi_get_param_nonfm 新增别名闸门
//      (置于端口表查询之后) 进 kcapi_read_param_port;
//   2) P-09: vcf.drive_comp 补读分支 (VcfParams 含该字段, vcf.c:221
//      真值), 写侧 :207 不动;
//   3) P-33: fm.frequency 读分支改读真值 base_freq (运行状态非音色
//      参数, 只读观测, 同 lfoN.sync_mode 先例), 原恒 0 假成功废止;
//   4) P-48: kcapi_ctrl_cable_info 对 inactive 槽固定返回 "?"
//      (与越界分支同一字面量), active==1 路径逐位不变。
//
// 变更摘要 (2026-10-26 SNAPSHOT-FIX-1, 测试台侧 WP3):
//   1) S5: 新增 kcapi_snapshot_save_v5 / kcapi_snapshot_v5_size ——
//      save_v5 内部先 kcapi_snapshot_save 到 5732B V4 主体栈缓冲,
//      再 synth_snapshot_wrap_v5 包成 5740B V5 (cap<5740 或空指针
//      -1, 成功 0); size 透传 synth_snapshot_v5_size;
//   2) S7: env 读分支补三断口 (与写分支同名同口径) —— envN.sustain_point
//      读 EnvShapeParams.sustain_point_index (写落 env_set_sustain_point
//      同字段)、envN.loop_start/loop_end 读 loop_start_index/
//      loop_end_index; 三名字不在端口表, 别名闸门 kcapi_is_alias_read
//      相应扩三个判定 (置于端口表查询之后, 既有读写行为逐位不变)。

#include "kernel_capi.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>   /* TBENCH-FIX-1: 未知名日志 (host 侧允许 stdio) */

#include "synth.h"
#include "snapshot_v1.h"   /* [DX7] V1 兼容加载 (仅本 TU 使用, 内联静态) */

extern VcoBank vco_default_bank;

/* Cast opaque handle to the real kernel Synth. */
#define KS(s) ((Synth *)(s))

void kcapi_vco_bank_init(void)
{
    vco_bank_init(&vco_default_bank);
}

kcapi_synth_t *kcapi_synth_create(void)
{
    return (kcapi_synth_t *)calloc(1, sizeof(Synth));
}

void kcapi_synth_destroy(kcapi_synth_t *s)
{
    if (s) free(s);
}

void kcapi_synth_init(kcapi_synth_t *s, float sample_rate)
{
    if (!s) return;
    synth_init(KS(s), &vco_default_bank, sample_rate);
}

void kcapi_note_on(kcapi_synth_t *s, int note, int velocity)
{
    if (s) synth_note_on(KS(s), note, velocity);
}

void kcapi_note_off(kcapi_synth_t *s)
{
    if (s) synth_note_off(KS(s));
}

void kcapi_pitch_bend(kcapi_synth_t *s, float val)
{
    if (!s) return;
    KS(s)->globals.pitch_bend = val;
    fm_pitch_bend(&KS(s)->voice.fm, val);
    voice_set_pitch_bend(&KS(s)->voice, val);
}

void kcapi_mod_wheel(kcapi_synth_t *s, float val)
{
    if (s) KS(s)->globals.modwheel = val;
}

void kcapi_aftertouch(kcapi_synth_t *s, float val)
{
    if (s) KS(s)->globals.aftertouch = val;
}

void kcapi_breath(kcapi_synth_t *s, float val)
{
    if (s) KS(s)->globals.breath = val;
}

static int parse_vco_index(const char *name)
{
    if (name[0] == 'v' && name[1] == 'c' && name[2] == 'o' &&
        name[3] >= '0' && name[3] <= '2' && name[4] == '.')
        return name[3] - '0';
    return -1;
}

static int parse_lfo_index(const char *name)
{
    if (name[0] == 'l' && name[1] == 'f' && name[2] == 'o' &&
        name[3] >= '0' && name[3] <= '2' && name[4] == '.')
        return name[3] - '0';
    return -1;
}

static int parse_env_index(const char *name)
{
    if (name[0] == 'e' && name[1] == 'n' && name[2] == 'v' &&
        name[3] >= '0' && name[3] <= '2' && name[4] == '.')
        return name[3] - '0';
    return -1;
}

/* TBENCH-FIX-1: 兜底转发 —— 分发表未覆盖的名字转发内核按名入口
 * (synth_set_param, HOUSEKEEPING-2 后按名专查 DST_PARAM 已修);
 * 转发前查表确认内核认识该名 (DST_PARAM 口或 GLOBAL READ 口),
 * 认不出的明确报"未知参数名"并返回 -1, 杜绝静默丢弃。
 * 返回 0=落地, -1=未知名。 */
static int kcapi_forward_param(Synth *ks, const char *name, float value)
{
    if (vmatrix_find_param_port(name) >= 0) {
        synth_set_param(ks, name, value);
        return 0;
    }
    int idx = vmatrix_find_port(name);
    if (idx >= 0) {
        const VmxPort *p = vmatrix_port(idx);
        if (p->space == VMX_SPACE_GLOBAL && p->kind == VMX_PORT_READ) {
            synth_set_param(ks, name, value);
            return 0;
        }
    }
    fprintf(stderr, "[kcapi] 未知参数名: %s (值 %.6g 未写入)\n",
            name, (double)value);
    return -1;
}

int kcapi_set_param(kcapi_synth_t *s, const char *name, float value)
{
    if (!s || !name) return -1;

    Synth *ks = KS(s);
    Voice *v = &ks->voice;

    if (strncmp(name, "global.", 7) == 0) {
        /* TBENCH-FIX-1: 走兜底 (GLOBAL READ 口直写, 行为不变;
         * 未知名有日志) */
        return kcapi_forward_param(ks, name, value);
    }
    else if (strncmp(name, "voice.", 6) == 0) {
        const char *sub = name + 6;
        if (strcmp(sub, "engine_mode") == 0) voice_set_engine_mode(v, (EngineMode)(int)value);
        else if (strcmp(sub, "vco_mix0") == 0) v->vco_mix[0] = value;
        else if (strcmp(sub, "vco_mix1") == 0) v->vco_mix[1] = value;
        else if (strcmp(sub, "vco_mix2") == 0) v->vco_mix[2] = value;
        else if (strcmp(sub, "sub_gain") == 0) v->sub_gain = value;
        else if (strcmp(sub, "fm_gain") == 0) v->fm_gain = value;
        else if (strcmp(sub, "pitch_bend_range") == 0) voice_set_pitch_bend_range(v, value);
        else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
        return 0;
    }
    else {
        int idx;
        if ((idx = parse_vco_index(name)) >= 0) {
            const char *sub = name + 5;
            if (strcmp(sub, "frequency") == 0) voice_set_vco_frequency(v, idx, value);
            else if (strcmp(sub, "waveform") == 0) voice_set_vco_waveform(v, idx, (int)value);
            else if (strcmp(sub, "fine_tune") == 0) voice_set_vco_fine_tune(v, idx, value);
            else if (strcmp(sub, "pulse_width") == 0) voice_set_vco_pulse_width(v, idx, value);
            else if (strcmp(sub, "level") == 0) voice_set_vco_level(v, idx, value);
            else if (strcmp(sub, "sync_enabled") == 0) voice_set_vco_sync_enabled(v, idx, (int)value);
            else if (strcmp(sub, "morph_mode") == 0) voice_set_vco_morph_mode(v, idx, (int)value);
            else if (strcmp(sub, "noise_seed") == 0) voice_set_vco_seed(v, idx, (unsigned)value);
            else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
            return 0;
        }
        else if (strncmp(name, "vcf.", 4) == 0) {
            const char *sub = name + 4;
            if (strcmp(sub, "cutoff") == 0) voice_set_vcf_cutoff(v, value);
            else if (strcmp(sub, "resonance") == 0) voice_set_vcf_resonance(v, value);
            else if (strcmp(sub, "drive") == 0) voice_set_vcf_drive(v, value);
            else if (strcmp(sub, "mode") == 0) voice_set_vcf_mode(v, (int)value);
            else if (strcmp(sub, "svf_type") == 0) voice_set_vcf_svf_type(v, (int)value);
            else if (strcmp(sub, "oversampling") == 0) voice_set_vcf_oversampling(v, (int)value);
            else if (strcmp(sub, "soft_clip") == 0) voice_set_vcf_soft_clip(v, (int)value, 0);
            else if (strcmp(sub, "smoothing") == 0) voice_set_vcf_smoothing(v, (int)value);
            else if (strcmp(sub, "drive_comp") == 0) voice_set_vcf_soft_clip(v, v->vcf.soft_clip_on, (int)value);
            else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
            return 0;
        }
        else if (strncmp(name, "vca.", 4) == 0) {
            const char *sub = name + 4;
            if (strcmp(sub, "level") == 0) voice_set_vca_level(v, value);
            else if (strcmp(sub, "drive") == 0) voice_set_vca_drive(v, value);
            else if (strcmp(sub, "curve") == 0) voice_set_vca_curve(v, (int)value);
            else if (strcmp(sub, "smooth_ms") == 0) voice_set_vca_gain_smoothing(v, value);
            else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
            return 0;
        }
        else if ((idx = parse_lfo_index(name)) >= 0) {
            const char *sub = name + 5;
            if (strcmp(sub, "frequency") == 0) voice_set_lfo_frequency(v, idx, value);
            else if (strcmp(sub, "waveform") == 0) voice_set_lfo_waveform(v, idx, (int)value);
            else if (strcmp(sub, "depth") == 0) voice_set_lfo_depth(v, idx, value);
            else if (strcmp(sub, "offset") == 0) voice_set_lfo_offset(v, idx, value);
            else if (strcmp(sub, "fade_time") == 0) voice_set_lfo_fade(v, idx, value);
            else if (strcmp(sub, "sync_mode") == 0) voice_set_lfo_sync_mode(v, idx, (int)value);
            else if (strcmp(sub, "seed") == 0) voice_set_lfo_seed(v, idx, (uint32_t)value);
            else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
            return 0;
        }
        else if ((idx = parse_env_index(name)) >= 0) {
            EnvShape *sh = &ks->env_shape[idx];
            const char *sub = name + 5;
            if (strcmp(sub, "predelay") == 0) voice_set_env_predelay(v, idx, value);
            else if (strcmp(sub, "attack") == 0) env_set_attack(sh, value);
            else if (strcmp(sub, "hold") == 0) env_set_hold(sh, value);
            else if (strcmp(sub, "decay") == 0) env_set_decay(sh, value);
            else if (strcmp(sub, "sustain") == 0) env_set_sustain_level(sh, value);
            else if (strcmp(sub, "release") == 0) env_set_release(sh, value);
            else if (strcmp(sub, "attack_curve") == 0) env_set_attack_curve(sh, (int)value);
            else if (strcmp(sub, "decay_curve") == 0) env_set_decay_curve(sh, (int)value);
            else if (strcmp(sub, "mode") == 0) env_set_mode(sh, (int)value);
            else if (strcmp(sub, "sustain_point") == 0) env_set_sustain_point(sh, (int)value);
            else if (strcmp(sub, "loop_enable") == 0) env_set_loop(sh, (int)value, sh->p.loop_start_index, sh->p.loop_end_index);
            else if (strcmp(sub, "loop_start") == 0) env_set_loop(sh, sh->p.loop_enable, (int)value, sh->p.loop_end_index);
            else if (strcmp(sub, "loop_end") == 0) env_set_loop(sh, sh->p.loop_enable, sh->p.loop_start_index, (int)value);
            else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 */
            return 0;
        }
        else if (strncmp(name, "fm.", 3) == 0) {
            FmSynth *fm = &v->fm;
            const char *sub = name + 3;
            if (strncmp(sub, "op", 2) == 0 && sub[2] >= '0' && sub[2] <= '5' && sub[3] == '.') {
                int op = sub[2] - '0';
                const char *p = sub + 4;
                if (strcmp(p, "coarse") == 0) fm_op_set_coarse(fm, op, value);
                else if (strcmp(p, "fine") == 0) fm_op_set_fine(fm, op, value);
                else if (strcmp(p, "detune") == 0) fm_op_set_detune(fm, op, (int)value);
                else if (strcmp(p, "output_level") == 0) fm_op_set_output_level(fm, op, (int)value);
                else if (strcmp(p, "krs") == 0) fm_op_set_krs(fm, op, (int)value);
                else if (strcmp(p, "kls_bp") == 0) fm_op_set_kls(fm, op, (int)value, fm->ops[op].kls_lc, fm->ops[op].kls_ld, fm->ops[op].kls_rc, fm->ops[op].kls_rd);
                else if (strcmp(p, "kls_lc") == 0) fm_op_set_kls(fm, op, fm->ops[op].kls_bp, (int)value, fm->ops[op].kls_ld, fm->ops[op].kls_rc, fm->ops[op].kls_rd);
                else if (strcmp(p, "kls_ld") == 0) fm_op_set_kls(fm, op, fm->ops[op].kls_bp, fm->ops[op].kls_lc, (int)value, fm->ops[op].kls_rc, fm->ops[op].kls_rd);
                else if (strcmp(p, "kls_rc") == 0) fm_op_set_kls(fm, op, fm->ops[op].kls_bp, fm->ops[op].kls_lc, fm->ops[op].kls_ld, (int)value, fm->ops[op].kls_rd);
                else if (strcmp(p, "kls_rd") == 0) fm_op_set_kls(fm, op, fm->ops[op].kls_bp, fm->ops[op].kls_lc, fm->ops[op].kls_ld, fm->ops[op].kls_rc, (int)value);
                else if (strcmp(p, "pms") == 0) fm_op_set_pitch_mod_sens(fm, op, (int)value);
                else if (strcmp(p, "feedback") == 0) fm_set_feedback(fm, op, value);
                else if (strcmp(p, "waveform") == 0) fm_op_set_waveform(fm, op, (int)value);
                else if (strcmp(p, "tone") == 0) fm_op_set_tone(fm, op, value);
                else if (strcmp(p, "vel_sens") == 0) fm_op_set_vel_sens(fm, op, value);
                /* TBENCH-FIX-1: 补齐 DX7 4R4L / legacy 五参 EG / ratio
                 * (此前未知名落入此处静默丢弃, FM-DIAG-1 实测) */
                else if (strcmp(p, "ratio") == 0) fm_set_ratio(fm, op, value);
                else if (p[0] == 'r' && p[1] >= '1' && p[1] <= '4' && p[2] == '\0')
                    fm_op_set_dx_rate(fm, op, p[1] - '1', (int)value);
                else if (p[0] == 'l' && p[1] >= '1' && p[1] <= '4' && p[2] == '\0')
                    fm_op_set_dx_level(fm, op, p[1] - '1', (int)value);
                else if (strcmp(p, "eg_ar") == 0) fm_op_set_eg(fm, op, value, fm->ops[op].eg.d1r, fm->ops[op].eg.d1l, fm->ops[op].eg.d2r, fm->ops[op].eg.rr);
                else if (strcmp(p, "eg_d1r") == 0) fm_op_set_eg(fm, op, fm->ops[op].eg.ar, value, fm->ops[op].eg.d1l, fm->ops[op].eg.d2r, fm->ops[op].eg.rr);
                else if (strcmp(p, "eg_d1l") == 0) fm_op_set_eg(fm, op, fm->ops[op].eg.ar, fm->ops[op].eg.d1r, value, fm->ops[op].eg.d2r, fm->ops[op].eg.rr);
                else if (strcmp(p, "eg_d2r") == 0) fm_op_set_eg(fm, op, fm->ops[op].eg.ar, fm->ops[op].eg.d1r, fm->ops[op].eg.d1l, value, fm->ops[op].eg.rr);
                else if (strcmp(p, "eg_rr") == 0) fm_op_set_eg(fm, op, fm->ops[op].eg.ar, fm->ops[op].eg.d1r, fm->ops[op].eg.d1l, fm->ops[op].eg.d2r, value);
                else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 兜底 */
                return 0;
            } else {
                if (strcmp(sub, "algorithm") == 0) voice_set_fm_algorithm(v, (int)value);
                else if (strcmp(sub, "frequency") == 0) voice_set_fm_frequency(v, value);
                else if (strcmp(sub, "transpose") == 0) voice_set_fm_transpose(v, (int)value);
                else if (strcmp(sub, "pitch_bend_range") == 0) voice_set_fm_pitch_bend_range(v, (int)value);
                else if (strcmp(sub, "porta_on") == 0) voice_set_fm_portamento(v, (int)value, fm->porta_rate);
                else if (strcmp(sub, "porta_rate") == 0) voice_set_fm_portamento(v, fm->porta_on, (int)value);
                else if (strcmp(sub, "seed") == 0) fm_set_seed(fm, (unsigned)value);
                else if (strcmp(sub, "lfo_wave") == 0) fm_lfo_set_wave(fm, (int)value);
                else if (strcmp(sub, "lfo_speed") == 0) fm_lfo_set_speed(fm, (int)value);
                else if (strcmp(sub, "lfo_delay") == 0) fm_lfo_set_delay(fm, (int)value);
                else if (strcmp(sub, "lfo_pmd") == 0) fm_lfo_set_pmd(fm, (int)value);
                else if (strcmp(sub, "lfo_amd") == 0) fm_lfo_set_amd(fm, (int)value);
                else if (strcmp(sub, "lfo_sync") == 0) fm_lfo_set_sync(fm, (int)value);
                else if (strcmp(sub, "peg_ar") == 0) fm_peg_set(fm, value, fm->peg.d1r, fm->peg.d1l, fm->peg.d2r, fm->peg.rr);
                else if (strcmp(sub, "peg_d1r") == 0) fm_peg_set(fm, fm->peg.ar, value, fm->peg.d1l, fm->peg.d2r, fm->peg.rr);
                else if (strcmp(sub, "peg_d1l") == 0) fm_peg_set(fm, fm->peg.ar, fm->peg.d1r, value, fm->peg.d2r, fm->peg.rr);
                else if (strcmp(sub, "peg_d2r") == 0) fm_peg_set(fm, fm->peg.ar, fm->peg.d1r, fm->peg.d1l, value, fm->peg.rr);
                else if (strcmp(sub, "peg_rr") == 0) fm_peg_set(fm, fm->peg.ar, fm->peg.d1r, fm->peg.d1l, fm->peg.d2r, value);
                /* TBENCH-FIX-1: 补齐 eg_mode / key_sync / DX7 LFO / DX7 PEG */
                else if (strcmp(sub, "eg_mode") == 0) fm_set_eg_mode(fm, (int)value);
                else if (strcmp(sub, "key_sync") == 0) fm_set_key_sync(fm, (int)value);
                else if (strcmp(sub, "dxlfo.wave") == 0) fm_dxlfo_set_wave(fm, (int)value);
                else if (strcmp(sub, "dxlfo.speed") == 0) fm_lfo_set_speed(fm, (int)value);
                else if (strcmp(sub, "dxlfo.delay") == 0) fm_lfo_set_delay(fm, (int)value);
                else if (strcmp(sub, "dxlfo.pmd") == 0) fm_lfo_set_pmd(fm, (int)value);
                else if (strcmp(sub, "dxlfo.amd") == 0) fm_lfo_set_amd(fm, (int)value);
                else if (strcmp(sub, "dxlfo.sync") == 0) fm_lfo_set_sync(fm, (int)value);
                else if (strncmp(sub, "dxpeg.r", 7) == 0 && sub[7] >= '1' && sub[7] <= '4' && sub[8] == '\0') {
                    int r[4], l[4];
                    memcpy(r, fm->dxpeg_r, sizeof(r));
                    memcpy(l, fm->dxpeg_l, sizeof(l));
                    r[sub[7] - '1'] = (int)value;
                    fm_dxpeg_set(fm, r, l);
                }
                else if (strncmp(sub, "dxpeg.l", 7) == 0 && sub[7] >= '1' && sub[7] <= '4' && sub[8] == '\0') {
                    int r[4], l[4];
                    memcpy(r, fm->dxpeg_r, sizeof(r));
                    memcpy(l, fm->dxpeg_l, sizeof(l));
                    l[sub[7] - '1'] = (int)value;
                    fm_dxpeg_set(fm, r, l);
                }
                else return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1 兜底 */
                return 0;
            }
            return 0;
        }
        else {
            return kcapi_forward_param(ks, name, value);   /* TBENCH-FIX-1: 原样转发 + 未知名日志 */
        }
    }
}

/* TBENCH-FIX-1: "fm." 按名读回 (写入能读回才算通)。经 fm_get_params
 * 快照字段, 覆盖算子全参数面与全局参数; 未知名 -1 + 日志。 */
/* TBENCH-FIX-2: 非 fm.* DST_PARAM 口当前生效值只读观测 —— 移植自
 * ENV-DIAG-1 勘察件 env_diag_test.c 的 port_readback (vco/vcf/vca/
 * lfo/env/voice 全覆盖), 口径 = 内核现有只读 getter/参数快照;
 * vca.response (deprecated, 快照外) 与 lfoN.sync_mode (运行时状态)
 * 读结构体字段 —— 只读观测, 内核零改动。返回 0=可读, -1=无路径。 */
static int kcapi_read_param_port(const Synth *ks, const char *name, float *out)
{
    int idx = parse_vco_index(name);
    if (idx >= 0) {
        VcoParams p;
        vco_get_params(&ks->voice.vco[idx], &p);
        const char *sub = name + 5;
        if (strcmp(sub, "frequency") == 0) *out = p.frequency;
        else if (strcmp(sub, "fine_tune") == 0) *out = p.fine_tune_cents;
        else if (strcmp(sub, "pulse_width") == 0) *out = p.pulse_width;
        else if (strcmp(sub, "level") == 0) *out = p.level;
        else if (strcmp(sub, "waveform") == 0) *out = (float)p.waveform;
        else if (strcmp(sub, "sync_enabled") == 0) *out = (float)p.sync_enabled;
        else if (strcmp(sub, "morph_mode") == 0) *out = (float)p.morph_mode;
        else if (strcmp(sub, "noise_seed") == 0) *out = (float)p.noise_seed;
        else return -1;
        return 0;
    }
    if (strncmp(name, "vcf.", 4) == 0) {
        VcfParams p;
        vcf_get_params(&ks->voice.vcf, &p);
        const char *sub = name + 4;
        if (strcmp(sub, "cutoff") == 0) *out = p.cutoff;
        else if (strcmp(sub, "resonance") == 0) *out = p.resonance;
        else if (strcmp(sub, "drive") == 0) *out = p.drive_db;
        else if (strcmp(sub, "mode") == 0) *out = (float)p.mode;
        else if (strcmp(sub, "svf_type") == 0) *out = (float)p.svf_type;
        else if (strcmp(sub, "oversampling") == 0) *out = (float)p.oversampling;
        else if (strcmp(sub, "soft_clip_on") == 0) *out = (float)p.soft_clip_on;
        else if (strcmp(sub, "smooth_on") == 0) *out = (float)p.smooth_on;
        /* TBENCH-FIX-3: P-08 别名读分支 —— 与写分支 (kernel_capi.c
         * kcapi_set_param vcf. 段) 同名同口径; 别名不在端口表, 经
         * kcapi_get_param_nonfm 别名闸门到达, 端口原名分支保留不动 */
        else if (strcmp(sub, "soft_clip") == 0) *out = (float)p.soft_clip_on;
        else if (strcmp(sub, "smoothing") == 0) *out = (float)p.smooth_on;
        /* TBENCH-FIX-3: P-09 vcf.drive_comp 读分支 —— VcfParams 含该
         * 字段 (vcf.c vcf_get_params 真值), 写侧分支不动 */
        else if (strcmp(sub, "drive_comp") == 0) *out = (float)p.drive_comp;
        else return -1;
        return 0;
    }
    if (strncmp(name, "vca.", 4) == 0) {
        VcaParams p;
        vca_get_params(&ks->voice.vca, &p);
        const char *sub = name + 4;
        if (strcmp(sub, "level") == 0) *out = p.level;
        else if (strcmp(sub, "drive") == 0) *out = p.drive;
        else if (strcmp(sub, "curve") == 0) *out = (float)p.curve;
        else if (strcmp(sub, "smooth_ms") == 0) *out = p.smooth_ms;
        /* vca.response: deprecated 不进 VcaParams 快照 (vca.h
         * HOUSEKEEPING-1), 但 Vca.response 字段由 vca_set_response
         * 真实维护 —— 读字段即当前生效值 (只读观测) */
        else if (strcmp(sub, "response") == 0) *out = (float)ks->voice.vca.response;
        else return -1;
        return 0;
    }
    idx = parse_lfo_index(name);
    if (idx >= 0) {
        LfoParams p;
        lfo_get_params(&ks->voice.lfo[idx], &p);
        const char *sub = name + 5;
        if (strcmp(sub, "frequency") == 0) *out = p.frequency;
        else if (strcmp(sub, "waveform") == 0) *out = (float)p.waveform;
        else if (strcmp(sub, "depth") == 0) *out = p.depth;
        else if (strcmp(sub, "offset") == 0) *out = p.offset;
        else if (strcmp(sub, "fade_time") == 0) *out = p.fade_time;
        else if (strcmp(sub, "seed") == 0) *out = (float)p.seed;
        /* lfoN.sync_mode: 运行时状态, 快照外 —— 与既有
         * kcapi_get_lfo_sync_mode 同一字段 (只读观测) */
        else if (strcmp(sub, "sync_mode") == 0) *out = (float)ks->voice.lfo[idx].sync_mode;
        else return -1;
        return 0;
    }
    idx = parse_env_index(name);
    if (idx >= 0) {
        const char *sub = name + 5;
        if (strcmp(sub, "predelay") == 0) {
            EnvVoiceParams p;
            env_get_params(&ks->voice.env[idx], &p);
            *out = p.predelay;
            return 0;
        }
        EnvShapeParams p;
        env_shape_get_params(&ks->env_shape[idx], &p);
        if (strcmp(sub, "attack") == 0) *out = p.attack_time;
        else if (strcmp(sub, "hold") == 0) *out = p.hold_time;
        else if (strcmp(sub, "decay") == 0) *out = p.decay_time;
        else if (strcmp(sub, "sustain_level") == 0) *out = p.sustain_level;
        /* TBENCH-FIX-3: P-08 别名读分支 envN.sustain —— 与写分支
         * (kcapi_set_param env 段 env_set_sustain_level) 同名同口径;
         * 端口原名 sustain_level 分支保留不动 */
        else if (strcmp(sub, "sustain") == 0) *out = p.sustain_level;
        else if (strcmp(sub, "release") == 0) *out = p.release_time;
        else if (strcmp(sub, "attack_curve") == 0) *out = (float)p.attack_curve;
        else if (strcmp(sub, "decay_curve") == 0) *out = (float)p.decay_curve;
        else if (strcmp(sub, "mode") == 0) *out = (float)p.mode;
        else if (strcmp(sub, "sustain_point_index") == 0) *out = (float)p.sustain_point_index;
        /* SNAPSHOT-FIX-1: S7 别名读断口 —— envN.sustain_point /
         * loop_start / loop_end 与写分支同名同口径 (写: kcapi_set_param
         * env 段 env_set_sustain_point / env_set_loop, 落同名字段);
         * 端口表原名 sustain_point_index 分支保留不动 */
        else if (strcmp(sub, "sustain_point") == 0) *out = (float)p.sustain_point_index;
        else if (strcmp(sub, "loop_start") == 0) *out = (float)p.loop_start_index;
        else if (strcmp(sub, "loop_end") == 0) *out = (float)p.loop_end_index;
        else if (strcmp(sub, "loop_enable") == 0) *out = (float)p.loop_enable;
        else return -1;
        return 0;
    }
    if (strncmp(name, "voice.", 6) == 0) {
        const char *sub = name + 6;
        if (strcmp(sub, "porta_time") == 0) *out = ks->voice.porta_time;
        else if (strcmp(sub, "master_tune") == 0) *out = ks->voice.master_tune;
        else if (strcmp(sub, "transpose") == 0) *out = (float)ks->voice.transpose;
        else if (strcmp(sub, "engine_mode") == 0) *out = (float)ks->voice.engine_mode;
        else if (strcmp(sub, "legato_mode") == 0) *out = (float)ks->voice.legato_mode;   /* LEGATO-FIX-1: 任务书授权破例一行 (读回面补新口) */
        else if (strcmp(sub, "vco_mix0") == 0) *out = ks->voice.vco_mix[0];
        else if (strcmp(sub, "vco_mix1") == 0) *out = ks->voice.vco_mix[1];
        else if (strcmp(sub, "vco_mix2") == 0) *out = ks->voice.vco_mix[2];
        else if (strcmp(sub, "sub_gain") == 0) *out = ks->voice.sub_gain;
        else if (strcmp(sub, "fm_gain") == 0) *out = ks->voice.fm_gain;
        else if (strcmp(sub, "pitch_bend_range") == 0) *out = ks->voice.pitch_bend_range;
        else return -1;
        return 0;
    }
    return -1;
}

/* TBENCH-FIX-3: P-08/P-09 别名判定 —— kcapi_set_param 写分支认识但
 * 端口表没有的别名 (vcf.soft_clip / vcf.smoothing / vcf.drive_comp /
 * envN.sustain, N=0..2); 判定与写分支同名, 读口径见
 * kcapi_read_param_port 对应 TBENCH-FIX-3 分支。
 * SNAPSHOT-FIX-1: S7 别名面扩 envN.sustain_point / loop_start /
 * loop_end (写分支认识, 端口表原名 sustain_point_index), 读口径见
 * kcapi_read_param_port 对应 SNAPSHOT-FIX-1 分支。 */
static int kcapi_is_alias_read(const char *name)
{
    if (strncmp(name, "vcf.", 4) == 0) {
        const char *sub = name + 4;
        return strcmp(sub, "soft_clip") == 0 ||
               strcmp(sub, "smoothing") == 0 ||
               strcmp(sub, "drive_comp") == 0;
    }
    if (parse_env_index(name) >= 0) {
        const char *sub = name + 5;
        return strcmp(sub, "sustain") == 0 ||
               strcmp(sub, "sustain_point") == 0 ||   /* SNAPSHOT-FIX-1: S7 */
               strcmp(sub, "loop_start") == 0 ||      /* SNAPSHOT-FIX-1: S7 */
               strcmp(sub, "loop_end") == 0;          /* SNAPSHOT-FIX-1: S7 */
    }
    return 0;
}

/* TBENCH-FIX-2: 非 fm.* 名读回分发 —— 先经 vmatrix_find_param_port
 * (与 synth_set_param 写入同一套解析: 写哪口读哪口, 重名组不串);
 * 命中 DST_PARAM 走 kcapi_read_param_port 只读快照; 未命中再查全表:
 * READ 口走现成 vmatrix_read_src_value 快照 (P0-MODSRC 同一路径,
 * 只读观测), DST_MOD/AUDIO_BUF 语义无参数值返回 -1【静默】(边界
 * 情形 2: 正常操作不刷屏); 全表都不认识才是真正未知名: -1 + 一行
 * stderr (格式与既有 "(读回)" 行一致, 诊断价值保留)。 */
static int kcapi_get_param_nonfm(const kcapi_synth_t *s, const char *name,
                                 float *out)
{
    if (vmatrix_find_param_port(name) >= 0) {
        /* DST_PARAM: 读当前生效值 (landed 语义; 钳位/编码/替换类口
         * 读回内核实际生效值, 非回显写入值) */
        return kcapi_read_param_port(KS(s), name, out);
    }
    /* TBENCH-FIX-3: P-08/P-09 别名闸门 —— kcapi_set_param 写分支认识
     * 但端口表没有的四个别名 (vcf.soft_clip/vcf.smoothing/
     * vcf.drive_comp/envN.sustain), 只补读不新增端口; 判定置于端口表
     * 查询之后, 既有 374 口读写行为与未知名 stderr 逐位不变 */
    if (kcapi_is_alias_read(name))
        return kcapi_read_param_port(KS(s), name, out);
    int idx = vmatrix_find_port(name);
    if (idx >= 0) {
        const VmxPort *p = vmatrix_port(idx);
        if (p->kind == VMX_PORT_READ) {
            *out = vmatrix_read_src_value(&KS(s)->voice, &KS(s)->globals,
                                          (uint16_t)idx);
            return 0;
        }
        return -1;   /* DST_MOD / AUDIO_BUF: 静默 */
    }
    fprintf(stderr, "[kcapi] 未知参数名(读回): %s\n", name);
    return -1;
}

/* TBENCH-FIX-2: 按名查是否为 DST_PARAM 参数口 (供 alog 区分
 * "读旧值失败=异常" 与 "语义无旧值"); 与写入路径同一套解析。 */
int kcapi_is_param_port(const char *name)
{
    if (!name) return 0;
    return vmatrix_find_param_port(name) >= 0 ? 1 : 0;
}

int kcapi_get_param(const kcapi_synth_t *s, const char *name, float *out)
{
    if (!s || !name || !out) return -1;
    if (strncmp(name, "fm.", 3) != 0)
        return kcapi_get_param_nonfm(s, name, out);   /* TBENCH-FIX-2 */
    FmParams fp;
    kcapi_get_fm_params(s, &fp);
    const char *sub = name + 3;
    if (strncmp(sub, "op", 2) == 0 && sub[2] >= '0' && sub[2] <= '5' && sub[3] == '.') {
        int op = sub[2] - '0';
        const FmOpParams *o = &fp.ops[op];
        const char *p = sub + 4;
        if (strcmp(p, "coarse") == 0) *out = o->coarse;
        else if (strcmp(p, "fine") == 0) *out = o->fine;
        else if (strcmp(p, "detune") == 0) *out = (float)o->detune;
        else if (strcmp(p, "output_level") == 0) *out = (float)o->output_level;
        else if (strcmp(p, "ratio") == 0) *out = o->coarse;   /* ratio 口 = coarse+fine=0 语义, 读回 coarse */
        else if (strcmp(p, "feedback") == 0) *out = o->feedback;
        else if (strcmp(p, "krs") == 0) *out = (float)o->krs;
        else if (strcmp(p, "kls_bp") == 0) *out = (float)o->kls_bp;
        else if (strcmp(p, "kls_lc") == 0) *out = (float)o->kls_lc;
        else if (strcmp(p, "kls_ld") == 0) *out = (float)o->kls_ld;
        else if (strcmp(p, "kls_rc") == 0) *out = (float)o->kls_rc;
        else if (strcmp(p, "kls_rd") == 0) *out = (float)o->kls_rd;
        else if (strcmp(p, "pms") == 0) *out = (float)o->pms;
        else if (strcmp(p, "waveform") == 0) *out = (float)o->waveform;
        else if (strcmp(p, "tone") == 0) *out = o->tone_cutoff;
        else if (strcmp(p, "vel_sens") == 0) *out = o->vel_sens;
        else if (strcmp(p, "eg_ar") == 0) *out = o->eg_ar;
        else if (strcmp(p, "eg_d1r") == 0) *out = o->eg_d1r;
        else if (strcmp(p, "eg_d1l") == 0) *out = o->eg_d1l;
        else if (strcmp(p, "eg_d2r") == 0) *out = o->eg_d2r;
        else if (strcmp(p, "eg_rr") == 0) *out = o->eg_rr;
        else if (strcmp(p, "fixed") == 0) *out = (float)o->opmode;
        else if (strcmp(p, "mute") == 0) *out = (float)o->mute;
        else if (strcmp(p, "ams") == 0) *out = (float)o->ams;
        else if (strcmp(p, "vel_sens7") == 0) *out = (float)o->velsens7;
        else if (strcmp(p, "feedback7") == 0) *out = (float)o->dx_feedback7;
        else if (p[0] == 'r' && p[1] >= '1' && p[1] <= '4' && p[2] == '\0')
            *out = (float)o->dx_r[p[1] - '1'];
        else if (p[0] == 'l' && p[1] >= '1' && p[1] <= '4' && p[2] == '\0')
            *out = (float)o->dx_l[p[1] - '1'];
        else {
            /* TBENCH-FIX-2: fm 分支不认识的 fm.* 名先查端口表
             * (fm.opN.level=DST_MOD 静默 -1 等); 全表不识才打
             * stderr (格式不变) —— 兜底在 kcapi_get_param_nonfm */
            return kcapi_get_param_nonfm(s, name, out);
        }
        return 0;
    }
    if (strcmp(sub, "algorithm") == 0) *out = (float)fp.algorithm;
    else if (strcmp(sub, "frequency") == 0) *out = KS(s)->voice.fm.base_freq;   /* TBENCH-FIX-3: P-33 读真值 —— base_freq 是运行状态非音色参数 (fm.h:272), 只读观测同 lfoN.sync_mode 先例; 原恒 0 假成功废止 */
    else if (strcmp(sub, "transpose") == 0) *out = (float)fp.transpose;
    else if (strcmp(sub, "pitch_bend_range") == 0) *out = (float)fp.bend_range;
    else if (strcmp(sub, "porta_on") == 0) *out = (float)fp.porta_on;
    else if (strcmp(sub, "porta_rate") == 0) *out = (float)fp.porta_rate;
    else if (strcmp(sub, "seed") == 0) *out = (float)fp.seed;
    else if (strcmp(sub, "lfo_wave") == 0) *out = (float)fp.lfo_wave;
    else if (strcmp(sub, "lfo_speed") == 0) *out = (float)fp.lfo_speed;
    else if (strcmp(sub, "lfo_delay") == 0) *out = (float)fp.lfo_delay;
    else if (strcmp(sub, "lfo_pmd") == 0) *out = (float)fp.lfo_pmd;
    else if (strcmp(sub, "lfo_amd") == 0) *out = (float)fp.lfo_amd;
    else if (strcmp(sub, "lfo_sync") == 0) *out = (float)fp.lfo_sync;
    else if (strcmp(sub, "peg_ar") == 0) *out = fp.peg_ar;
    else if (strcmp(sub, "peg_d1r") == 0) *out = fp.peg_d1r;
    else if (strcmp(sub, "peg_d1l") == 0) *out = fp.peg_d1l;
    else if (strcmp(sub, "peg_d2r") == 0) *out = fp.peg_d2r;
    else if (strcmp(sub, "peg_rr") == 0) *out = fp.peg_rr;
    else if (strcmp(sub, "eg_mode") == 0) *out = (float)fp.eg_mode;
    else if (strcmp(sub, "key_sync") == 0) *out = (float)fp.key_sync;
    else if (strcmp(sub, "dxlfo.wave") == 0) *out = (float)fp.lfo_wave;  /* 存储共享, DX7 顺序见 fm_dxlfo_set_wave */
    else if (strcmp(sub, "dxlfo.speed") == 0) *out = (float)fp.lfo_speed;
    else if (strcmp(sub, "dxlfo.delay") == 0) *out = (float)fp.lfo_delay;
    else if (strcmp(sub, "dxlfo.pmd") == 0) *out = (float)fp.lfo_pmd;
    else if (strcmp(sub, "dxlfo.amd") == 0) *out = (float)fp.lfo_amd;
    else if (strcmp(sub, "dxlfo.sync") == 0) *out = (float)fp.lfo_sync;
    else if (strncmp(sub, "dxpeg.r", 7) == 0 && sub[7] >= '1' && sub[7] <= '4' && sub[8] == '\0')
        *out = (float)fp.dxpeg_r[sub[7] - '1'];
    else if (strncmp(sub, "dxpeg.l", 7) == 0 && sub[7] >= '1' && sub[7] <= '4' && sub[8] == '\0')
        *out = (float)fp.dxpeg_l[sub[7] - '1'];
    else {
        /* TBENCH-FIX-2: 同上当算子分支 —— 先查端口表 (fm.out=READ 接
         * 真实值, fm.buf/pm_audio=AUDIO_BUF 静默 -1), 全表不识才
         * stderr (fm.not_exist 格式不变, 兜底在 kcapi_get_param_nonfm) */
        return kcapi_get_param_nonfm(s, name, out);
    }
    return 0;
}

int kcapi_cable_ctrl_connect(kcapi_synth_t *s, const char *src, const char *dst, float amount)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 0, src, dst, amount, 0);
}

int kcapi_cable_audio_connect(kcapi_synth_t *s, const char *src, const char *dst, float amount)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 1, src, dst, amount, 0);
}

int kcapi_cable_disconnect_ctrl(kcapi_synth_t *s, int id)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 2, NULL, NULL, 0.0f, id);
}

int kcapi_cable_disconnect_audio(kcapi_synth_t *s, int id)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 3, NULL, NULL, 0.0f, id);
}

int kcapi_cable_set_amount_ctrl(kcapi_synth_t *s, int id, float amount)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 4, NULL, NULL, amount, id);
}

int kcapi_cable_set_amount_audio(kcapi_synth_t *s, int id, float amount)
{
    if (!s) return -1;
    return synth_cable_op(KS(s), 5, NULL, NULL, amount, id);
}

void kcapi_render_block(kcapi_synth_t *s, float *out, int n)
{
    if (s && out && n > 0) synth_render_block(KS(s), out, n);
}

int kcapi_snapshot_size(void)
{
    return (int)sizeof(SynthSnapshot);
}

void kcapi_snapshot_save(const kcapi_synth_t *s, void *buf)
{
    if (s && buf) synth_snapshot(KS(s), (SynthSnapshot *)buf);
}

void kcapi_snapshot_load(kcapi_synth_t *s, const void *buf)
{
    if (s && buf) synth_restore(KS(s), (const SynthSnapshot *)buf);
}

/* [DX7] V1 (5104B, DX7 扩展前布局) preset 加载入口: 纯追加, 不动既有
 * kcapi_snapshot_* 语义。按长度分派: V1 -> snapshot_v1.h 字段级转换
 * (新 DX7 字段全 0 = EXT 中性, 渲染与保存时逐位一致); V2 -> 原路径。
 * [PLAY-FEEL-1] 分发下沉到 snapshot_v1.h 的 synth_restore_blob:
 * V1=5104 字段级转换 / V2=5716 前缀拷贝+新字段中性 / V3=5728 前缀
 * 拷贝 [LEGATO-FIX-1] / V4=5732 直接加载。 */
int kcapi_snapshot_v1_size(void)
{
    return (int)SNAPSHOT_V1_SIZE;
}

int kcapi_snapshot_load_legacy(kcapi_synth_t *s, const void *buf, int len)
{
    if (!s || !buf) return -1;
    return synth_restore_blob(KS(s), buf, (size_t)len);   /* PLAY-FEEL-1: V1/V2/V3 分发 */
}

/* SNAPSHOT-FIX-1: V5 快照保存 (5740B = 8B 头 + 5732B V4 主体)。
 * save_v5: 内部先 kcapi_snapshot_save 到 5732B V4 主体栈缓冲, 再
 * synth_snapshot_wrap_v5 包 V5 头; cap<5740 或空指针返回 -1, 成功 0。
 * size: 透传 synth_snapshot_v5_size。只增不删, 既有 kcapi_snapshot_*
 * 语义不变。 */
size_t kcapi_snapshot_v5_size(void)
{
    return synth_snapshot_v5_size();
}

int kcapi_snapshot_save_v5(const kcapi_synth_t *s, void *buf, size_t cap)
{
    if (!s || !buf) return -1;
    if (cap < synth_snapshot_v5_size()) return -1;
    SynthSnapshot body;   /* SNAPSHOT-FIX-1: 5732B V4 主体栈缓冲 */
    synth_snapshot(KS(s), &body);
    return synth_snapshot_wrap_v5(&body, buf);
}

int kcapi_num_ports(void)
{
    return vmatrix_num_ports();
}

const char *kcapi_port_name(int idx)
{
    const VmxPort *p = vmatrix_port(idx);
    return p ? p->name : NULL;
}

int kcapi_port_kind(int idx)
{
    const VmxPort *p = vmatrix_port(idx);
    return p ? p->kind : -1;
}

int kcapi_port_adst(int idx)
{
    const VmxPort *p = vmatrix_port(idx);
    return p ? p->adst : -1;
}

int kcapi_ctrl_cable_count(const kcapi_synth_t *s)
{
    if (!s) return 0;
    return VMX_MAX_CABLES;
}

void kcapi_ctrl_cable_info(const kcapi_synth_t *s, int idx, int *id, const char **src_name,
                           const char **dst_name, float *amount, int *active)
{
    if (!s || idx < 0 || idx >= VMX_MAX_CABLES) return;
    const VmxCable *c = &KS(s)->vmatrix.cables[idx];
    if (id) *id = c->id;
    /* TBENCH-FIX-3: P-48 inactive 槽不按陈旧 src/dst 报端口名 (UI 连线
     * 列表幽灵连线), 固定 "?" (与既有越界分支同一字面量); id/amount/
     * active 行为不动, active==1 路径逐位不变 */
    if (src_name) *src_name = (c->active && c->src < (uint16_t)vmatrix_num_ports()) ? vmatrix_port(c->src)->name : "?";
    if (dst_name) *dst_name = (c->active && c->dst < (uint16_t)vmatrix_num_ports()) ? vmatrix_port(c->dst)->name : "?";
    if (amount) *amount = c->amount;
    if (active) *active = c->active;
}

int kcapi_audio_cable_count(const kcapi_synth_t *s)
{
    (void)s;
    return VMX_MAX_ACABLES;
}

void kcapi_audio_cable_info(const kcapi_synth_t *s, int idx, int *id, const char **src_name,
                            const char **dst_name, float *amount, int *active, int *dormant)
{
    if (!s || idx < 0 || idx >= VMX_MAX_ACABLES) return;
    const VmxAcable *a = &KS(s)->vmatrix.acables[idx];
    if (id) *id = a->id;
    if (src_name) *src_name = (a->src_buf < (uint16_t)vmatrix_num_ports()) ? vmatrix_port(a->src_buf)->name : "?";
    if (dst_name) *dst_name = (a->dst_port < (uint16_t)vmatrix_num_ports()) ? vmatrix_port(a->dst_port)->name : "?";
    if (amount) *amount = a->amount;
    if (active) *active = a->active;
    if (dormant) *dormant = a->dormant;
}

void kcapi_get_vco_params(const kcapi_synth_t *s, int idx, void *params)
{
    if (s && idx >= 0 && idx < 3 && params) vco_get_params(&KS(s)->voice.vco[idx], (VcoParams *)params);
}

void kcapi_get_vcf_params(const kcapi_synth_t *s, void *params)
{
    if (s && params) vcf_get_params(&KS(s)->voice.vcf, (VcfParams *)params);
}

void kcapi_get_vca_params(const kcapi_synth_t *s, void *params)
{
    if (s && params) vca_get_params(&KS(s)->voice.vca, (VcaParams *)params);
}

void kcapi_get_lfo_params(const kcapi_synth_t *s, int idx, void *params)
{
    if (s && idx >= 0 && idx < 3 && params) lfo_get_params(&KS(s)->voice.lfo[idx], (LfoParams *)params);
}

void kcapi_get_env_shape_params(const kcapi_synth_t *s, int idx, void *params)
{
    if (s && idx >= 0 && idx < 3 && params) env_shape_get_params(&KS(s)->env_shape[idx], (EnvShapeParams *)params);
}

void kcapi_get_env_voice_params(const kcapi_synth_t *s, int idx, void *params)
{
    if (s && idx >= 0 && idx < 3 && params) env_get_params(&KS(s)->voice.env[idx], (EnvVoiceParams *)params);
}

void kcapi_get_fm_params(const kcapi_synth_t *s, void *params)
{
    if (s && params) fm_get_params(&KS(s)->voice.fm, (FmParams *)params);
}

float kcapi_get_vco_mix(const kcapi_synth_t *s, int idx)
{
    if (s && idx >= 0 && idx < 3) return KS(s)->voice.vco_mix[idx];
    return 0.0f;
}

float kcapi_get_sub_gain(const kcapi_synth_t *s)
{
    return s ? KS(s)->voice.sub_gain : 0.0f;
}

float kcapi_get_fm_gain(const kcapi_synth_t *s)
{
    return s ? KS(s)->voice.fm_gain : 0.0f;
}

int kcapi_get_engine_mode(const kcapi_synth_t *s)
{
    return s ? (int)KS(s)->voice.engine_mode : 0;
}

float kcapi_get_peak_output(const kcapi_synth_t *s)
{
    (void)s;
    return 0.0f; // peak is measured in the audio callback, not here
}

void kcapi_render_offline(const kcapi_synth_t *s, float *out, int n_samples)
{
    if (!s || !out || n_samples <= 0) return;

    SynthSnapshot snap;
    synth_snapshot(KS(s), &snap);

    Synth offline;
    synth_init(&offline, &vco_default_bank, VOICE_SAMPLE_RATE);
    synth_restore(&offline, &snap);

    synth_render_to_buffer(&offline, out, n_samples, NULL, 0);
}

void kcapi_lfo_set_phase(kcapi_synth_t *s, int idx, float phase)
{
    if (s && idx >= 0 && idx < 3) lfo_set_phase(&KS(s)->voice.lfo[idx], phase);
}

void kcapi_lfo_fade_release(kcapi_synth_t *s, int idx)
{
    if (s && idx >= 0 && idx < 3) lfo_fade_release(&KS(s)->voice.lfo[idx]);
}

int kcapi_get_lfo_sync_mode(const kcapi_synth_t *s, int idx)
{
    if (s && idx >= 0 && idx < 3) return KS(s)->voice.lfo[idx].sync_mode;
    return 0;
}

/* ---------------- [TESTBENCH-P3 纯追加] ENV 图形读取 ---------------- */

int kcapi_get_env_bk(const kcapi_synth_t *s, int idx, int max_n,
                     float *xs, float *ys, int *seg_curves)
{
    if (!s || idx < 0 || idx >= 3) return 0;
    const EnvParams *p = &KS(s)->env_shape[idx].p;
    int n = p->bk_count;
    if (n > max_n) n = max_n;
    for (int i = 0; i < n; i++) {
        if (xs) xs[i] = p->bk_points[i].x;
        if (ys) ys[i] = p->bk_points[i].y;
        if (seg_curves && i < n - 1)
            seg_curves[i] = (int)p->bk_seg_curve[i];
    }
    return n;
}

int kcapi_get_env_bez(const kcapi_synth_t *s, int idx, int max_n,
                      float *xs, float *ys,
                      float *hlx, float *hly, float *hrx, float *hry,
                      int *auto_l, int *auto_r)
{
    if (!s || idx < 0 || idx >= 3) return 0;
    const EnvParams *p = &KS(s)->env_shape[idx].p;
    int n = p->bez_count;
    if (n > max_n) n = max_n;
    for (int i = 0; i < n; i++) {
        const EnvBezNode *nd = &p->bez_nodes[i];
        if (xs) xs[i] = nd->x;
        if (ys) ys[i] = nd->y;
        if (hlx) hlx[i] = nd->hlx;
        if (hly) hly[i] = nd->hly;
        if (hrx) hrx[i] = nd->hrx;
        if (hry) hry[i] = nd->hry;
        if (auto_l) auto_l[i] = nd->auto_l;
        if (auto_r) auto_r[i] = nd->auto_r;
    }
    return n;
}

float kcapi_get_env_output(const kcapi_synth_t *s, int idx)
{
    if (s && idx >= 0 && idx < 3) return KS(s)->voice.env_out[idx];
    return 0.0f;
}

float kcapi_get_lfo_output(const kcapi_synth_t *s, int idx)
{
    if (s && idx >= 0 && idx < 3) return KS(s)->voice.lfo_out[idx];
    return 0.0f;
}

int kcapi_get_env_runtime(const kcapi_synth_t *s, int idx,
                          float *lut_frac, float *lut_dur)
{
    if (!s || idx < 0 || idx >= 3) return 0; /* ENV_IDLE */
    const Voice *v = &KS(s)->voice;
    const Env *e = &v->env[idx];
    const EnvShape *sh = e->shape;
    if (lut_frac)
        *lut_frac = e->pos / (float)(LUT_SIZE - 1);
    if (lut_dur)
        *lut_dur = sh ? sh->meta[sh->active].duration : 0.0f;
    return e->state;
}

/* ---------------- [P0-MODSRC-1 纯追加] 调制源端口读回 ---------------- */

float kcapi_get_mod_src_value(const kcapi_synth_t *s, int idx)
{
    if (!s || idx < 0 || idx >= kcapi_num_ports()) return 0.0f;
    return vmatrix_read_src_value(&KS(s)->voice, &KS(s)->globals,
                                  (uint16_t)idx);
}
