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

/* 延迟注入交给独立进程做: 宿主(SakuraChs.exe 那类汉化启动器)注入完就自己退出,
   在它里面起线程会跟着一起死。这里拉起同目录 FVPLauncher.exe 的 --inject-ready 模式,
   由它等到子进程加载器初始化完再注入。 */
static int spawn_ready_helper(DWORD pid, const WCHAR *dll) {
    WCHAR self[FVPL_MAX_PATH], dir[FVPL_MAX_PATH], helper[FVPL_MAX_PATH];
    WCHAR cmd[2 * FVPL_MAX_PATH + 64];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    int i;

    if (!self_dll(self, FVPL_MAX_PATH)) return 0;
    lstrcpynW(dir, self, FVPL_MAX_PATH);
    for (i = lstrlenW(dir) - 1; i >= 0; i--) {
        if (dir[i] == L'\\' || dir[i] == L'/') break;
    }
    if (i < 0) return 0;
    dir[i + 1] = 0;
    if (!pv_join(helper, FVPL_MAX_PATH, dir, L"FVPLauncher.exe")) return 0;
    wsprintfW(cmd, L"\"%s\" --inject-ready %u \"%s\"", helper, (unsigned)pid, dll);
    RtlZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    RtlZeroMemory(&pi, sizeof(pi));
    /* 宿主可能在 job 对象里: 破不出来就退回普通创建 */
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, NULL, dir, &si, &pi))
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi))
            return 0;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 1;
}

static void after_create(HANDLE proc, DWORD pid, HANDLE tid, int suspended_by_us,
                         const WCHAR *dll) {
    if (suspended_by_us) {
        /* 调用方没打算自己挂起 -> 子进程本来就会跑, 立即注入并放行 */
        if (FvplInjectDll(proc, pid, dll, 15000, NULL, NULL)) FvplLog("child injected");
        else FvplLog("child injection skipped");
        if (tid) ResumeThread(tid);
        return;
    }
    /* 调用方自己带着 CREATE_SUSPENDED 来的(汉化组: 挂起 -> 往子进程导入表里塞自己的
       DLL -> 再放行)。此刻注入会当场触发子进程的加载器初始化, 之后才塞进导入表的项
       永远轮不到被处理 —— 实测 filter.dll 及其依赖 ass.dll/MSVCP140 整条进不了游戏,
       汉化失效、游戏回落原版日文。改由独立进程等它跑起来(加载器初始化完)再注入。 */
    if (spawn_ready_helper(pid, dll)) FvplLog("child injection deferred to FVPLauncher.exe");
    else FvplLog("child injection deferred-failed");
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
    FvplLog((flags & CREATE_SUSPENDED) ? "  caller passed CREATE_SUSPENDED"
                                       : "  caller passed no CREATE_SUSPENDED");
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
