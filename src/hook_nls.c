/* hook_nls.c — 虚拟代码页: 让引擎的 ANSI 字节串按配置的解释, 与系统真实 ACP 无关
 *
 * 分层依据 (全部来自 Sakura.exe 实测导入表):
 *   - 引擎自带 GetACP / GetCPInfo / IsValidCodePage / MultiByteToWideChar /
 *     WideCharToMultiByte / LCMapStringA / GetStringTypeA / GetLocaleInfoA,
 *     说明它把 "ACP" 当唯一真值用; 换掉这几个就等于换掉它的世界观。
 *   - 文本渲染走 GetGlyphOutlineA + CreateFontA (字形由引擎自己描进 D3D 纹理),
 *     所以这两个是"能不能看见正确汉字"的关键。
 *   - 存档标题走 LoadStringA: 资源里的宽字符串被按 ACP 降级, 换页会换目录名。
 * 刻意不碰的东西: RegisterClassExA / DispatchMessageA / DefWindowProcA。
 *   把窗口转成 W 形态会让系统给引擎的 A 版窗口过程发 W 版消息,
 *   轻则 WM_GETTEXT 缓冲区语义错位。标题乱码属于外观问题, 用 SetWindowTextW 补。 */
#include "hooks.h"
#include "pathutil.h"
#include <stdint.h>

typedef uint32_t (WINAPI *PFN_GetACP)(void);
typedef BOOL (WINAPI *PFN_GetCPInfo)(UINT, LPCPINFO);
typedef BOOL (WINAPI *PFN_IsDBCSLeadByteEx)(UINT, BYTE);
typedef BOOL (WINAPI *PFN_IsDBCSLeadByte)(BYTE);
typedef int (WINAPI *PFN_MB2W)(UINT, DWORD, LPCSTR, int, LPWSTR, int);
typedef int (WINAPI *PFN_W2MB)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL);
typedef int (WINAPI *PFN_GetLocaleInfoA)(LCID, LCTYPE, LPSTR, int);
typedef int (WINAPI *PFN_LCMapStringA)(LCID, DWORD, LPCSTR, int, LPSTR, int);
typedef BOOL (WINAPI *PFN_GetStringTypeA)(LCID, DWORD, LPCSTR, int, LPWORD, int);
typedef int (WINAPI *PFN_LoadStringA)(HMODULE, UINT, LPSTR, int);
typedef int (WINAPI *PFN_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);
typedef HWND (WINAPI *PFN_CreateWindowExA)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int,
                                           HWND, HMENU, HINSTANCE, LPVOID);
typedef HFONT (WINAPI *PFN_CreateFontA)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                                        DWORD, DWORD, DWORD, DWORD, LPCSTR);
typedef DWORD (WINAPI *PFN_GetGlyphOutlineA)(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID,
                                             const MAT2 *);
typedef int (WINAPI *PFN_lstrcmpiA)(LPCSTR, LPCSTR);
typedef uint32_t (WINAPI *PFN_GetOEMCP)(void);
typedef BOOL (WINAPI *PFN_IsValidCodePage)(UINT);

static PFN_GetACP            o_GetACP;
static PFN_GetCPInfo         o_GetCPInfo;
static PFN_GetOEMCP          o_GetOEMCP;
static PFN_IsValidCodePage   o_IsValidCodePage;
static PFN_IsDBCSLeadByteEx  o_IsDBCSLeadByteEx;
static PFN_IsDBCSLeadByte    o_IsDBCSLeadByte;
static PFN_MB2W              o_MultiByteToWideChar;
static PFN_W2MB              o_WideCharToMultiByte;
static PFN_GetLocaleInfoA    o_GetLocaleInfoA;
static PFN_LCMapStringA      o_LCMapStringA;
static PFN_GetStringTypeA    o_GetStringTypeA;
static PFN_LoadStringA       o_LoadStringA;
static PFN_MessageBoxA       o_MessageBoxA;
static PFN_CreateWindowExA   o_CreateWindowExA;
static PFN_CreateFontA       o_CreateFontA;
static PFN_GetGlyphOutlineA  o_GetGlyphOutlineA;
static PFN_lstrcmpiA         o_lstrcmpiA;

