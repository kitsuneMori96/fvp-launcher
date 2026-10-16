/* launcher.c — FVP 通用启动器: 定位游戏 exe -> 挂起创建进程 -> 注入 fvpl_hook.dll -> 放行
 *
 * 启动器本体不做任何 hook, 也不改游戏文件。注入要求同位数, 所以 x86 / x64 各出一套,
 * 由这里的 PE 位数校验保证不会拿 32 位 DLL 去注 64 位游戏。 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "config.h"
#include "pathutil.h"
#include "peinfo.h"
#include "winutil.h"

#if defined(__x86_64__) || defined(_M_X64)
#define OWN_MACHINE FVPL_MACHINE_AMD64
#define OWN_BITS    64
#else
#define OWN_MACHINE FVPL_MACHINE_I386
#define OWN_BITS    32
#endif

#define DLL_NAME L"fvpl_hook.dll"
#define MAX_BLOB (64u * 1024u * 1024u)

static WCHAR s_logpath[FVPL_MAX_PATH];

/* 弹窗看不到就等于没发生: 同一句话也落到 exe 旁边的 fvpl_launcher.log (UTF-8), 方便远程排障 */
static void alog(const WCHAR *fmt, ...) {
    WCHAR buf[1600];
    char utf8[4096];
    FILE *f;
    SYSTEMTIME st;
    va_list ap;
    int n;

    if (!s_logpath[0]) return;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1600, fmt, ap);
    va_end(ap);
    buf[1599] = 0;
    /* 不用 vfwprintf: 它按 MSVC 语义把 %s 当窄字符串, 宽字符路径会被截断 */
    n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (n <= 0) return;
    f = _wfopen(s_logpath, L"ab");
    if (!f) return;
    GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\n", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond, utf8);
    fclose(f);
}

static void box(UINT icon, const WCHAR *fmt, ...) {
    WCHAR buf[1600];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1600, fmt, ap);
    va_end(ap);
    buf[1599] = 0;
    alog(L"[错误] %s", buf);
    MessageBoxW(NULL, buf, L"FVP 启动器", MB_OK | icon);
}

static int read_blob(const WCHAR *path, unsigned char **out, unsigned int *size) {
    HANDLE h;
    LARGE_INTEGER sz;
    DWORD got;
    unsigned char *buf;

    *out = NULL;
    *size = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0x100 || (unsigned long long)sz.QuadPart > MAX_BLOB) {
        CloseHandle(h);
        return 0;
    }
    buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sz.QuadPart);
    if (!buf) { CloseHandle(h); return 0; }
    got = 0;
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *out = buf;
    *size = got;
    return 1;
}

/* FVP 引擎的存档根一定出自 SHGetFolderPathA(shell32.dll), 用它当指纹认出游戏主程序 */
static int is_fvp_exe(const WCHAR *path) {
    unsigned char *blob;
    unsigned int size;
    int hit, m;
    if (!read_blob(path, &blob, &size)) return 0;
    m = pe_machine(blob, size);
    hit = (m == (int)OWN_MACHINE) && pe_has_import(blob, size, "shell32.dll", "SHGetFolderPathA");
    HeapFree(GetProcessHeap(), 0, blob);
    return hit;
}

static void append(WCHAR *dst, int cap, int *len, const WCHAR *s) {
    while (*s && *len < cap - 1) dst[(*len)++] = *s++;
    dst[*len] = 0;
}

