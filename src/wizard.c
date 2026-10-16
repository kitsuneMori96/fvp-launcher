/* wizard.c — 首次设定界面: 没有 fvp-launcher.ini 时先弹一次
 *
 * 为什么要有它: 不给技术背景的玩家也能用 —— 把自动推断出来的东西(游戏主程序 /
 * 游戏文本语言 / 存档重定向 / 汉化链)摆在界面上让用户确认一次, 点“开始游戏”就写出
 * fvp-launcher.ini; 之后每次双击都直接跑, 不再打扰。
 *
 * 只写启动器自己目录里的 fvp-launcher.ini, 不碰游戏目录里的任何游戏文件。
 * 删除 ini 就会重新弹这个界面。 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "wizard.h"
#include "pathutil.h"
#include "winutil.h"

/* mingw 的 -std=c11 里没有 _snwprintf 的原型（launcher.c 用的 _vsnwprintf 有），
 * 这里自己包一层：写不下就截断，永远保证 0 结尾。 */
static int wiz_fmt(fvpl_wchar *buf, int cch, const fvpl_wchar *fmt, ...) {
    va_list ap;
    int n;
    if (cch <= 0) return 0;
    buf[0] = 0;
    va_start(ap, fmt);
    n = _vsnwprintf(buf, (size_t)cch, fmt, ap);
    va_end(ap);
    buf[cch - 1] = 0;
    return n;
}

#define WIZ_CLASS  L"FVPL_WIZARD_WINDOW"
#define WIZ_W      600
#define WIZ_H      340

#define IDC_EXE      2001
#define IDC_LANG     2002
#define IDC_SHELL    2004
#define IDC_REDIRECT 2005
#define IDC_MIGRATE  2006
#define IDC_INFO     2007

/* 游戏文本语言选项。cp 是"游戏文本码页"(0 = 不做文本层转换, 跟随系统);
 * cs 是配套 GDI 字符集, 交给 hook 侧强制字体字符集。 */
typedef struct {
    const fvpl_wchar *name;
    int cp;
    int cs;
} LangItem;

static const LangItem k_lang[] = {
    { L"自动（跟随系统，不做文本转换）", 0,     0 },
    { L"日本語（932 / Shift-JIS）",    932, 128 },
    { L"简体中文（936 / GBK）",        936, 134 },
    { L"繁體中文（950 / Big5）",       950, 136 },
    { L"한국어（949）",                949, 129 }
};
#define N_LANG ((int)(sizeof(k_lang) / sizeof(k_lang[0])))

typedef struct {
    HINSTANCE inst;
    const fvpl_wchar *basedir;
    const WizCand *cands;
    int n;
    int host_cp;               /* 本机 ACP: “自动”与“系统与路径编码”都用它 */
    int host_cs;               /* 自动时给的字符集(跟随系统) */
    int lang_idx;              /* 组合框当前索引 */
    int lang_auto;             /* 语言是自动识别的(1) 还是用户手改的(0) */
    int ok;                    /* 用户点了开始游戏 */
    FvplConfig *cfg;
} WizCtx;

static WizCtx *g_wiz;

