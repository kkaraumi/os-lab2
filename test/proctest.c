#include "proclaunch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
#endif

static int failures = 0;

static void report(const char *name, int ok)
{
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok) ++failures;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc >= 4 && strcmp(argv[1], "--child") == 0) {
        int code = atoi(argv[2]);
        int ms   = atoi(argv[3]);
        sleep_ms(ms);
        
        if (argc >= 5) {
            const char *expected = "hello \"big\" world\\";
            return strcmp(argv[4], expected) == 0 ? 0 : 1;
        }
        return code;
    }

    const char *self = argv[0];
    char codebuf[16], msbuf[16];

    {
        snprintf(codebuf, sizeof codebuf, "%d", 42);
        snprintf(msbuf,   sizeof msbuf,   "%d", 1000);
        const char *a[] = { self, "--child", codebuf, msbuf, NULL };

        pl_process p;
        int ok = (pl_start(a, &p) == 0);
        int iters = 0, code = -1, done = 0;
        while (ok && iters < 50) {
            int r = pl_poll(&p, &code);
            if (r == 1) { done = 1; break; }
            if (r < 0)  { ok = 0; break; }
            ++iters;
            sleep_ms(100);
        }
        report("1: фоновый запуск (родитель работал)", ok && done && iters > 3 && code == 42);
    }

    {
        snprintf(codebuf, sizeof codebuf, "%d", 7);
        snprintf(msbuf,   sizeof msbuf,   "%d", 300);
        const char *a[] = { self, "--child", codebuf, msbuf, NULL };

        pl_process p;
        int code = -1;
        int ok = (pl_start(a, &p) == 0) && (pl_wait(&p, &code) == 0);
        report("2: pl_wait вернул код 7", ok && code == 7);
    }

    {
        const char *expected = "hello \"big\" world\\";
        const char *a[] = { self, "--child", "0", "0", expected, NULL };

        pl_process p;
        int code = -1;
        int ok = (pl_start(a, &p) == 0) && (pl_wait(&p, &code) == 0) && code == 0;
        report("3: аргумент с пробелами/кавычками", ok);
    }

    {
        const char *a[] = { "no_such_program_12345", NULL };
        pl_process p;
        int r = pl_start(a, &p);
        report("4: запуск несуществующей программы -> -1", r == -1);
    }

    {
        const char *a[] = { self, "--child", "300", "0", NULL };
        pl_process p;
        int code = -1;
        int ok = (pl_start(a, &p) == 0) && (pl_wait(&p, &code) == 0);
#ifdef _WIN32
        report("5: код 300 (Windows)", ok && code == 300);
#else
        report("5: код 300 -> 44 (Linux, 8 бит)", ok && code == 44);
#endif
    }

    printf("\n%s\n", failures == 0 ? "Все тесты пройдены." : "Есть провалившиеся тесты.");
    return failures == 0 ? 0 : 1;
}