static int nls_on(void)  { return FvplReady(); }
static int ui_on(void)   { return FvplReady() && g_cfg.virtualize_ui; }
static int ui_text(WCHAR *dst, int cch, LPCSTR src);

static LCID MapLCID(LCID lcid) {
    if (!g_cfg.lcid) return lcid;
    if (lcid == LOCALE_USER_DEFAULT || lcid == LOCALE_SYSTEM_DEFAULT ||
        lcid == LOCALE_NEUTRAL || lcid == 0) return (LCID)g_cfg.lcid;
    return lcid;
}

static uint32_t WINAPI Hook_GetACP(void) {
    if (!nls_on()) return o_GetACP ? o_GetACP() : 0;
    return (uint32_t)g_cfg.codepage;
}

static BOOL WINAPI Hook_GetCPInfo(UINT cp, LPCPINFO info) {
    if (!nls_on()) return o_GetCPInfo ? o_GetCPInfo(cp, info) : FALSE;
    return o_GetCPInfo(FvplMapCP(cp), info);
}

/* Sakura.exe 确实导入这两个, 漏掉会让引擎在"我是不是日文机"这一步穿帮 */
static uint32_t WINAPI Hook_GetOEMCP(void) {
    if (!nls_on()) return o_GetOEMCP ? o_GetOEMCP() : 0;
    return (uint32_t)g_cfg.codepage;
}

static BOOL WINAPI Hook_IsValidCodePage(UINT cp) {
    if (!nls_on() || o_IsValidCodePage == NULL)
        return o_IsValidCodePage ? o_IsValidCodePage(cp) : FALSE;
    if (cp == (UINT)g_cfg.codepage) return TRUE;
    return o_IsValidCodePage(cp);
}

static BOOL WINAPI Hook_IsDBCSLeadByteEx(UINT cp, BYTE b) {
    if (!nls_on() || o_IsDBCSLeadByteEx == NULL)
        return o_IsDBCSLeadByteEx ? o_IsDBCSLeadByteEx(cp, b) : FALSE;
    return o_IsDBCSLeadByteEx(FvplMapCP(cp), b);
}

static BOOL WINAPI Hook_IsDBCSLeadByte(BYTE b) {
    if (!nls_on() || o_IsDBCSLeadByteEx == NULL)
        return o_IsDBCSLeadByte ? o_IsDBCSLeadByte(b) : FALSE;
    return o_IsDBCSLeadByteEx((UINT)g_cfg.codepage, b);
}

static int WINAPI Hook_MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int cbsrc,
                                           LPWSTR dst, int ccdst) {
    if (!nls_on()) return o_MultiByteToWideChar ? o_MultiByteToWideChar(cp, flags, src, cbsrc, dst, ccdst) : 0;
    return o_MultiByteToWideChar(FvplMapCP(cp), flags, src, cbsrc, dst, ccdst);
}

static int WINAPI Hook_WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int cbsrc,
                                           LPSTR dst, int ccdst, LPCSTR def, LPBOOL used) {
    if (!nls_on()) return o_WideCharToMultiByte ? o_WideCharToMultiByte(cp, flags, src, cbsrc, dst, ccdst, def, used) : 0;
    return o_WideCharToMultiByte(FvplMapCP(cp), flags, src, cbsrc, dst, ccdst, def, used);
}

static int WINAPI Hook_GetLocaleInfoA(LCID lcid, LCTYPE type, LPSTR buf, int cch) {
    if (!nls_on()) return o_GetLocaleInfoA ? o_GetLocaleInfoA(lcid, type, buf, cch) : 0;
    return o_GetLocaleInfoA(MapLCID(lcid), type, buf, cch);
}

static int WINAPI Hook_LCMapStringA(LCID lcid, DWORD flags, LPCSTR src, int cbsrc,
                                    LPSTR dst, int ccdst) {
    if (!nls_on()) return o_LCMapStringA ? o_LCMapStringA(lcid, flags, src, cbsrc, dst, ccdst) : 0;
    return o_LCMapStringA(MapLCID(lcid), flags, src, cbsrc, dst, ccdst);
}