static void ui_font(HWND h) {
    SendMessageW(h, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

static HWND ui_make(HWND parent, const fvpl_wchar *cls, const fvpl_wchar *text,
                    DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id, g_wiz->inst, NULL);
    if (c) ui_font(c);
    return c;
}

static int ui_checked(HWND parent, int id) {
    return (int)SendMessageW(GetDlgItem(parent, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void ui_check(HWND parent, int id, int on) {
    SendMessageW(GetDlgItem(parent, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

/* 组合框里的语言名(0 号就是固定的“自动”文案) */
static const fvpl_wchar *lang_label(int idx) {
    if (idx < 0 || idx >= N_LANG) return k_lang[0].name;
    return k_lang[idx].name;
}

/* 摘要里用的短语言名 */
static const fvpl_wchar *lang_name(int idx) {
    if (idx <= 0 || idx >= N_LANG) return L"自动";
    return k_lang[idx].name;
}

/* 检测出来的文本码页 -> 组合框索引(认不出就回“自动”) */
static int cp_to_lang_idx(int cp) {
    int i;
    if (cp == 0) return 0;
    for (i = 1; i < N_LANG; i++) if (k_lang[i].cp == cp) return i;
    return 0;
}

/* 组合框索引 -> 文本码页(0 = 不转换) */
static int lang_cp(int idx) {
    if (idx <= 0 || idx >= N_LANG) return 0;
    return k_lang[idx].cp;
}

/* 从完整路径里取文件名 */
static void wiz_base(fvpl_wchar *name, int cch, const fvpl_wchar *path) {
    int i = pv_len(path);
    while (i > 0 && path[i - 1] != L'\\' && path[i - 1] != L'/') i--;
    pv_copy(name, cch, path + i);
}

/* 界面底部那行随时更新的“这次会怎么跑”摘要 */
static void ui_info(HWND hwnd) {
    fvpl_wchar buf[1200], name[FVPL_MAX_PATH];
    HWND exe = GetDlgItem(hwnd, IDC_EXE);
    int sel = (int)SendMessageW(exe, CB_GETCURSEL, 0, 0);
    int li = (int)SendMessageW(GetDlgItem(hwnd, IDC_LANG), CB_GETCURSEL, 0, 0);
    int tcp = lang_cp(li);

    name[0] = 0;
    if (sel >= 0 && sel < g_wiz->n) wiz_base(name, FVPL_MAX_PATH, g_wiz->cands[sel].path);

    wiz_fmt(buf, 1200,
        L"启动：%s\r\n"
        L"游戏文本语言：%s（%s）\r\n"
        L"系统与路径编码：本机 %d（不动）\r\n"
        L"存档：%s\r\n"
        L"汉化链：%s\r\n"
        L"字体修正：%s",
        name[0] ? name : L"（还没选）",
        lang_name(li), g_wiz->lang_auto ? L"自动识别" : L"手动",
        g_wiz->host_cp,
        ui_checked(hwnd, IDC_REDIRECT) ? L"游戏目录下的 userdata（随时可删）"
                                       : L"系统默认（我的文档）",
        ui_checked(hwnd, IDC_SHELL) ? L"需要跟到游戏本体" : L"关",
        tcp != 0 ? L"随游戏文本语言自动生效" : L"关");
    buf[1199] = 0;
    SetWindowTextW(GetDlgItem(hwnd, IDC_INFO), buf);
}

/* 换了主程序: 还在自动模式下就按新候选检测到的码页改语言 */
static void ui_exe_changed(HWND hwnd) {
    if (g_wiz->lang_auto) {
        int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_EXE), CB_GETCURSEL, 0, 0);
        int cp = (sel >= 0 && sel < g_wiz->n) ? g_wiz->cands[sel].cp : 0;
        SendMessageW(GetDlgItem(hwnd, IDC_LANG), CB_SETCURSEL, (WPARAM)cp_to_lang_idx(cp), 0);
    }
    ui_info(hwnd);
}

/* 把界面上的选择写进 cfg */
static void ui_apply(HWND hwnd) {
    FvplConfig *c = g_wiz->cfg;
    int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_EXE), CB_GETCURSEL, 0, 0);
    int lang = (int)SendMessageW(GetDlgItem(hwnd, IDC_LANG), CB_GETCURSEL, 0, 0);
    fvpl_wchar name[FVPL_MAX_PATH];

    if (sel >= 0 && sel < g_wiz->n) {
        /* 只写文件名: 游戏和启动器在同一目录, 这样整个文件夹可以随意搬 */
        wiz_base(name, FVPL_MAX_PATH, g_wiz->cands[sel].path);
        pv_copy(c->target, FVPL_MAX_PATH, name);
    }
    /* 系统/路径层永远跟着本机 ACP, 绝不谎报; 界面只改文本层 text_codepage */
    c->codepage = (fvpl_uint)g_wiz->host_cp;
    if (lang <= 0 || lang >= N_LANG) {
        c->text_codepage = 0;
        c->font_charset = (fvpl_uint)g_wiz->host_cs;
    } else {
        c->text_codepage = (fvpl_uint)k_lang[lang].cp;
        c->font_charset = (fvpl_uint)k_lang[lang].cs;
    }
    c->virtualize_files = 1;
    c->virtualize_ui = 0;
    c->inject_children = ui_checked(hwnd, IDC_SHELL);
    c->save_redirect = ui_checked(hwnd, IDC_REDIRECT);
    c->save_migrate = ui_checked(hwnd, IDC_MIGRATE);
    if (!c->save_path[0]) pv_copy(c->save_path, FVPL_MAX_PATH, L"userdata");
    if (!c->migrate_names[0]) pv_copy(c->migrate_names, 256, L"FAVORITE");
    g_wiz->lang_idx = lang;
}

static void ui_fill(HWND hwnd, const fvpl_wchar *preselect) {
    HWND exe = GetDlgItem(hwnd, IDC_EXE), lang = GetDlgItem(hwnd, IDC_LANG);
    fvpl_wchar buf[FVPL_MAX_PATH + 48], name[FVPL_MAX_PATH];
    int i, pick = 0;
    const int bcap = (int)(sizeof(buf) / sizeof(buf[0]));

    for (i = 0; i < g_wiz->n; i++) {
        wiz_base(name, FVPL_MAX_PATH, g_wiz->cands[i].path);
        if (g_wiz->cands[i].is_fvp) pv_copy(buf, bcap, name);
        else wiz_fmt(buf, bcap, L"%s（可能是汉化/中文版启动器）", name);
        SendMessageW(exe, CB_ADDSTRING, 0, (LPARAM)buf);
        if (preselect && _wcsicmp(g_wiz->cands[i].path, preselect) == 0) pick = i;
    }
    if (g_wiz->n > 0) SendMessageW(exe, CB_SETCURSEL, (WPARAM)pick, 0);

    for (i = 0; i < N_LANG; i++) SendMessageW(lang, CB_ADDSTRING, 0, (LPARAM)lang_label(i));

    /* 语言按选中候选检测到的码页自动预选; 用户一旦手改 IDC_LANG 就停自动 */
    g_wiz->lang_auto = 1;
    {
        int cp = (pick >= 0 && pick < g_wiz->n) ? g_wiz->cands[pick].cp : 0;
        SendMessageW(lang, CB_SETCURSEL, (WPARAM)cp_to_lang_idx(cp), 0);
    }

    ui_check(hwnd, IDC_SHELL, g_wiz->cfg->inject_children);
    ui_check(hwnd, IDC_REDIRECT, g_wiz->cfg->save_redirect);
    ui_check(hwnd, IDC_MIGRATE, g_wiz->cfg->save_migrate);
    ui_info(hwnd);
}

static LRESULT CALLBACK WizProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        ui_make(hwnd, L"STATIC",
                L"这个文件夹里还没有配置文件。下面是我们按游戏和系统推断出来的设置，确认一下就能开始。\r\n"
                L"点「开始游戏」会把它写进 fvp-launcher.ini（以后想改：删掉该文件再运行，或用记事本改）。",
                SS_LEFT, 16, 10, WIZ_W - 32, 46, -1);
        ui_make(hwnd, L"STATIC", L"游戏主程序", SS_LEFT, 16, 60, 100, 18, -1);
        ui_make(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                124, 58, WIZ_W - 140, 240, IDC_EXE);
        ui_make(hwnd, L"STATIC", L"游戏文本语言", SS_LEFT, 16, 90, 120, 18, -1);
        ui_make(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                124, 88, WIZ_W - 140, 240, IDC_LANG);
        ui_make(hwnd, L"BUTTON",
                L"这个 exe 是汉化启动器，游戏本体由它再启动（例如 SakuraChs.exe 会再启动 Sakura.exe）：需要跟到游戏本体时勾选",
                BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 16, 114, WIZ_W - 32, 32, IDC_SHELL);
        ui_make(hwnd, L"BUTTON", L"把存档放进游戏目录下的 userdata（不改系统里的存档，删掉即还原）",
                BS_AUTOCHECKBOX | WS_TABSTOP, 16, 150, WIZ_W - 32, 20, IDC_REDIRECT);
        ui_make(hwnd, L"BUTTON", L"首次运行时，把正在用的旧存档复制过来（只复制这个游戏的）",
                BS_AUTOCHECKBOX | WS_TABSTOP, 16, 172, WIZ_W - 32, 20, IDC_MIGRATE);
        ui_make(hwnd, L"STATIC", L"", SS_LEFT, 16, 196, WIZ_W - 32, 100, IDC_INFO);
        ui_make(hwnd, L"BUTTON", L"开始游戏", BS_DEFPUSHBUTTON | WS_TABSTOP,
                WIZ_W - 240, WIZ_H - 40, 110, 28, IDOK);
        ui_make(hwnd, L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
                WIZ_W - 124, WIZ_H - 40, 110, 28, IDCANCEL);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_EXE:
            if (HIWORD(wp) == CBN_SELCHANGE) ui_exe_changed(hwnd);
            return 0;
        case IDC_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE) { g_wiz->lang_auto = 0; ui_info(hwnd); }
            return 0;
        case IDC_SHELL:
        case IDC_REDIRECT:
        case IDC_MIGRATE:
            ui_info(hwnd);
            return 0;
        case IDOK: {
            int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_EXE), CB_GETCURSEL, 0, 0);
            if (g_wiz->n <= 0 || sel < 0) {
                MessageBoxW(hwnd,
                    L"没在这个文件夹里找到游戏主程序。\n\n"
                    L"请把 FVPLauncher.exe、fvpl_hook.dll 和游戏主程序放在同一个文件夹里，"
                    L"然后再运行一次。",
                    L"FVP 启动器", MB_OK | MB_ICONWARNING);
                return 0;
            }
            ui_apply(hwnd);
            if (!WizardWriteIni(g_wiz->basedir, g_wiz->cfg, g_wiz->lang_idx)) {
                MessageBoxW(hwnd,
                    L"配置没能写进 fvp-launcher.ini（可能文件夹不可写）。\n\n"
                    L"这次可以用推断出来的设置继续跑，但下次还要再确认一次。",
                    L"FVP 启动器", MB_OK | MB_ICONWARNING);
            }
            g_wiz->ok = 1;
            DestroyWindow(hwnd);
            return 0;
        }
        case IDCANCEL:
            g_wiz->ok = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        g_wiz->ok = 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WizardRun(const fvpl_wchar *basedir, const WizCand *cands, int n,
              const fvpl_wchar *preselect, FvplConfig *cfg) {
    WizCtx ctx;
    WNDCLASSEXW wc;
    MSG msg;
    RECT r;
    HWND hwnd;
    int x, y, ww, wh;

    RtlZeroMemory(&ctx, sizeof(ctx));
    ctx.inst = GetModuleHandleW(NULL);
    ctx.basedir = basedir;
    ctx.cands = cands;
    ctx.n = (n > 0) ? (n > WIZ_MAX_CAND ? WIZ_MAX_CAND : n) : 0;
    ctx.host_cp = (int)cfg->codepage;
    ctx.host_cs = (int)cfg->font_charset;
    ctx.cfg = cfg;
    g_wiz = &ctx;

    RtlZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WizProc;
    wc.hInstance = ctx.inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WIZ_CLASS;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&wc)) { g_wiz = NULL; return 0; }

    r.left = 0; r.top = 0; r.right = WIZ_W; r.bottom = WIZ_H;
    AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
    ww = r.right - r.left;
    wh = r.bottom - r.top;
    x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;

    hwnd = CreateWindowExW(0, WIZ_CLASS, L"FVP 启动器 — 首次设定",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                           x, y, ww, wh, NULL, NULL, ctx.inst, NULL);
    if (!hwnd) { g_wiz = NULL; return 0; }

    ui_fill(hwnd, preselect);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    g_wiz = NULL;
    return ctx.ok;
}

