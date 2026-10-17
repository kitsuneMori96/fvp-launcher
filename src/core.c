/* core.c — hook DLL 的配置加载、惰性初始化、虚拟代码页换算与存档目录落地 */
#include "hooks.h"
#include "pathutil.h"
#include "winutil.h"
#include <shlobj.h>
#include <shellapi.h>
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

/* ---- 懒迁移: 游戏第一次来读某一份存档时才把它搬过来 ----
 * 旧做法是启动时把 Documents\<migrate_names> 整个复制进存档根, 于是机器上玩过很多
 * 游戏时, 会把所有游戏的存档全灌进这个游戏目录。现在改成按需:
 *   游戏要读 <存档根>\<相对路径>  ->  目标不存在, 而 Documents\<相对路径> 存在时,
 *   只把那一份 (或它所在的那一层目录) 复制过来。
 * 结果: 每个游戏只带进自己的存档, 游戏没访问过的东西一个字节都不动。 */
static int path_exists_w(const WCHAR *p) {
    return p != NULL && p[0] != 0 && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

/* root 是 path 的前缀 (忽略大小写, 后面必须紧跟分隔符) 时 *rest = 相对部分 */
static int path_under_w(const WCHAR *root, const WCHAR *path, const WCHAR **rest) {
    int i, n = pv_len(root);
    if (n <= 0 || path == NULL) return 0;
    for (i = 0; i < n; i++) {
        WCHAR a = root[i], b = path[i];
        if (!b) return 0;
        if (a >= L'a' && a <= L'z') a = (WCHAR)(a - 32);
        if (b >= L'a' && b <= L'z') b = (WCHAR)(b - 32);
        if (a != b) return 0;
    }
    if (path[n] != L'\\' && path[n] != L'/') return 0;
    *rest = path + n + 1;
    return 1;
}

/* 相对路径的第一段目录名是否在 migrate_names 白名单里 ("*" = 不限) */
static int migrate_name_ok(const WCHAR *rel) {
    WCHAR first[260], token[256];
    int i = 0, k = 0, n = pv_len(g_cfg.migrate_names);
    if (n <= 0 || rel == NULL) return 0;
    while (rel[k] && rel[k] != L'\\' && rel[k] != L'/' && k < 259) { first[k] = rel[k]; k++; }
    first[k] = 0;
    if (k == 0) return 0;
    for (;;) {
        int tk = 0, all;
        while (i < n && (g_cfg.migrate_names[i] == L';' || g_cfg.migrate_names[i] == L',' ||
                         g_cfg.migrate_names[i] == L' ')) i++;
        while (i < n && g_cfg.migrate_names[i] != L';' && g_cfg.migrate_names[i] != L',' &&
               g_cfg.migrate_names[i] != L' ') {
            if (tk < 255) token[tk++] = g_cfg.migrate_names[i];
            i++;
        }
        token[tk] = 0;
        if (tk == 0) return 0;
        all = (token[0] == L'*' && token[1] == 0);
        if (all || pv_ci_equal(first, token)) return 1;
        if (i >= n) return 0;
    }
}

/* 同一次运行里同一个相对路径只搬一次: 游戏轮询空存档位时会反复来问同一份 */
static unsigned rel_hash(const WCHAR *s) {
    unsigned h = 2166136261u;
    for (; s != NULL && *s != 0; s++) {
        unsigned c = (unsigned)(*s);
        h = (h ^ (c & 0xFFu)) * 16777619u;
        h = (h ^ (c >> 8)) * 16777619u;
    }
    return h;
}

static int migrate_already(unsigned h) {
    static unsigned seen[32];
    static int n = 0;
    int i;
    for (i = 0; i < n; i++) {
        if (seen[i] == h) return 1;
    }
    if (n < 32) seen[n++] = h;
    return 0;
}

/* 游戏访问 path 时调用: 需要就先只搬这一份过来 */
void FvplMigratePathW(const WCHAR *path) {
    static LONG busy = 0;
    WCHAR docs[FVPL_MAX_PATH], src[FVPL_MAX_PATH], dst[FVPL_MAX_PATH];
    WCHAR rel[FVPL_MAX_PATH], parent[FVPL_MAX_PATH];
    const WCHAR *rest;
    HRESULT hr;
    if (!g_cfg.save_migrate || !s_base_ok || s_base[0] == 0) return;
    if (!path_under_w(s_base, path, &rest) || rest[0] == 0) return;
    if (!migrate_name_ok(rest)) return;
    if (path_exists_w(path)) return;                 /* 已经有一份了, 不动 */
    if (InterlockedExchange(&busy, 1) != 0) return;  /* 复制过程中可能再被调进来 */
    hr = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
    if (SUCCEEDED(hr) && docs[0] && !pv_ci_equal(docs, s_base)) {
        if (!pv_copy(rel, FVPL_MAX_PATH, rest)) rel[0] = 0;
        for (int hop = 0; rel[0] != 0 && hop < 8; hop++) {
            if (!pv_join(src, FVPL_MAX_PATH, docs, rel)) break;
            if (path_exists_w(src)) {
                if (migrate_already(rel_hash(rel))) break;   /* 这次运行已经搬过这一份 */
                if (pv_join(dst, FVPL_MAX_PATH, s_base, rel) && !path_exists_w(dst)) {
                    if (GetFileAttributesW(src) & FILE_ATTRIBUTE_DIRECTORY) {
                        copy_tree(src, dst);
                    } else {
                        if (pv_dirname(parent, FVPL_MAX_PATH, dst)) ensure_dirs_w(parent);
                        CopyFileW(src, dst, TRUE);
                    }
                    {
                        char a[512];
                        if (WideCharToMultiByte(CP_UTF8, 0, rel, -1, a, (int)sizeof(a),
                                                NULL, NULL) > 0) {
                            FvplLog("迁移存档 (只搬本游戏这一份):");
                            FvplLog(a);
                        }
                    }
                }
                break;
            }
            /* Documents 里没有这一份: 退到它所在的目录再试, 但绝不越过第一段
             * (第一段就是白名单里的游戏名 -> 再往上就成了"把所有游戏一起搬") */
            if (!pv_dirname(parent, FVPL_MAX_PATH, rel)) break;
            if (parent[0] == 0 || pv_ci_equal(parent, rel)) break;
            {
                int k;
                for (k = 0; parent[k] && parent[k] != L'\\' && parent[k] != L'/'; k++) ;
                if (parent[k] == 0) break;           /* 只剩第一段了, 到此为止 */
            }
            if (!pv_copy(rel, FVPL_MAX_PATH, parent)) break;
        }
    }
    InterlockedExchange(&busy, 0);
}

static void do_init(void) {
    WCHAR exepath[FVPL_MAX_PATH], basedir[FVPL_MAX_PATH];
    static fvpl_wchar text[8192];
    FvplIni ini;
    fvpl_wchar err[256];
    BOOL used;
    int n;

    pv_copy(err, 256, L"");
    cfg_set_host_acp(FvplHostAcp());
    n = (int)GetModuleFileNameW(NULL, exepath, FVPL_MAX_PATH);
    if (n <= 0 || n >= FVPL_MAX_PATH) { s_state = 3; return; }
    if (!pv_dirname(basedir, FVPL_MAX_PATH, exepath)) { s_state = 3; return; }
    if (!pv_copy(s_base, FVPL_MAX_PATH, basedir)) { s_state = 3; return; }

    for (int i = 0; i < FVPL_INI_MAXENTRY; i++) { ini.ent[i].sec[0] = 0; ini.ent[i].key[0] = 0; }
    ini.n = 0;
    if (FvplLoadIniText(basedir, text, 8192))
        cfg_ini_parse(text, &ini);
    /* 没有 ini 也要能跑: 空配置 = 默认值 (重定向到 userdata + 本机码页) */
    if (!cfg_build(&ini, basedir, &g_cfg, err, 256)) {
        FvplIni empty;
        empty.n = 0;
        if (!cfg_build(&empty, basedir, &g_cfg, err, 256)) { s_state = 3; return; }
        warn_once(L"fvp-launcher.ini 里有看不懂的设置，这次先用默认设置运行。\n"
                  L"（可以删掉这个文件重新开始。）");
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
    /* 存档迁移改成懒迁移: 游戏访问哪一份才搬哪一份, 见 FvplMigratePathW() */
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

/* 文本层码页: 未设 text_codepage 时与系统/路径码页相同 (等价于不做文本层转换)。
 * 日文原版装在中文系统上时 = 932, 这样标题和台词按游戏自己的编码解释,
 * 而 GetACP / 文件路径仍然按本机 936, 引擎取路径就不会错。 */
UINT FvplTextCp(void) {
    return g_cfg.text_codepage ? g_cfg.text_codepage : g_cfg.codepage;
}

int FvplTextA2W(const char *a, WCHAR *w, int cch) {
    return MultiByteToWideChar(FvplTextCp(), 0, a, -1, w, cch);
}

int FvplTextW2A(const WCHAR *w, char *a, int cch) {
    return WideCharToMultiByte(FvplTextCp(), 0, w, -1, a, cch, NULL, NULL);
}

const WCHAR *FvplSaveDirW(void) {
    if (!FvplReady() || !g_cfg.save_redirect) return NULL;
    if (!s_base_ok) {
        warn_once(L"这个游戏的文件夹路径里有当前「文字编码」存不下的字符，\n"
                  L"所以「把存档存到游戏目录」这个功能先关掉了，游戏本身不受影响，\n"
                  L"存档会像以前一样放在「文档」里。\n\n"
                  L"想让存档存到游戏目录，二选一：\n"
                  L"· 把游戏文件夹改成纯英文/数字的名字（例如 D:\\games\\irotoridori）\n"
                  L"· 或用记事本打开 fvp-launcher.ini，把 [nls] 下面的 codepage 改成 936");
        return NULL;
    }
    return s_base;
}

/* ---- [launcher] icon: 运行时给游戏窗口换图标 ------------------------------
 * 只影响外观: 换成配置里的 .ico (或 .exe 里的第 0 号图标)。游戏窗口一般比 DLL 晚
 * 建好, 所以这里 250ms 一步地找, 最多试 ~20 秒; 找不到就放弃 (不打扰游戏)。 */
typedef struct { HWND best; LONG area; } IconFind;

static BOOL CALLBACK icon_find_cb(HWND hwnd, LPARAM lp) {
    IconFind *c = (IconFind *)lp;
    DWORD pid = 0;
    WCHAR cls[64];
    RECT r;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    cls[0] = 0;
    GetClassNameW(hwnd, cls, 64);
    if (lstrcmpW(cls, L"HS_MAIN_WINDOW_CLASS00") == 0) { c->best = hwnd; return FALSE; }
    if (GetWindowRect(hwnd, &r)) {          /* 没有指定类名时取面积最大的可见顶层窗口 */
        LONG a = (r.right - r.left) * (r.bottom - r.top);
        if (a > c->area) { c->area = a; c->best = hwnd; }
    }
    return TRUE;
}

static void log_wide(const WCHAR *w) {
    char m[1024];
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, m, (int)sizeof(m), NULL, NULL) > 0)
        FvplLog(m);
}

void FvplApplyWindowIcon(void) {
    WCHAR path[FVPL_MAX_PATH], exe[FVPL_MAX_PATH], dir[FVPL_MAX_PATH];
    HICON big = NULL, small = NULL;
    int tries;

    if (g_cfg.icon[0] == 0) return;

    if (pv_is_abs(g_cfg.icon)) {
        if (!pv_copy(path, FVPL_MAX_PATH, g_cfg.icon)) return;
    } else {
        if (GetModuleFileNameW(NULL, exe, FVPL_MAX_PATH) == 0) return;
        if (!pv_dirname(dir, FVPL_MAX_PATH, exe)) return;
        if (!pv_join(path, FVPL_MAX_PATH, dir, g_cfg.icon)) return;
    }

    big = (HICON)LoadImageW(NULL, path, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (big) {
        small = (HICON)LoadImageW(NULL, path, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_LOADFROMFILE);
    } else if (pv_ends_with_ci(path, L".exe")) {
        /* 不是能直接当图片读的文件: 试试当成另一个 exe, 取它的第 0 号图标 */
        if (ExtractIconExW(path, 0, &big, &small, 1) <= 0) { big = NULL; small = NULL; }
    }
    if (!big) {
        WCHAR w[1024];
        _snwprintf(w, 1024, L"窗口图标: 读不到 %s", path);
        w[1023] = 0;
        log_wide(w);
        return;
    }
    if (!small) small = big;

    for (tries = 0; tries < 80; tries++) {
        IconFind c;
        c.best = NULL;
        c.area = 0;
        EnumWindows(icon_find_cb, (LPARAM)&c);
        if (c.best) {
            WCHAR w[1024];
            SendMessageW(c.best, WM_SETICON, ICON_BIG, (LPARAM)big);
            SendMessageW(c.best, WM_SETICON, ICON_SMALL, (LPARAM)small);
            SetClassLongPtrW(c.best, GCLP_HICON, (LONG_PTR)big);
            SetClassLongPtrW(c.best, GCLP_HICONSM, (LONG_PTR)small);
            _snwprintf(w, 1024, L"窗口图标已应用: %s", path);
            w[1023] = 0;
            log_wide(w);
            return;
        }
        Sleep(250);
    }
    log_wide(L"窗口图标: 没找到窗口, 跳过");
}
