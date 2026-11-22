/*
 * platform_win32.c — 平台层 Win32 实现 (TBENCH-WIN-1)
 *
 * 与 platform_posix.c 对偶, 语义逐项对齐:
 *   monotonic 毫秒   QueryPerformanceCounter (静态初始化频率, 线程安全
 *                    由静态局部初始化保证; 失败回退 GetTickCount64)
 *   绝对路径         GetFullPathNameA (相对路径按当前进程 CWD 解析,
 *                    与 realpath 对已有文件的解析在日志落点用途等价;
 *                    对不存在路径 realpath 会失败而本实现成功 ——
 *                    alog_start 只解析刚成功 fopen 的文件, 口径一致)
 *   localtime        localtime_s (UCRT)
 *   stderr 回接      freopen("CONOUT$")
 *   临时目录         GetTempPathA
 *   唯一目录         GetTempPathA + GetCurrentProcessId + GetTickCount64
 *                    + 静态计数 + _mkdir (重试 100 次防碰撞)
 *   getcwd/chdir     _getcwd/_chdir;  unlink _unlink
 */

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "platform.h"

#include <direct.h>   /* _getcwd / _chdir / _mkdir */
#include <stdio.h>    /* _unlink / freopen / fflush */
#include <string.h>

long plat_monotonic_ms(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&freq))
            freq.QuadPart = 0;
    }
    if (freq.QuadPart != 0 && QueryPerformanceCounter(&now))
        return (long)(now.QuadPart * 1000LL / freq.QuadPart);
    return (long)(GetTickCount64() & 0x7fffffffLL);  /* 回退: 精度 15ms */
}

int plat_absolute_path(const char *path, char *out, size_t cap)
{
    DWORD n = GetFullPathNameA(path, (DWORD)cap, out, NULL);
    return n != 0 && n < (DWORD)cap;
}

void plat_localtime(const time_t *wall, struct tm *out)
{
    localtime_s(out, wall);
}

int plat_reopen_stderr_tty(void)
{
    fflush(stderr);
    return freopen("CONOUT$", "w", stderr) != NULL;
}

size_t plat_temp_dir(char *out, size_t cap)
{
    DWORD n = GetTempPathA((DWORD)cap, out);
    /* GetTempPath 返回含末尾反斜杠的路径; 0 或截断均判失败 */
    return (n != 0 && n < (DWORD)cap) ? (size_t)n : 0;
}

int plat_mkdtemp(const char *prefix, char *out, size_t cap)
{
    char base[PLAT_PATH_MAX];
    size_t n, i;
    static unsigned long seq = 0;
    if (!prefix)
        prefix = "gp1";
    n = plat_temp_dir(base, sizeof(base));
    if (n == 0 || n + strlen(prefix) + 48 >= sizeof(base))
        return 0;
    for (i = 0; i < 100; i++) {
        snprintf(base + n, sizeof(base) - n, "%s%lu_%llu_%lu", prefix,
                 (unsigned long)GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64(), seq++);
        if (_mkdir(base) == 0) {
            size_t len = strlen(base);
            if (len >= cap)
                return 0;
            memcpy(out, base, len + 1);
            return 1;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return 0;
    }
    return 0;
}

int plat_getcwd(char *buf, size_t cap)
{
    return _getcwd(buf, (int)cap) != NULL;
}

int plat_chdir(const char *path)
{
    return _chdir(path) == 0;
}

int plat_unlink(const char *path)
{
    return _unlink(path) == 0;
}

#endif /* _WIN32 */