int WizardWriteIni(const fvpl_wchar *basedir, const FvplConfig *cfg, int lang_idx) {
    fvpl_wchar path[FVPL_MAX_PATH], text[4096], tcv[16], fcv[16];
    static char utf8[8192];
    char bom[3];
    DWORD wrote;
    HANDLE h;
    int n, i;

    (void)lang_idx;   /* 文本码页直接从 cfg->text_codepage 取, 不再靠界面索引 */

    wiz_fmt(tcv, 16, L"%u", cfg->text_codepage);
    wiz_fmt(fcv, 16, L"%u", cfg->font_charset);
    tcv[15] = 0; fcv[15] = 0;

    n = wiz_fmt(text, 4096,
        L"; FVP 启动器配置（首次设定自动生成；用记事本可以直接改）\r\n"
        L"; 删掉这个文件，下次运行会重新弹出首次设定。\r\n"
        L"; [nls] codepage 管“系统与路径”，用的就是本机码页，一般不要动；\r\n"
        L"; [nls] text_codepage 管“游戏文本”（台词/标题）的码页，0 = 不做文本转换。\r\n"
        L"\r\n"
        L"[launcher]\r\n"
        L"; 游戏主程序（相对本文件所在目录，也可以写完整路径）\r\n"
        L"exe = %s\r\n"
        L"; 游戏由别的汉化启动器再启动一次时打开（汉化版常见）\r\n"
        L"inject_children = %d\r\n"
        L"\r\n"
        L"[save]\r\n"
        L"; 1 = 存档存到游戏目录下的 userdata，不动系统“我的文档”里的存档\r\n"
        L"redirect = %d\r\n"
        L"path = userdata\r\n"
        L"; 1 = 第一次用到某个存档时，从“我的文档”把它复制过来（只复制，不删除）\r\n"
        L"migrate = %d\r\n"
        L"migrate_names = %s\r\n"
        L"\r\n"
        L"[nls]\r\n"
        L"; 系统/路径层，auto = 本机码页，不要乱填\r\n"
        L"codepage = auto\r\n"
        L"; 游戏台词与标题的码页；0 = 不转换\r\n"
        L"text_codepage = %s\r\n"
        L"font_charset = %s\r\n"
        L"virtualize_files = 1\r\n"
        L"virtualize_ui = 0\r\n"
        L"lcid = 0\r\n",
        cfg->target, cfg->inject_children, cfg->save_redirect,
        cfg->save_migrate, cfg->migrate_names[0] ? cfg->migrate_names : L"FAVORITE",
        tcv, fcv);
    if (n <= 0 || n >= 4096) return 0;
    text[4095] = 0;

    if (!pv_join(path, FVPL_MAX_PATH, basedir, FVPL_INI_NAME)) return 0;
    n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (n <= 0) return 0;

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    bom[0] = (char)0xEF; bom[1] = (char)0xBB; bom[2] = (char)0xBF;
    for (i = 0; i < 3; i += (int)wrote) {
        if (!WriteFile(h, bom + i, (DWORD)(3 - i), &wrote, NULL) || wrote == 0) {
            CloseHandle(h);
            return 0;
        }
    }
    if (!WriteFile(h, utf8, (DWORD)(n - 1), &wrote, NULL) || wrote != (DWORD)(n - 1)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return 1;
}
