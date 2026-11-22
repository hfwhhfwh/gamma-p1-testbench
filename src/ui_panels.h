/*
 * ui_panels.h — ImGui 面板总入口
 */

#ifndef UI_PANELS_H
#define UI_PANELS_H

#include "synth_ui.h"

struct GLFWwindow;

namespace gp1_ui {

/* 初始化 ImGui/GLFW/OpenGL3 */
bool ui_panels_init(GLFWwindow *window);
void ui_panels_shutdown();

/* 每帧绘制全部面板，返回 false 表示用户请求退出 */
bool ui_panels_frame(AppState *app);

} /* namespace gp1_ui */

#endif /* UI_PANELS_H */