static BOOL WINAPI Hook_GetStringTypeA(LCID lcid, DWORD type, LPCSTR src, int cbsrc,
                                        LPWORD classes, int cdst) {
    if (!nls_on()) return o_GetStringTypeA ? o_GetStringTypeA(lcid, type, src, cbsrc, classes, cdst) : FALSE;
    return o_GetStringTypeA(MapLCID(lcid), type, src, cbsrc, classes, cdst);
}

static int WINAPI Hook_lstrcmpiA(LPCSTR a, LPCSTR b) {
    int r;
    if (!nls_on() || !g_cfg.lcid || o_lstrcmpiA == NULL)
        return o_lstrcmpiA ? o_lstrcmpiA(a, b) : 0;
    r = CompareStringA((LCID)g_cfg.lcid, NORM_IGNORECASE, a, -1, b, -1);
    return r - 2;
}

/* 资源串: Wide 资源 -> 虚拟代码页字节。换 codepage 时存档目录名会跟着变,
 * 这正是把存档根固定下来的原因。 */
static int WINAPI Hook_LoadStringA(HMODULE mod, UINT id, LPSTR buf, int cch) {
    WCHAR w[512];
    char  a[1024];
    int n, m;
    if (!nls_on() || buf == NULL || cch <= 0 || o_LoadStringA == NULL)
        return o_LoadStringA ? o_LoadStringA(mod, id, buf, cch) : 0;
    n = LoadStringW(mod ? mod : GetModuleHandleW(NULL), id, w, 511);
    if (n <= 0) return o_LoadStringA(mod, id, buf, cch);
    m = WideCharToMultiByte((UINT)g_cfg.codepage, 0, w, n, a, (int)sizeof(a) - 1, NULL, NULL);
    if (m <= 0) return o_LoadStringA(mod, id, buf, cch);
    if (m > cch - 1) m = cch - 1;
    memcpy(buf, a, (size_t)m);
    buf[m] = 0;
    return m;
}

static int WINAPI Hook_MessageBoxA(HWND hw, LPCSTR text, LPCSTR caption, UINT type) {
    WCHAR wt[2048], wc[512];
    if (!ui_on() || o_MessageBoxA == NULL || !ui_text(wt, 2048, text) ||
        !ui_text(wc, 512, caption))
        return o_MessageBoxA ? o_MessageBoxA(hw, text, caption, type) : 0;
    return MessageBoxW(hw, wt, wc, type);
}

static HWND WINAPI Hook_CreateWindowExA(DWORD exstyle, LPCSTR cls, LPCSTR title, DWORD style,
                                        int x, int y, int w, int h, HWND parent, HMENU menu,
                                        HINSTANCE inst, LPVOID lparam) {
    HWND hw;
    WCHAR wt[512];
    hw = o_CreateWindowExA ? o_CreateWindowExA(exstyle, cls, title, style, x, y, w, h,
                                               parent, menu, inst, lparam) : NULL;
    if (!ui_on() || hw == NULL || title == NULL || !title[0]) return hw;
    if (FvplA2W(title, wt, 512) > 0) SetWindowTextW(hw, wt);
    return hw;
}

static HFONT WINAPI Hook_CreateFontA(int height, int width, int escapement, int orientation,
                                     int weight, DWORD italic, DWORD underline, DWORD strikeout,
                                     DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                                     DWORD pitchfam, LPCSTR face) {
    WCHAR wf[256];
    if (!ui_on() || o_CreateFontA == NULL)
        return o_CreateFontA ? o_CreateFontA(height, width, escapement, orientation, weight,
                                             italic, underline, strikeout, charset, outprec,
                                             clipprec, quality, pitchfam, face) : NULL;
    if (face == NULL || !face[0])
        return o_CreateFontA(height, width, escapement, orientation, weight, italic, underline,
                             strikeout, charset, outprec, clipprec, quality, pitchfam, face);
    if (FvplA2W(face, wf, 256) <= 0)
        return o_CreateFontA(height, width, escapement, orientation, weight, italic, underline,
                             strikeout, charset, outprec, clipprec, quality, pitchfam, face);
    return CreateFontW(height, width, escapement, orientation, weight, (BYTE)italic,
                       (BYTE)underline, (BYTE)strikeout, (BYTE)g_cfg.font_charset,
                       outprec, clipprec, quality, pitchfam, wf);
}

