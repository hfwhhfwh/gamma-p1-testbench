/*
 * seq.h — 16 步音序器 (测试台侧小程序)
 *
 * 设计取向: 越简单越好。状态全部为本 TU 静态量, 无堆分配;
 * 走带时钟用 UI 帧传入的秒时间戳 (glfwGetTime), 每步
 * 16 分音符 = 60/BPM/4 秒; 发声复用 ui_note_on /
 * ui_note_off_note —— 音符栈回退与操作日志 (TBENCH-LOG-WIRE)
 * 自动兼容, 内核零改动。
 *
 * seq_tick: 每帧调用 (窗口关闭也走带); seq_panel: Seq 窗口内容。
 */

#ifndef SEQ_H
#define SEQ_H

namespace gp1_ui {

struct AppState;

/* now = 当前秒时间戳 (与面板内 ImGui::GetTime()/glfwGetTime 同源) */
void seq_tick(AppState *app, double now);
void seq_panel(AppState *app);

} /* namespace gp1_ui */

#endif /* SEQ_H */
