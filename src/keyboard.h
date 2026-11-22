/*
 * keyboard.h — GLFW 电脑键盘 fallback：tracker 布局（Z 排 / Q 排 + 数字排黑键）
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "synth_ui.h"

struct GLFWwindow;

namespace gp1_ui {

void keyboard_init(GLFWwindow *window, AppState *app);
void keyboard_poll(GLFWwindow *window, AppState *app); /* 每帧调用 */

} /* namespace gp1_ui */

#endif /* KEYBOARD_H */
