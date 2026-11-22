/*
 * keyboard.cpp — 电脑键盘映射到 MIDI note（tracker 布局）
 *
 *   Z 排：Z S X D C V G B H N J M  =  C C# D D# E F F# G G# A A# B
 *   Q 排：Q 2 W 3 E R 5 T 6 Y 7 U  =  高八度同上（数字排 = 黑键）
 *
 * 基准音 = Keyboard 面板 kb_base_note + 12（面板显示三组的中间八度），
 * 随面板 Octave +/- 联动。按下沿触发 note_on（忽略系统 key repeat），
 * 松开 note_off（走 UI 音符栈，单键释放不影响其他按住的键）。
 * 总开关 app->kb_pc_map_enabled（面板 "PC Keys" 复选框）；
 * io.WantTextInput 为 true（输入框聚焦）时整表失效。
 *
 * 变更摘要 (2026-10-07 TBENCH-FIX-3):
 *   P-14: 按下时把算好的音符号随按下标记一起登记 (新增平行数组
 *   s_note[kBindCount]), 所有松键路径一律用【登记值】发
 *   ui_note_off_note —— 按住期间 Octave± 改变 kb_base_note 后松键
 *   不再按当前 base 重算 (原行为发错 note_off, 原音符永远卡响);
 *   P-15: note>127 分支语义改为"有登记在按先按登记值发 off 再清
 *   标记" (原静默清标记不发 off, 音符泄漏); 按下瞬间 note>127 的键
 *   不允许登记 (按不下 = 无 off 责任)。
 */

#include "keyboard.h"
#include "GLFW/glfw3.h"
#include "imgui.h"

namespace gp1_ui {

struct KeyBind { int glfw_key; int semitone; };

static const KeyBind kBinds[] = {
    /* Z 排（中八度） */
    {GLFW_KEY_Z, 0}, {GLFW_KEY_S, 1}, {GLFW_KEY_X, 2}, {GLFW_KEY_D, 3},
    {GLFW_KEY_C, 4}, {GLFW_KEY_V, 5}, {GLFW_KEY_G, 6}, {GLFW_KEY_B, 7},
    {GLFW_KEY_H, 8}, {GLFW_KEY_N, 9}, {GLFW_KEY_J, 10}, {GLFW_KEY_M, 11},
    /* Q 排（高八度），数字排为黑键 */
    {GLFW_KEY_Q, 12}, {GLFW_KEY_2, 13}, {GLFW_KEY_W, 14}, {GLFW_KEY_3, 15},
    {GLFW_KEY_E, 16}, {GLFW_KEY_R, 17}, {GLFW_KEY_5, 18}, {GLFW_KEY_T, 19},
    {GLFW_KEY_6, 20}, {GLFW_KEY_Y, 21}, {GLFW_KEY_7, 22}, {GLFW_KEY_U, 23},
};
static const int kBindCount = sizeof(kBinds) / sizeof(kBinds[0]);

static bool s_down[kBindCount]; /* 按绑定槽位做按下沿检测，天然免疫 key repeat */
// TBENCH-FIX-3: P-14/P-15 按下瞬间登记的音符号 (与 s_down 同槽位);
// 所有松键路径用登记值发 off, 禁止用当前 kb_base_note 重算
static int s_note[kBindCount];

void keyboard_init(GLFWwindow *window, AppState *app)
{
    (void)window;
    (void)app;
    for (int i = 0; i < kBindCount; i++) {
        s_down[i] = false;
        s_note[i] = -1;   // TBENCH-FIX-3: P-14 -1 = 无登记
    }
}

void keyboard_poll(GLFWwindow *window, AppState *app)
{
    if (!window || !app)
        return;

    /* 总开关关闭或输入框聚焦时：释放所有残留按键后整表失效 */
    if (!app->kb_pc_map_enabled || ImGui::GetIO().WantTextInput) {
        for (int i = 0; i < kBindCount; i++) {
            if (s_down[i]) {
                // TBENCH-FIX-3: P-14 用登记值发 off (登记值恒 <=127,
                // 按下瞬间 >127 不允许登记, 见主循环)
                ui_note_off_note(app, s_note[i]);
                s_down[i] = false;
                s_note[i] = -1;
            }
        }
        return;
    }

    const int base = app->kb_base_note + 12;
    for (int i = 0; i < kBindCount; i++) {
        int note = base + kBinds[i].semitone;
        if (note > 127) {
            // TBENCH-FIX-3: P-15 该键有登记在按时, 先按登记值发 off
            // 再清标记 (原静默清标记不发 off, 音符泄漏)
            if (s_down[i]) {
                ui_note_off_note(app, s_note[i]);
                s_down[i] = false;
                s_note[i] = -1;
            }
            continue;
        }
        bool pressed = (glfwGetKey(window, kBinds[i].glfw_key) == GLFW_PRESS);
        if (pressed && !s_down[i]) {
            ui_note_on(app, note, 100);
            s_down[i] = true;
            s_note[i] = note;   // TBENCH-FIX-3: P-14 按下即登记音符号
        } else if (!pressed && s_down[i]) {
            // TBENCH-FIX-3: P-14 松键用登记值发 off (不按当前 base 重算)
            ui_note_off_note(app, s_note[i]);
            s_down[i] = false;
            s_note[i] = -1;
        }
    }
}

} /* namespace gp1_ui */
