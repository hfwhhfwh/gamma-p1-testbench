// TBENCH-FIX-3: T-D 无头桩 —— 仅 keyboard.cpp 用到的最小 ImGui 面
// (ImGui::GetIO().WantTextInput)。GetIO 定义在测试 TU (单实例)。
// 仅 tests/keyboard_stuck_test.cpp 构建使用, 不进测试台 app 构建。

#ifndef TBENCH_FIX3_KBD_STUB_IMGUI_H
#define TBENCH_FIX3_KBD_STUB_IMGUI_H

namespace ImGui {

struct ImGuiIO { bool WantTextInput; };

ImGuiIO &GetIO();

} // namespace ImGui

#endif // TBENCH_FIX3_KBD_STUB_IMGUI_H
