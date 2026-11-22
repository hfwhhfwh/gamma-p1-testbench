/*
 * platform_posix.c — 平台层 POSIX 实现 (TBENCH-WIN-1)
 *
 * 直接转调移植前 action_log.c / logpath1_test.c 等使用的原 POSIX
 * API (clock_gettime/realpath/localtime_r/mkdtemp/getcwd/chdir/unlink
 * + freopen("/dev/tty")), Mac/Linux 行为与移植前逐位一致。
 *
 * 特性宏: _DARWIN_C_SOURCE (macOS mkdtemp 属 BSD 扩展) 与
 * _POSIX_C_SOURCE 200809L (localtime_r/realpath) — 与原
 * logpath1_test.c 顶部同款, 不改任何语义。
 */

#if !defined(_WIN32)

#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

long plat_monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int plat_absolute_path(const char *path, char *out, size_t cap)
{
    (void)cap;   /* POSIX realpath 要求缓冲 >= PATH_MAX, 与移植前同 */
    return realpath(path, out) != NULL;
}

void plat_localtime(const time_t *wall, struct tm *out)
{
    localtime_r(wall, out);
}

int plat_reopen_stderr_tty(void)
{
    fflush(stderr);
    return freopen("/dev/tty", "w", stderr) != NULL;
}

size_t plat_temp_dir(char *out, size_t cap)
{
    const char *td = getenv("TMPDIR");
    size_t n;
    if (!td || !*td)
        td = "/tmp";
    n = strlen(td);
    if (n == 0 || n >= cap)
        return 0;
    memcpy(out, td, n + 1);
    return n;
}

int plat_mkdtemp(const char *prefix, char *out, size_t cap)
{
    char templ[PLAT_PATH_MAX];
    size_t n;
    if (!prefix)
        prefix = "gp1";
    n = plat_temp_dir(templ, sizeof(templ));
    if (n == 0 || n + strlen(prefix) + 8 >= sizeof(templ))
        return 0;
    snprintf(templ + n, sizeof(templ) - n, "%sXXXXXX", prefix);
    if (!mkdtemp(templ))
        return 0;
    n = strlen(templ);
    if (n >= cap)
        return 0;
    memcpy(out, templ, n + 1);
    return 1;
}

int plat_getcwd(char *buf, size_t cap)
{
    return getcwd(buf, cap) != NULL;
}

int plat_chdir(const char *path)
{
    return chdir(path) == 0;
}

int plat_unlink(const char *path)
{
    return unlink(path) == 0;
}

#endif /* !_WIN32 */
