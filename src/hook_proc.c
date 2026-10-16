/* hook_proc.c — 把 hook 带进子进程 (汉化版常见 SakuraChs.exe -> Sakura.exe 链式启动)
 *
 * 汉化组的 filter.dll 静态链接在 SakuraChs.exe 里, 它调的是 CreateProcessA,
 * 所以 A/W 两版都要拦。注入失败不拦停子进程: 子进程裸跑至少还能玩, 只是没换编码。 */
#include "hooks.h"
#include "winutil.h"
#include "pathutil.h"

typedef BOOL (WINAPI *PFN_CreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                          LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                                          LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *PFN_CreateProcessA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES,
                                          LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                                          LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
static PFN_CreateProcessW o_CreateProcessW;
static PFN_CreateProcessA o_CreateProcessA;

static int self_dll(WCHAR *out, int cch) {
    HMODULE m = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&self_dll, &m)) return 0;
    if (m == NULL || GetModuleFileNameW(m, out, (DWORD)cch) == 0) return 0;
    return 1;
}

/* 1 = 配置要求注入且拿得到自身 DLL 路径; 0 = 原样放行 */
static int want_inject(WCHAR *dll, int cch) {
    if (!FvplReady() || !g_cfg.inject_children) return 0;
    return self_dll(dll, cch);
}

static void after_create(HANDLE proc, DWORD pid, HANDLE tid, int suspended_by_us,
                         const WCHAR *dll) {
    if (FvplInjectDll(proc, pid, dll, 15000, NULL, NULL)) FvplLog("child injected");
    else FvplLog("child injection skipped");
    if (suspended_by_us && tid) ResumeThread(tid);
}

static BOOL WINAPI Hook_CreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa,
                                       LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
                                       LPVOID env, LPCWSTR dir, LPSTARTUPINFOW si,
                                       LPPROCESS_INFORMATION pi) {
    WCHAR dll[FVPL_MAX_PATH];
    if (o_CreateProcessW == NULL) return FALSE;
    if (pi == NULL || si == NULL || !want_inject(dll, FVPL_MAX_PATH))
        return o_CreateProcessW(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    {
        STARTUPINFOW si2 = *si;
        PROCESS_INFORMATION pi2;
        int added = 0;
        si2.cb = sizeof(si2);
        if ((flags & CREATE_SUSPENDED) == 0) { flags |= CREATE_SUSPENDED; added = 1; }
        if (!o_CreateProcessW(app, cmd, pa, ta, inherit, flags, env, dir, &si2, &pi2)) return FALSE;
        *pi = pi2;
        after_create(pi2.hProcess, pi2.dwProcessId, pi2.hThread, added, dll);
    }
    return TRUE;
}

static BOOL WINAPI Hook_CreateProcessA(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa,
                                       LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags,
                                       LPVOID env, LPCSTR dir, LPSTARTUPINFOA si,
                                       LPPROCESS_INFORMATION pi) {
    WCHAR dll[FVPL_MAX_PATH];
    if (o_CreateProcessA == NULL) return FALSE;
    FvplLog("CreateProcessA intercepted");
    if (pi == NULL || si == NULL || !want_inject(dll, FVPL_MAX_PATH))
        return o_CreateProcessA(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    {
        STARTUPINFOA si2 = *si;
        PROCESS_INFORMATION pi2;
        int added = 0;
        si2.cb = sizeof(si2);
        if ((flags & CREATE_SUSPENDED) == 0) { flags |= CREATE_SUSPENDED; added = 1; }
        if (!o_CreateProcessA(app, cmd, pa, ta, inherit, flags, env, dir, &si2, &pi2)) return FALSE;
        *pi = pi2;
        after_create(pi2.hProcess, pi2.dwProcessId, pi2.hThread, added, dll);
    }
    return TRUE;
}

static const FvplPatch s_tbl[] = {
    { "kernel32.dll", "CreateProcessW", (void *)Hook_CreateProcessW, (void **)&o_CreateProcessW },
    { "kernel32.dll", "CreateProcessA", (void *)Hook_CreateProcessA, (void **)&o_CreateProcessA },
};

const FvplPatch *FvplTableProc(int *count) {
    *count = (int)(sizeof(s_tbl) / sizeof(s_tbl[0]));
    return s_tbl;
}
