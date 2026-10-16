/* core.c — hook DLL 的配置加载、惰性初始化、虚拟代码页换算与存档目录落地 */
#include "hooks.h"
#include "pathutil.h"
#include "winutil.h"
#include <shlobj.h>
#include <stdio.h>

FvplConfig g_cfg;

static volatile LONG s_state = 0;      /* 0=未初始化 1=进行中 2=就绪 3=配置不可用 */
static WCHAR s_base[FVPL_MAX_PATH];    /* 存档重定向根 (游戏目录或配置目录) */
static char  s_baseA[FVPL_MAX_PATH];   /* 同一目录的虚拟 ACP 形态, 给 ANSI 引擎 */
static int   s_base_ok = 0;
static int   s_warned = 0;
static FILE *s_log = NULL;

void FvplLog(const char *what) {
    SYSTEMTIME st;
    WCHAR dir[FVPL_MAX_PATH], lp[FVPL_MAX_PATH];
    DWORD n;
    if (!what) return;
    if (!s_log) {
        n = GetModuleFileNameW(NULL, dir, FVPL_MAX_PATH);
        if (n == 0 || n >= FVPL_MAX_PATH) return;
        if (!pv_dirname(dir, FVPL_MAX_PATH, dir)) return;
        if (!pv_join(lp, FVPL_MAX_PATH, dir, L"fvpl_hook.log")) return;
        s_log = _wfopen(lp, L"ab");
        if (!s_log) return;
    }
    GetLocalTime(&st);
    fprintf(s_log, "%04d-%02d-%02d %02d:%02d:%02d %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, what);
    fflush(s_log);
}

static void warn_once(const WCHAR *msg) {
    if (s_warned) return;
    s_warned = 1;
    MessageBoxW(NULL, msg, L"FVP 启动器 (fvpl_hook)", MB_OK | MB_ICONWARNING);
}

static void ensure_dirs_w(const WCHAR *path) {
    WCHAR cur[FVPL_MAX_PATH];
    int i, n;
    if (!pv_copy(cur, FVPL_MAX_PATH, path)) return;
    n = pv_len(cur);
    for (i = 1; i <= n; i++) {
        if (cur[i] == L'\\' || cur[i] == 0) {
            WCHAR keep = cur[i];
            cur[i] = 0;
            CreateDirectoryW(cur, NULL);
            cur[i] = keep;
            if (!keep) break;
        }
    }
}

/* 递归复制: 目标已存在的文件一律跳过, 源永不删除 */
static void copy_tree(const WCHAR *src, const WCHAR *dst) {
    WCHAR pat[FVPL_MAX_PATH], s[FVPL_MAX_PATH], t[FVPL_MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    CreateDirectoryW(dst, NULL);
    if (!pv_join(pat, FVPL_MAX_PATH, src, L"*")) return;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(fd.cFileName, L".")) continue;
        if (!wcscmp(fd.cFileName, L"..")) continue;
        if (!pv_join(s, FVPL_MAX_PATH, src, fd.cFileName)) continue;
        if (!pv_join(t, FVPL_MAX_PATH, dst, fd.cFileName)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copy_tree(s, t);
        else CopyFileW(s, t, TRUE);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int has_marker(void) {
    WCHAR m[FVPL_MAX_PATH];
    return pv_join(m, FVPL_MAX_PATH, s_base, L".fvpl-migrated") &&
           GetFileAttributesW(m) != INVALID_FILE_ATTRIBUTES;
}

static void put_marker(void) {
    WCHAR m[FVPL_MAX_PATH];
    HANDLE h;
    if (!pv_join(m, FVPL_MAX_PATH, s_base, L".fvpl-migrated")) return;
    h = CreateFileW(m, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

/* 老档迁移: 真 Documents 下命中 migrate_names 的一级目录整体复制进存档根。
 * 走 W 版 API 取 Documents —— 用户名是中文时 A 版会按真实 ACP 编码, 换页就错。 */
static void maybe_migrate(void) {
    WCHAR docs[FVPL_MAX_PATH], src[FVPL_MAX_PATH], dst[FVPL_MAX_PATH];
    WCHAR token[256];
    HRESULT hr;
    if (!g_cfg.save_migrate) return;
    if (has_marker()) return;
    hr = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
    if (FAILED(hr) || !docs[0]) return;
    if (pv_ci_equal(docs, s_base)) { put_marker(); return; }
    {
        int i = 0, k = 0, n = pv_len(g_cfg.migrate_names);
        if (n <= 0) { put_marker(); return; }
        for (;;) {
            int all;
            while (i < n && (g_cfg.migrate_names[i] == L';' || g_cfg.migrate_names[i] == L',' ||
                             g_cfg.migrate_names[i] == L' ')) i++;
            k = 0;
            while (i < n && g_cfg.migrate_names[i] != L';' && g_cfg.migrate_names[i] != L',' &&
                   g_cfg.migrate_names[i] != L' ') {
                if (k < 255) token[k++] = g_cfg.migrate_names[i];
                i++;
            }
            token[k] = 0;
            if (k == 0) break;
            all = (token[0] == L'*' && token[1] == 0);
            if (all) {
                WIN32_FIND_DATAW fd;
                HANDLE h;
                WCHAR pat[FVPL_MAX_PATH];
                if (!pv_join(pat, FVPL_MAX_PATH, docs, L"*")) break;
                h = FindFirstFileW(pat, &fd);
                if (h == INVALID_HANDLE_VALUE) break;
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                    if (!pv_join(src, FVPL_MAX_PATH, docs, fd.cFileName)) continue;
                    if (!pv_join(dst, FVPL_MAX_PATH, s_base, fd.cFileName)) continue;
                    copy_tree(src, dst);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            } else {
                if (!pv_join(src, FVPL_MAX_PATH, docs, token)) break;
                if (!pv_join(dst, FVPL_MAX_PATH, s_base, token)) break;
                if (GetFileAttributesW(src) != INVALID_FILE_ATTRIBUTES) copy_tree(src, dst);
            }
            if (i >= n) break;
        }
    }
    put_marker();
}

static void do_init(void) {
    WCHAR exepath[FVPL_MAX_PATH], basedir[FVPL_MAX_PATH];
    static fvpl_wchar text[8192];
    FvplIni ini;
    fvpl_wchar err[256];
    BOOL used;
    int n;

    pv_copy(err, 256, L"");
    n = (int)GetModuleFileNameW(NULL, exepath, FVPL_MAX_PATH);
    if (n <= 0 || n >= FVPL_MAX_PATH) { s_state = 3; return; }
    if (!pv_dirname(basedir, FVPL_MAX_PATH, exepath)) { s_state = 3; return; }
    if (!pv_copy(s_base, FVPL_MAX_PATH, basedir)) { s_state = 3; return; }

    for (int i = 0; i < FVPL_INI_MAXENTRY; i++) { ini.ent[i].sec[0] = 0; ini.ent[i].key[0] = 0; }
    ini.n = 0;
    if (FvplLoadIniText(basedir, text, 8192))
        cfg_ini_parse(text, &ini);
    /* 没有 ini 也要能跑: 空配置 = 默认值 (重定向到 userdata + 932) */
    if (!cfg_build(&ini, basedir, &g_cfg, err, 256)) {
        FvplIni empty;
        empty.n = 0;
        if (!cfg_build(&empty, basedir, &g_cfg, err, 256)) { s_state = 3; return; }
        warn_once(L"fvp-launcher.ini 配置有误, 已按默认值运行。");
    }
    if (!g_cfg.save_redirect) { s_base[0] = 0; s_state = 2; return; }

    pv_copy(s_base, FVPL_MAX_PATH, g_cfg.save_path);
    ensure_dirs_w(s_base);
    /* 引擎拿到的是 ANSI 路径: 目录名必须能用虚拟 ACP 无损表示, 否则宁可不重定向 */
    used = 0;
    if (WideCharToMultiByte(g_cfg.codepage, WC_NO_BEST_FIT_CHARS, s_base, -1,
                            NULL, 0, NULL, &used) <= 0 || used) {
        s_base_ok = 0;
    } else {
        WideCharToMultiByte(g_cfg.codepage, 0, s_base, -1, s_baseA, FVPL_MAX_PATH, NULL, NULL);
        s_base_ok = 1;
    }
    maybe_migrate();
    s_state = 2;
}

void FvplInitOnce(void) {
    LONG want = InterlockedCompareExchange(&s_state, 1, 0);
    if (want == 0) do_init();
    while (s_state == 1) Sleep(1);
}

int FvplReady(void) {
    FvplInitOnce();
    return s_state == 2;
}

UINT FvplMapCP(UINT cp) {
    if (!FvplReady()) return cp;
    if (cp == CP_ACP || cp == CP_THREAD_ACP) return g_cfg.codepage;
    return cp;
}

int FvplA2W(const char *a, WCHAR *w, int cch) {
    return MultiByteToWideChar(g_cfg.codepage, 0, a, -1, w, cch);
}

int FvplW2A(const WCHAR *w, char *a, int cch) {
    return WideCharToMultiByte(g_cfg.codepage, 0, w, -1, a, cch, NULL, NULL);
}

const WCHAR *FvplSaveDirW(void) {
    if (!FvplReady() || !g_cfg.save_redirect) return NULL;
    if (!s_base_ok) {
        warn_once(L"存档目录无法用当前 codepage 无损表示,\n"
                  L"存档重定向已自动关闭 (保持游戏原行为)。\n"
                  L"请把游戏放到能被该代码页表示的路径, 或换 codepage。");
        return NULL;
    }
    return s_base;
}