/* 目录里扫一遍, 命中 0 个失败、多个则提示并用第一个 */
static int auto_detect(const WCHAR *basedir, const WCHAR *self_name, WCHAR *out, int outcch) {
    WCHAR pat[FVPL_MAX_PATH], full[FVPL_MAX_PATH], chosen[FVPL_MAX_PATH], list[1600];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n_cand = 0;

    chosen[0] = 0;
    if (!pv_join(pat, FVPL_MAX_PATH, basedir, L"*.exe")) return 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (self_name && _wcsicmp(fd.cFileName, self_name) == 0) continue;
        if (!pv_join(full, FVPL_MAX_PATH, basedir, fd.cFileName)) continue;
        if (!is_fvp_exe(full)) continue;
        if (!n_cand) pv_copy(chosen, FVPL_MAX_PATH, full);
        n_cand++;
    } while (FindNextFileW(h, &fd) && n_cand < 16);
    FindClose(h);

    if (n_cand == 0) return 0;
    if (n_cand > 1) {
        int len = 0;
        WCHAR pat2[FVPL_MAX_PATH];
        WIN32_FIND_DATAW f2;
        HANDLE h2;
        append(list, 1600, &len, L"这个文件夹里有不止一个可能的游戏主程序，这次先用第一个：\n");
        if (!pv_join(pat2, FVPL_MAX_PATH, basedir, L"*.exe")) return 0;
        h2 = FindFirstFileW(pat2, &f2);
        if (h2 != INVALID_HANDLE_VALUE) {
            int shown = 0;
            do {
                WCHAR j[FVPL_MAX_PATH];
                if (f2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (self_name && _wcsicmp(f2.cFileName, self_name) == 0) continue;
                if (!pv_join(j, FVPL_MAX_PATH, basedir, f2.cFileName)) continue;
                if (!is_fvp_exe(j)) continue;
                if (++shown > 8 || len > 1400) break;
                append(list, 1600, &len, f2.cFileName);
                append(list, 1600, &len, L"\n");
            } while (FindNextFileW(h2, &f2));
            FindClose(h2);
        }
        append(list, 1600, &len, L"\n如果启动的不是你想玩的游戏：用记事本打开同目录的 fvp-launcher.ini，\n"
                                   L"在开头的 [launcher] 下面写一行 exe = 想玩的主程序完整路径。");
        box(MB_ICONWARNING, L"%s", list);
    }
    return pv_copy(out, outcch, chosen);
}

