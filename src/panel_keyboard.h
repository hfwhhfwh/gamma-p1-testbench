/*
 * panel_keyboard.h — 可点击钢琴键盘面板（ImDrawList 自绘，无第三方组件）
 */

#ifndef PANEL_KEYBOARD_H
#define PANEL_KEYBOARD_H

#include "synth_ui.h"

namespace gp1_ui {

/* 每帧绘制底部通栏键盘面板（Octave +/-、Hold、PC 键盘映射开关 + 琴键） */
void panel_keyboard(AppState *app);

} /* namespace gp1_ui */

#endif /* PANEL_KEYBOARD_H */