/* 引擎按字节取字形: 把 uChar 里的 (前字节, 后字节) 组回虚拟代码页再查宽字符字形 */
static DWORD WINAPI Hook_GetGlyphOutlineA(HDC dc, UINT uchar, UINT format, LPGLYPHMETRICS gm,
                                          DWORD cbbuf, LPVOID buf, const MAT2 *m2) {
    char mb[2];
    int len = 1;
    UINT cp = (UINT)g_cfg.codepage;
    WCHAR wch = 0;
    UINT u;
    if (!ui_on() || o_GetGlyphOutlineA == NULL)
        return o_GetGlyphOutlineA ? o_GetGlyphOutlineA(dc, uchar, format, gm, cbbuf, buf, m2) : GDI_ERROR;
    if (o_IsDBCSLeadByteEx != NULL && o_IsDBCSLeadByteEx(cp, (BYTE)(uchar >> 8))) {
        mb[0] = (char)((uchar >> 8) & 0xFF);
        mb[1] = (char)(uchar & 0xFF);
        len = 2;
    } else {
        mb[0] = (char)(uchar & 0xFF);
    }
    if (MultiByteToWideChar(cp, 0, mb, len, &wch, 1) <= 0) return GDI_ERROR;
    u = (UINT)wch;
    return GetGlyphOutlineW(dc, u, format, gm, cbbuf, buf, m2);
}

static int ui_text(WCHAR *dst, int cch, LPCSTR src) {
    if (src == NULL) { dst[0] = 0; return 1; }
    return FvplA2W(src, dst, cch) > 0;
}

static const FvplPatch s_tbl[] = {
    { "kernel32.dll", "GetACP",               (void *)Hook_GetACP,              (void **)&o_GetACP },
    { "kernel32.dll", "GetCPInfo",            (void *)Hook_GetCPInfo,           (void **)&o_GetCPInfo },
    { "kernel32.dll", "GetOEMCP",             (void *)Hook_GetOEMCP,            (void **)&o_GetOEMCP },
    { "kernel32.dll", "IsValidCodePage",      (void *)Hook_IsValidCodePage,     (void **)&o_IsValidCodePage },
    { "kernel32.dll", "IsDBCSLeadByte",       (void *)Hook_IsDBCSLeadByte,      (void **)&o_IsDBCSLeadByte },
    { "kernel32.dll", "IsDBCSLeadByteEx",     (void *)Hook_IsDBCSLeadByteEx,    (void **)&o_IsDBCSLeadByteEx },
    { "kernel32.dll", "MultiByteToWideChar",  (void *)Hook_MultiByteToWideChar, (void **)&o_MultiByteToWideChar },
    { "kernel32.dll", "WideCharToMultiByte",  (void *)Hook_WideCharToMultiByte, (void **)&o_WideCharToMultiByte },
    { "kernel32.dll", "GetLocaleInfoA",       (void *)Hook_GetLocaleInfoA,      (void **)&o_GetLocaleInfoA },
    { "kernel32.dll", "LCMapStringA",         (void *)Hook_LCMapStringA,        (void **)&o_LCMapStringA },
    { "kernel32.dll", "GetStringTypeA",       (void *)Hook_GetStringTypeA,      (void **)&o_GetStringTypeA },
    { "kernel32.dll", "lstrcmpiA",            (void *)Hook_lstrcmpiA,           (void **)&o_lstrcmpiA },
    { "user32.dll",   "LoadStringA",          (void *)Hook_LoadStringA,         (void **)&o_LoadStringA },
    { "user32.dll",   "MessageBoxA",          (void *)Hook_MessageBoxA,         (void **)&o_MessageBoxA },
    { "user32.dll",   "CreateWindowExA",      (void *)Hook_CreateWindowExA,     (void **)&o_CreateWindowExA },
    { "gdi32.dll",    "CreateFontA",          (void *)Hook_CreateFontA,         (void **)&o_CreateFontA },
    { "gdi32.dll",    "GetGlyphOutlineA",     (void *)Hook_GetGlyphOutlineA,    (void **)&o_GetGlyphOutlineA },
};

const FvplPatch *FvplTableNls(int *count) {
    *count = (int)(sizeof(s_tbl) / sizeof(s_tbl[0]));
    return s_tbl;
}
