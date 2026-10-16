/* winutil.c — 见 winutil.h */
#include "winutil.h"
#include "utf8w.h"
#include "pathutil.h"
#include "config.h"
#include <tlhelp32.h>

int FvplLoadIniText(const WCHAR *dir, fvpl_wchar *out, int outcch) {
    WCHAR path[FVPL_MAX_PATH];
    static unsigned char raw[64 * 1024];
    HANDLE h;
    DWORD size, got = 0;
    int skip = 0;
    if (!pv_join(path, FVPL_MAX_PATH, dir, FVPL_INI_NAME)) return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(h, NULL);
    if (size == 0 || size == 0xFFFFFFFF || size > sizeof(raw) - 4) { CloseHandle(h); return 0; }
    if (!ReadFile(h, raw, size, &got, NULL) || got != size) { CloseHandle(h); return 0; }
    CloseHandle(h);
    if (got >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) skip = 3;
    return uw_decode(raw + skip, (int)(got - (unsigned)skip), out, outcch) >= 0;
}

/* LoadLibraryW 的返回值只有低 32 位能通过 GetExitCodeThread 传回来,
   x64 上镜像基址可能整块落在高 32 位而读出 0 —— 所以 0 不等于失败, 再用模块快照确认。 */
static int child_has_dll(DWORD pid, const WCHAR *dllpath) {
    MODULEENTRY32W me;
    HANDLE snap;
    WCHAR name[64];
    int i, n, found = 0;
    n = lstrlenW(dllpath);
    i = n;
    while (i > 0 && dllpath[i - 1] != L'\\' && dllpath[i - 1] != L'/') i--;
    if (n - i <= 0 || n - i >= 64) return 0;
    lstrcpynW(name, dllpath + i, n - i + 1);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, name) == 0) { found = 1; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

/* 进程当前已加载的模块数。加载器处理导入表之前只有 exe 与 ntdll 两项, 之后会跳到几十个 ——
   这就是"子进程已经跑起来"的可观测信号, 延迟注入用它决定什么时候动手。 */
int FvplProcessModuleCount(DWORD pid) {
    MODULEENTRY32W me;
    HANDLE snap;
    int n = 0;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return -1;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do { n++; } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return n;
}

int FvplInjectDll(HANDLE hProcess, DWORD pid, const WCHAR *dllpath, DWORD timeout_ms,
                  ULONG_PTR *module_base, const WCHAR **fail_step) {
    SIZE_T cb;
    LPVOID remote;
    SIZE_T wrote = 0;
    HMODULE k32;
    FARPROC pLoad;
    HANDLE ht;
    DWORD wait;
    ULONG_PTR ret = 0;
    if (fail_step) *fail_step = L"未知原因";
    cb = ((SIZE_T)lstrlenW(dllpath) + 1) * sizeof(WCHAR);
    if (fail_step) *fail_step = L"无法在游戏进程里申请内存（常被杀毒软件拦截）";
    remote = VirtualAllocEx(hProcess, NULL, cb, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == NULL) return 0;
    if (fail_step) *fail_step = L"无法把启动文件写进游戏进程（常被杀毒软件拦截）";
    if (!WriteProcessMemory(hProcess, remote, dllpath, cb, &wrote) || wrote != cb) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return 0;
    }
    k32 = GetModuleHandleW(L"kernel32.dll");
    pLoad = k32 ? GetProcAddress(k32, "LoadLibraryW") : NULL;
    if (pLoad == NULL) {
        if (fail_step) *fail_step = L"系统缺少必要的组件";
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return 0;
    }
    if (fail_step) *fail_step = L"无法让游戏加载启动文件（常被杀毒软件拦截）";
    /* LoadLibraryW 的签名和线程入口本就不一致, 这是远线程注入的固定写法 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    ht = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoad, remote, 0, NULL);
#pragma GCC diagnostic pop
    if (ht == NULL) {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return 0;
    }
    wait = WaitForSingleObject(ht, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        if (fail_step) *fail_step = L"等待游戏响应超时（常被杀毒软件拦截）";
        TerminateThread(ht, 1);
        CloseHandle(ht);
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        return 0;
    }
    {
        DWORD lo = 0;
        GetExitCodeThread(ht, &lo);
        ret = (ULONG_PTR)lo;
    }
    CloseHandle(ht);
    VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
    if (ret == 0 && !child_has_dll(pid, dllpath)) {
        if (fail_step) *fail_step = L"游戏加载启动文件失败（文件缺失、版本不符或被拦截）";
        return 0;
    }
    if (module_base) *module_base = ret;
    if (fail_step) *fail_step = NULL;
    return 1;
}

fvpl_uint FvplHostAcp(void) {
    typedef UINT (WINAPI *PFN_GETACP)(void);
    static PFN_GETACP pfn;
    HMODULE k;
    if (pfn) return (fvpl_uint)pfn();
    k = GetModuleHandleA("kernel32.dll");
    if (k) pfn = (PFN_GETACP)(void *)GetProcAddress(k, "GetACP");
    if (pfn) return (fvpl_uint)pfn();
    return 1252;   /* 理论上到不了: kernel32 一定导出 GetACP */
}
