/* TBENCH-FIX-3: T-D 无头桩 —— 仅 keyboard.cpp 用到的最小 GLFW 面
 * (键常量 / GLFW_PRESS / glfwGetKey)。键状态由测试经 g_kbd_stub_keys
 * 驱动; 键号为稳定小整数 (0..23), 与真实 GLFW 键码无对应关系。
 * 仅 tests/keyboard_stuck_test.cpp 构建使用, 不进测试台 app 构建。 */

#ifndef TBENCH_FIX3_KBD_STUB_GLFW3_H
#define TBENCH_FIX3_KBD_STUB_GLFW3_H

typedef struct GLFWwindow GLFWwindow;

#define GLFW_PRESS   1
#define GLFW_RELEASE 0

/* Z 排（中八度） */
#define GLFW_KEY_Z 0
#define GLFW_KEY_S 1
#define GLFW_KEY_X 2
#define GLFW_KEY_D 3
#define GLFW_KEY_C 4
#define GLFW_KEY_V 5
#define GLFW_KEY_G 6
#define GLFW_KEY_B 7
#define GLFW_KEY_H 8
#define GLFW_KEY_N 9
#define GLFW_KEY_J 10
#define GLFW_KEY_M 11
/* Q 排（高八度），数字排为黑键 */
#define GLFW_KEY_Q 12
#define GLFW_KEY_2 13
#define GLFW_KEY_W 14
#define GLFW_KEY_3 15
#define GLFW_KEY_E 16
#define GLFW_KEY_R 17
#define GLFW_KEY_5 18
#define GLFW_KEY_T 19
#define GLFW_KEY_6 20
#define GLFW_KEY_Y 21
#define GLFW_KEY_7 22
#define GLFW_KEY_U 23

#define KBD_STUB_KEY_COUNT 24

extern int g_kbd_stub_keys[KBD_STUB_KEY_COUNT];   /* 测试 TU 定义 */

static inline int glfwGetKey(GLFWwindow *window, int key)
{
    (void)window;
    return (key >= 0 && key < KBD_STUB_KEY_COUNT) ? g_kbd_stub_keys[key]
                                                  : GLFW_RELEASE;
}

#endif /* TBENCH_FIX3_KBD_STUB_GLFW3_H */
