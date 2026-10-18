/* dllmain.c — fvpl_hook.dll 入口: 只换导入表, 不碰代码段 */
#include "hooks.h"
#include "iat.h"
#include <tlhelp32.h>
#include <winternl.h>
#include <stdio.h>

static void install(HMODULE host) {
    const FvplPatch *t;
    int n, a, b, c;
    t = FvplTableIo(&n);
    a = FvplIatApply(host, t, n);
    t = FvplTableNls(&n);
    b = FvplIatApply(host, t, n);
    t = FvplTableProc(&n);
    c = FvplIatApply(host, t, n);
    {
        char msg[96];
        _snprintf(msg, sizeof(msg), "host io=%d nls=%d proc=%d", a, b, c);
        msg[sizeof(msg) - 1] = 0;
        FvplLog(msg);
    }
    if (a + b + c <= 0) FvplLog("NO hook matched this host");
}

/* B 档用法 (FVPBind 把钩子写进引擎自己的导入表) 要求加载器能解析到一个真实的导出符号,
 * 否则进程直接起不来 ("无法定位程序输入点")。引擎永远不会调用它。
 * 保持 cdecl: stdcall 会导出成 _FvplAttach@0, 导入表里的名字就对不上了。 */
int FvplAttach(void) { return 1; }

/* 直接读 PEB 的模块链表:
 * DllMain 里不能 CreateToolhelp32Snapshot (它会再去拿 loader lock, 我们正持有),
 * 而汉化版的 filter.dll 是主程序的静态导入, 此刻已经在链表里了。
 * 偏移是加载器固定的结构布局 (x86: LIST_ENTRY 8 字节, x64: 16 字节)。 */
#if defined(__x86_64__) || defined(_M_X64)
#define LDR_MEM_LIST   0x20   /* PEB_LDR_DATA.InMemoryOrderModuleList */
#define ENTRY_DLL_BASE 0x20   /* 相对 InMemoryOrderLinks 的 DllBase 偏移 */
#else
#define LDR_MEM_LIST   0x14
#define ENTRY_DLL_BASE 0x10
#endif

typedef struct FvplListEntry { struct FvplListEntry *Flink, *Blink; } FvplListEntry;

static void patch_loaded_modules(const FvplPatch *t, int n, int *seen, int *hit) {
    char *peb = (char *)NtCurrentTeb()->ProcessEnvironmentBlock;
    char *ldr = *(char **)(peb + 0x0C);
    FvplListEntry *head, *e;
    *seen = 0;
    *hit = 0;
    if (!peb || !ldr) return;
    head = (FvplListEntry *)(ldr + LDR_MEM_LIST);
    for (e = head->Flink; e != NULL && e != head; e = e->Flink) {
        HMODULE m = *(HMODULE *)((char *)e + ENTRY_DLL_BASE);
        int k;
        (*seen)++;
        if (m == NULL) continue;
        k = FvplIatApply(m, t, n);
        if (k > 0) *hit += k;
        {
            char nm[MAX_PATH], msg[MAX_PATH + 32];
            DWORD got = GetModuleFileNameA(m, nm, sizeof(nm));
            int i = 0;
            if (got > 0 && got < sizeof(nm)) {
                i = (int)got;
                while (i > 0 && nm[i - 1] != '\\' && nm[i - 1] != '/') i--;
            } else {
                nm[0] = 0;
            }
            _snprintf(msg, sizeof(msg), "sweep %s -> %d", nm + i, k);
            msg[sizeof(msg) - 1] = 0;
            FvplLog(msg);
        }
    }
}

/* 后加载的模块 (游戏自己 LoadLibrary 的补丁 DLL) 由这个线程补打。
 * 它同时承担配置初始化: do_init 会碰文件系统, 绝不能在 loader lock 里跑。 */
static DWORD WINAPI watch_children(LPVOID arg) {
    const FvplPatch *t;
    int n, pass;
    (void)arg;
    if (!FvplReady()) return 0;
    /* [launcher] icon: 窗口建好得比注入晚, 这里 250ms 步进最多试 ~20 秒
     * (函数内部自己重试)。与 inject_children 无关, 所以放在早退之前。 */
    FvplApplyWindowIcon();
    if (!g_cfg.inject_children) return 0;
    t = FvplTableProc(&n);
    for (pass = 0; pass < 40; pass++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        MODULEENTRY32W me;
        if (snap == INVALID_HANDLE_VALUE) return 0;
        me.dwSize = sizeof(me);
        if (Module32FirstW(snap, &me)) {
            do {
                if (me.modBaseAddr != NULL) FvplIatApply((HMODULE)me.modBaseAddr, t, n);
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        Sleep(250);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE host = GetModuleHandleW(NULL);
        const FvplPatch *t;
        int n, seen = 0, hit = 0;
        HANDLE h;
        DisableThreadLibraryCalls(inst);
        if (host == NULL) return TRUE;
        install(host);
        t = FvplTableProc(&n);
        patch_loaded_modules(t, n, &seen, &hit);
        {
            char msg[96];
            _snprintf(msg, sizeof(msg), "child hook sweep: modules=%d patched=%d", seen, hit);
            msg[sizeof(msg) - 1] = 0;
            FvplLog(msg);
        }
        h = CreateThread(NULL, 0, watch_children, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