static void base_name(const WCHAR *path, WCHAR *name, int cch) {
    int i = pv_len(path);
    while (i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/') i--;
    pv_copy(name, cch, path + i);
}

static DWORD parse_pid(const WCHAR *s) {
    DWORD v = 0;
    int i;
    for (i = 0; s[i] >= L'0' && s[i] <= L'9'; i++) v = v * 10 + (DWORD)(s[i] - L'0');
    return v;
}

/* 延迟注入模式: FVPLauncher.exe --inject-ready <pid> [dll路径]
   hook 侧只在"调用方自己挂起子进程"时用它 —— 那种场合宿主(SakuraChs.exe)注入完就退出,
   不能在它里面等; 由这个独立进程等到子进程加载器初始化完(模块数超过 exe+ntdll)再注入。 */
static int inject_ready_mode(DWORD pid, const WCHAR *dllpath) {
    WCHAR dll[FVPL_MAX_PATH];
    const WCHAR *fail_step = NULL;
    HANDLE proc;
    int i, n;

    if (dllpath && dllpath[0]) {
        lstrcpynW(dll, dllpath, FVPL_MAX_PATH);
    } else {
        WCHAR own[FVPL_MAX_PATH], base[FVPL_MAX_PATH];
        if (GetModuleFileNameW(NULL, own, FVPL_MAX_PATH) == 0) return 1;
        if (!pv_dirname(base, FVPL_MAX_PATH, own)) return 1;
        if (!pv_join(dll, FVPL_MAX_PATH, base, DLL_NAME)) return 1;
    }

    proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                       PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (proc == NULL) {
        alog(L"延迟注入: 打不开 pid=%u (GetLastError=%u)", (unsigned)pid, (unsigned)GetLastError());
        return 1;
    }
    for (i = 0; i < 3000; i++) {
        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) {
            alog(L"延迟注入: pid=%u 已退出, 放弃", (unsigned)pid);
            CloseHandle(proc);
            return 1;
        }
        n = FvplProcessModuleCount(pid);
        if (n > 2) break;   /* 加载器已把导入表处理完, 汉化组塞的项都进来了 */
        Sleep(10);          /* 快照在对方加载器初始化途中可能拿不到, 继续等 */
    }
    alog(L"延迟注入: pid=%u 模块数=%d, 注入 %s", (unsigned)pid, FvplProcessModuleCount(pid), dll);
    if (FvplInjectDll(proc, pid, dll, 20000, NULL, &fail_step)) alog(L"延迟注入完成");
    else alog(L"延迟注入失败: %s", fail_step ? fail_step : L"未知");
    CloseHandle(proc);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    WCHAR own[FVPL_MAX_PATH], basedir[FVPL_MAX_PATH], self_name[FVPL_MAX_PATH];
    WCHAR dll[FVPL_MAX_PATH], target[FVPL_MAX_PATH];
    static WCHAR cmdline[FVPL_MAX_PATH * 4];
    static fvpl_wchar text[8192];
    static FvplConfig cfg;
    FvplIni ini;
    fvpl_wchar err[256];
    const WCHAR *fail_step = NULL;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    unsigned char *blob;
    unsigned int size;
    DWORD machine;
    int i, n, cap;

    (void)hInst; (void)hPrev; (void)lpCmd; (void)nShow;

    n = (int)GetModuleFileNameW(NULL, own, FVPL_MAX_PATH);
    if (n <= 0 || n >= FVPL_MAX_PATH) {
        box(MB_ICONERROR, L"启动器没能读到自己的位置，无法继续。\n"
            L"请把 FVPLauncher.exe 放在一个普通文件夹里再运行（不要直接从压缩包里运行）。\n\n"
            L"(错误码 %u)", (unsigned)GetLastError());
        return 1;
    }
    if (!pv_dirname(basedir, FVPL_MAX_PATH, own)) {
        box(MB_ICONERROR, L"启动器所在的文件夹路径太长了，Windows 无法处理。\n"
            L"请把游戏文件夹移到更短的位置（例如 D:\\games）再试。");
        return 1;
    }
    base_name(own, self_name, FVPL_MAX_PATH);
    if (pv_join(s_logpath, FVPL_MAX_PATH, basedir, L"fvpl_launcher.log"))
        alog(L"===== 启动 =====");

    /* 延迟注入模式: 只为 hook 侧服务, 不读 ini、不弹窗、不找游戏 */
    {
        int argc = 0;
        WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv != NULL) {
            if (argc >= 3 && lstrcmpW(argv[1], L"--inject-ready") == 0) {
                int rc = inject_ready_mode(parse_pid(argv[2]), argc >= 4 ? argv[3] : NULL);
                LocalFree(argv);
                return rc;
            }
            LocalFree(argv);
        }
    }

    for (i = 0; i < FVPL_INI_MAXENTRY; i++) { ini.ent[i].sec[0] = 0; ini.ent[i].key[0] = 0; }
    ini.n = 0;
    if (FvplLoadIniText(basedir, text, 8192)) cfg_ini_parse(text, &ini);
    err[0] = 0;
    if (!cfg_build(&ini, basedir, &cfg, err, 256)) {
        FvplIni empty;
        int bad = (err[0] != 0);
        empty.n = 0;
        if (!cfg_build(&empty, basedir, &cfg, err, 256)) {
            box(MB_ICONERROR, L"读配置失败，启动器无法继续。\n"
                L"请确认 fvp-launcher.ini 是普通文本文件，或者先把它删掉再试（删掉会用默认设置）。");
            return 1;
        }
        if (bad) box(MB_ICONWARNING,
            L"fvp-launcher.ini 里有看不懂的设置：\n%s\n\n本次先用默认设置启动。\n"
            L"（可以删掉这个文件重新开始，或对照 fvp-launcher.ini.example 检查。）", err);
    }

    if (cfg.target[0]) {
        if (pv_is_abs(cfg.target)) pv_copy(target, FVPL_MAX_PATH, cfg.target);
        else if (!pv_join(target, FVPL_MAX_PATH, basedir, cfg.target)) {
            box(MB_ICONERROR, L"配置文件里 [launcher] exe 写的路径太长了。\n"
                L"请把游戏文件夹移到更短的位置（例如 D:\\games）再试。");
            return 1;
        }
    } else if (!auto_detect(basedir, self_name, target, FVPL_MAX_PATH)) {
        box(MB_ICONERROR,
            L"没有在这个文件夹里找到游戏主程序：\n%s\n\n"
            L"请把 FVPLauncher.exe 和游戏主程序放在同一个文件夹里。\n"
            L"如果游戏装在别的地方：用记事本打开同目录的 fvp-launcher.ini，\n"
            L"在开头的 [launcher] 下面写一行：\n"
            L"    exe = 游戏主程序的完整路径", basedir);
        return 1;
    }

    if (!read_blob(target, &blob, &size)) {
        box(MB_ICONERROR, L"游戏主程序读不出来：\n%s\n\n"
            L"可能文件不在了，或正被别的程序占用。请确认文件还在，然后重试。", target);
        return 1;
    }
    machine = (DWORD)pe_machine(blob, size);
    HeapFree(GetProcessHeap(), 0, blob);
    if (machine != (DWORD)OWN_MACHINE) {
        box(MB_ICONERROR,
            L"版本不对：\n%s\n\n这是 %s 的游戏，而这个启动器是 %d 位的，两者必须一样。\n"
            L"请改用对应位数的 FVPLauncher（x86 = 32 位，x64 = 64 位）。",
            target, machine == (DWORD)FVPL_MACHINE_AMD64 ? L"64 位 (x64)" : L"32 位 (x86)", OWN_BITS);
        return 1;
    }

    if (!pv_join(dll, FVPL_MAX_PATH, basedir, DLL_NAME)) {
        box(MB_ICONERROR, L"路径太长了，Windows 无法处理。\n"
            L"请把游戏文件夹移到更短的位置（例如 D:\\games）再试。");
        return 1;
    }
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        box(MB_ICONERROR, L"缺少文件 %s。\n"
            L"它必须和 FVPLauncher.exe 放在同一个文件夹里，\n"
            L"请把安装包里的这几个文件一起复制到游戏文件夹。", DLL_NAME);
        return 1;
    }

    cap = (int)(sizeof(cmdline) / sizeof(cmdline[0]));
    if (pv_len(target) + pv_len(cfg.params) + 8 >= cap) {
        box(MB_ICONERROR, L"给游戏传的参数太长，无法启动。\n"
            L"请检查 fvp-launcher.ini 里 [launcher] params 的内容。");
        return 1;
    }
    alog(L"目标程序: %s", target);
    {
        int len = 0;
        append(cmdline, cap, &len, L"\"");
        append(cmdline, cap, &len, target);
        append(cmdline, cap, &len, L"\"");
        if (cfg.params[0]) { append(cmdline, cap, &len, L" "); append(cmdline, cap, &len, cfg.params); }
    }

    RtlZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    RtlZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL,
                        cfg.workdir[0] ? cfg.workdir : basedir, &si, &pi)) {
        box(MB_ICONERROR, L"游戏启动失败：\n%s\n\n"
            L"可能文件被占用或没有权限（可以试试右键“以管理员身份运行”）。\n\n"
            L"(错误码 %u)", target, (unsigned)GetLastError());
        return 1;
    }

    if (!FvplInjectDll(pi.hProcess, pi.dwProcessId, dll, 20000, NULL, &fail_step)) {
        /* 注入不成功就杀掉: 让游戏裸跑会把档写到真实 Documents, 正是要避免的事 */
        box(MB_ICONERROR,
            L"启动器没能接通游戏。\n原因：%s\n\n"
            L"为避免存档被写乱，已经把游戏关掉了。\n"
            L"常见原因：杀毒软件拦截、游戏被别的程序占用。\n"
            L"重试仍失败时，请把游戏文件夹里的 fvpl_hook.log 发给作者。\n\n"
            L"(错误码 %u)", fail_step ? fail_step : L"未知原因", (unsigned)GetLastError());
        TerminateProcess(pi.hProcess, 1);
    } else {
        ResumeThread(pi.hThread);
        alog(L"注入完成, 已放行 (pid=%u)", (unsigned)pi.dwProcessId);
    }

    if (pi.hThread) CloseHandle(pi.hThread);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    return 0;
}
