#include "proclaunch.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
/* ===================== WINDOWS ===================== */
#include <windows.h>

static char *append_arg(char *dst, const char *arg)
{
    const char *p = arg;
    int need_quotes = (*arg == '\0');
    for (const char *q = arg; *q; ++q) {
        if (*q == ' ' || *q == '\t' || *q == '"') { need_quotes = 1; break; }
    }
    if (need_quotes) *dst++ = '"';
    while (*p) {
        if (*p == '\\') {
            int bs = 0;
            while (*p == '\\') { ++bs; ++p; }
            if (*p == '"') {
                for (int i = 0; i < bs * 2; ++i) *dst++ = '\\';
                if (*p) { *dst++ = '\\'; *dst++ = '"'; ++p; }
            } else {
                if (*p == '\0' && need_quotes) {
                    for (int i = 0; i < bs * 2; ++i) *dst++ = '\\';
                } else {
                    for (int i = 0; i < bs; ++i) *dst++ = '\\';
                }
            }
        } else if (*p == '"') {
            *dst++ = '\\'; *dst++ = '"'; ++p;
        } else {
            *dst++ = *p++;
        }
    }
    if (need_quotes) *dst++ = '"';
    *dst = '\0';
    return dst;
}

static char *build_cmdline(const char *const argv[])
{
    size_t cap = 0;
    for (int i = 0; argv[i]; ++i) cap += strlen(argv[i]) * 2 + 4;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    char *w = buf;
    for (int i = 0; argv[i]; ++i) {
        if (i) *w++ = ' ';
        w = append_arg(w, argv[i]);
    }
    return buf;
}

int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc) return -1;

    char *cmd = build_cmdline(argv);
    if (!cmd) return -1;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                             0, NULL, NULL, &si, &pi);
    free(cmd);
    if (!ok) return -1;

    CloseHandle(pi.hThread);
    proc->handle = (intptr_t)pi.hProcess;
    proc->pid    = (long)pi.dwProcessId;
    proc->active = 1;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->active) return -1;
    HANDLE h = (HANDLE)proc->handle;

    DWORD w = WaitForSingleObject(h, INFINITE);
    if (w != WAIT_OBJECT_0) return -1;

    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) return -1;

    CloseHandle(h);
    proc->active = 0;
    if (exit_code) *exit_code = (int)code;
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->active) return -1;
    HANDLE h = (HANDLE)proc->handle;

    DWORD w = WaitForSingleObject(h, 0);
    if (w == WAIT_TIMEOUT) return 0;
    if (w != WAIT_OBJECT_0) return -1;

    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) return -1;

    CloseHandle(h);
    proc->active = 0;
    if (exit_code) *exit_code = (int)code;
    return 1;
}

#else
/* ===================== POSIX  ===================== */
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int pl_start(const char *const argv[], pl_process *proc)
{
    if (!argv || !argv[0] || !proc) return -1;

    int errpipe[2];
    if (pipe(errpipe) != 0) return -1;

    if (fcntl(errpipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(errpipe[0]); close(errpipe[1]); return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(errpipe[0]); close(errpipe[1]);
        return -1;
    }
    if (pid == 0) {
        close(errpipe[0]);
        execvp(argv[0], (char *const *)argv);
        int e = errno;
        (void)!write(errpipe[1], &e, sizeof e);
        _exit(127);
    }

    close(errpipe[1]);
    int child_errno = 0;
    ssize_t n;
    do {
        n = read(errpipe[0], &child_errno, sizeof child_errno);
    } while (n < 0 && errno == EINTR);
    close(errpipe[0]);

    if (n == (ssize_t)sizeof child_errno) {
        int st;
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
        return -1;
    }

    proc->pid    = (long)pid;
    proc->handle = 0;
    proc->active = 1;
    return 0;
}

int pl_wait(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->active) return -1;
    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;

    proc->active = 0;
    if (exit_code) {
        if (WIFEXITED(status))        *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
        else                          *exit_code = -1;
    }
    return 0;
}

int pl_poll(pl_process *proc, int *exit_code)
{
    if (!proc || !proc->active) return -1;
    int status = 0;
    pid_t r;
    do {
        r = waitpid((pid_t)proc->pid, &status, WNOHANG);
    } while (r < 0 && errno == EINTR);

    if (r == 0) return 0;
    if (r < 0)  return -1;

    proc->active = 0;
    if (exit_code) {
        if (WIFEXITED(status))        *exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) *exit_code = 128 + WTERMSIG(status);
        else                          *exit_code = -1;
    }
    return 1;
}

#endif