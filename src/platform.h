/*
 * platform.h — 测试台平台抽象层 (TBENCH-WIN-1)
 *
 * 封装 WP0c 盘点出的全部 POSIX 依赖点, 让宿主代码与测试件不直接
 * 出现 unistd.h / clock_gettime / realpath / localtime_r / /dev/tty:
 *   - monotonic 毫秒      (POSIX: clock_gettime(CLOCK_MONOTONIC);
 *                          Windows: QueryPerformanceCounter)
 *   - 绝对路径解析        (POSIX: realpath; Windows: GetFullPathNameA)
 *   - localtime 分解      (POSIX: localtime_r; Windows: localtime_s)
 *   - stderr 回接控制台   (POSIX: freopen("/dev/tty"); Windows: "CONOUT$")
 *   - 临时目录/唯一子目录/工作目录切换/删文件 (测试件用)
 *
 * 纪律: 纯 C11 接口 (extern "C"), 两侧实现都是 .c 文件, 内核与
 * 宿主业务逻辑零 #ifdef; Mac 侧实现直接转调原 POSIX API, 行为与
 * 移植前逐位一致 (默认即隐身)。路径缓冲统一用 PLAT_PATH_MAX。
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <time.h>

#ifdef _WIN32
#define PLAT_PATH_MAX 260   /* Windows _MAX_PATH (Windows.h 同值) */
#else
#include <limits.h>
#define PLAT_PATH_MAX PATH_MAX
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* monotonic 毫秒 (会话计时用; 不回绕, 精度 >= 1ms) */
long plat_monotonic_ms(void);

/* 解析绝对路径: 成功返回 1 并写 out (调用方保证 cap>=PLAT_PATH_MAX),
 * 失败返回 0 (out 内容未定义, 调用方回退原路径) */
int  plat_absolute_path(const char *path, char *out, size_t cap);

/* 本地时区分解 (同 localtime_r/localtime_s 语义) */
void plat_localtime(const time_t *wall, struct tm *out);

/* 把 stderr 重定向回控制台 (恢复 freopen 捕获前状态):
 * 成功 1, 失败 0 (保持重定向不动) */
int  plat_reopen_stderr_tty(void);

/* 系统临时目录写入 out (含末尾分隔符), 返回路径长度, 失败 0 */
size_t plat_temp_dir(char *out, size_t cap);

/* 在系统临时目录下建唯一目录 (名 = prefix + 进程/计数/时标熵),
 * 完整路径写 out; 成功 1, 失败 0 */
int  plat_mkdtemp(const char *prefix, char *out, size_t cap);

/* 工作目录与文件助手 (语义同 getcwd/chdir/unlink; 1/0 成败) */
int  plat_getcwd(char *buf, size_t cap);
int  plat_chdir(const char *path);
int  plat_unlink(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_H */
