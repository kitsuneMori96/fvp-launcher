/* hook_nls.c — 虚拟代码页: 让引擎的 ANSI 字节串按配置的解释, 与系统真实 ACP 无关
 *
 * 分层依据 (全部来自 Sakura.exe 实测导入表):
 *   - 引擎自带 GetACP / GetCPInfo / IsValidCodePage / MultiByteToWideChar /
 *     WideCharToMultiByte / LCMapStringA / GetStringTypeA / GetLocaleInfoA,
 *     说明它把 "ACP" 当唯一真值用; 换掉这几个就等于换掉它的世界观。
 *   - 文本渲染走 GetGlyphOutlineA + CreateFont* (字形由引擎自己描进 D3D 纹理)。
 *     字体这一摊由 [nls] font_fix 单独开关: 汉化版/中文游戏里"同一行字粗细明暗
 *     不一"不是缺字, 是引擎按日文语境要字体, 中文只能靠 GDI 逐字回退到别的字库。
 *     font_fix=1 时创建字体统一 lfCharSet, 字体枚举也把报给引擎的字符集/脚本名
 *     改成目标语言; virtualize_ui 只管界面文字 (MessageBox/标题/LoadString)。
 *   - 存档标题走 LoadStringA: 资源里的宽字符串被按 ACP 降级, 换页会换目录名。
 * 刻意不碰的东西: RegisterClassExA / DispatchMessageA / DefWindowProcA。
 *   把窗口转成 W 形态会让系统给引擎的 A 版窗口过程发 W 版消息,
 *   轻则 WM_GETTEXT 缓冲区语义错位。标题乱码只针对引擎实际走的
 *   SendMessageA(WM_SETTEXT) 单点纠正 (它不导入 SetWindowTextA), 不动消息回路。 */
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
typedef HFONT (WINAPI *PFN_CreateFontW)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD,
                                        DWORD, DWORD, DWORD, DWORD, LPCWSTR);
typedef HFONT (WINAPI *PFN_CreateFontIndirectA)(const LOGFONTA *);
typedef HFONT (WINAPI *PFN_CreateFontIndirectW)(const LOGFONTW *);
typedef int (WINAPI *PFN_EnumFontFamiliesExA)(HDC, LPLOGFONTA, FONTENUMPROCA, LPARAM, DWORD);
typedef int (WINAPI *PFN_EnumFontFamiliesExW)(HDC, LPLOGFONTW, FONTENUMPROCW, LPARAM, DWORD);
typedef int (WINAPI *PFN_EnumFontFamiliesA)(HDC, LPCSTR, FONTENUMPROCA, LPARAM);
typedef int (WINAPI *PFN_EnumFontFamiliesW)(HDC, LPCWSTR, FONTENUMPROCW, LPARAM);
typedef DWORD (WINAPI *PFN_GetGlyphOutlineA)(HDC, UINT, UINT, LPGLYPHMETRICS, DWORD, LPVOID,
                                             const MAT2 *);
typedef int (WINAPI *PFN_lstrcmpiA)(LPCSTR, LPCSTR);
typedef uint32_t (WINAPI *PFN_GetOEMCP)(void);
typedef BOOL (WINAPI *PFN_IsValidCodePage)(UINT);
typedef LRESULT (WINAPI *PFN_SendMessageA)(HWND, UINT, WPARAM, LPARAM);

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
static PFN_SendMessageA      o_SendMessageA;
static PFN_CreateFontA       o_CreateFontA;
static PFN_GetGlyphOutlineA  o_GetGlyphOutlineA;
static PFN_CreateFontW       o_CreateFontW;
static PFN_CreateFontIndirectA o_CreateFontIndirectA;
static PFN_CreateFontIndirectW o_CreateFontIndirectW;
static PFN_EnumFontFamiliesExA o_EnumFontFamiliesExA;
static PFN_EnumFontFamiliesExW o_EnumFontFamiliesExW;
static PFN_EnumFontFamiliesA  o_EnumFontFamiliesA;
static PFN_EnumFontFamiliesW  o_EnumFontFamiliesW;
static PFN_lstrcmpiA         o_lstrcmpiA;

static int nls_on(void)  { return FvplReady(); }
/* 文本层是否启用: 只要配了 text_codepage 就开 (与系统码页相同的显式值也算"要修正字体") */
static int text_on(void) { return FvplReady() && g_cfg.text_codepage != 0; }
static int ui_on(void)   { return text_on(); }
static int font_on(void) { return text_on(); }
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
    m = WideCharToMultiByte(FvplTextCp(), 0, w, n, a, (int)sizeof(a) - 1, NULL, NULL);
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
    if (FvplTextA2W(title, wt, 512) > 0) SetWindowTextW(hw, wt);
    return hw;
}

/* 标题栏: Sakura.exe 不导入 SetWindowTextA, 主窗口标题走 SendMessageA(WM_SETTEXT)。
 * 系统按真实 ACP(936) 把 SJIS 串转成 W 就成乱码, 且这一步会覆盖建窗时的纠正。
 * 这里自己按虚拟代码页解码后转投 SendMessageW。只动 WM_SETTEXT, 其余原样透传。 */
static LRESULT WINAPI Hook_SendMessageA(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (!ui_on() || o_SendMessageA == NULL)
        return o_SendMessageA ? o_SendMessageA(hw, msg, wp, lp) : 0;
    if (msg == WM_SETTEXT && lp != 0) {
        WCHAR wt[1024];
        if (FvplTextA2W((LPCSTR)lp, wt, (int)(sizeof(wt) / sizeof(wt[0]))) > 0)
            return SendMessageW(hw, WM_SETTEXT, wp, (LPARAM)wt);
    }
    return o_SendMessageA(hw, msg, wp, lp);
}

/* ---------- 字体修正 (font_fix) -------------------------------------------
 * 症状: 汉化/中文游戏里同一行字粗细明暗不一。根因不是缺字, 是引擎按日文语境要
 * 字体, 中文文本只能靠 GDI 逐字回退到别的字库, 每个回退字库的字重/描边都不同。
 * 对策 (与 FVPLoader 同源): 创建字体时把 lfCharSet 统一成配置值, 字体枚举时把
 * 报给引擎的字符集与脚本名也改成目标语言, 让引擎自己挑字体时看到的世界自洽。
 * 只在引擎没指定字符集 (ANSI/DEFAULT) 或它给的正好是本机字符集时改 —— 引擎明确
 * 要别的字符集时不替它做决定。font_face 非空时连字型一起换。 */

static int s_host_charset;

static BYTE host_text_charset(void) {
    HDC dc;
    int cs;
    if (s_host_charset > 0) return (BYTE)s_host_charset;
    dc = GetDC(NULL);
    cs = dc ? GetTextCharset(dc) : (int)DEFAULT_CHARSET;
    if (dc) ReleaseDC(NULL, dc);
    if (cs <= 0) cs = DEFAULT_CHARSET;
    s_host_charset = cs;
    return (BYTE)cs;
}

static const WCHAR *font_script_name(void) {
    switch ((int)FvplTextCp()) {
    case 932:  return L"日本語";
    case 936:  return L"简体中文";
    case 950:  return L"繁體中文";
    case 949:  return L"한국어";
    case 1252: return L"Latin";
    default:   return NULL;
    }
}

static int font_fix_face(BYTE face_cs, DWORD req_cs) {
    if (!font_on()) return 0;
    if ((BYTE)req_cs != DEFAULT_CHARSET) return 0;
    if (face_cs == ANSI_CHARSET || face_cs == DEFAULT_CHARSET || face_cs == host_text_charset())
        return 1;
    return 0;
}

static void font_fix_logfont(LOGFONTW *lf) {
    if (lf == NULL) return;
    lf->lfCharSet = (BYTE)g_cfg.font_charset;
    if (g_cfg.font_face[0]) lstrcpynW(lf->lfFaceName, g_cfg.font_face, LF_FACESIZE);
}

static void logfont_a2w(const LOGFONTA *a, LOGFONTW *w) {
    int i;
    for (i = 0; i < (int)sizeof(*w); i++) ((unsigned char *)w)[i] = 0;
    w->lfHeight = a->lfHeight;
    w->lfWidth = a->lfWidth;
    w->lfEscapement = a->lfEscapement;
    w->lfOrientation = a->lfOrientation;
    w->lfWeight = a->lfWeight;
    w->lfItalic = a->lfItalic;
    w->lfUnderline = a->lfUnderline;
    w->lfStrikeOut = a->lfStrikeOut;
    w->lfCharSet = a->lfCharSet;
    w->lfOutPrecision = a->lfOutPrecision;
    w->lfClipPrecision = a->lfClipPrecision;
    w->lfQuality = a->lfQuality;
    w->lfPitchAndFamily = a->lfPitchAndFamily;
    FvplTextA2W(a->lfFaceName, w->lfFaceName, LF_FACESIZE);
}

static HFONT WINAPI Hook_CreateFontA(int height, int width, int escapement, int orientation,
                                     int weight, DWORD italic, DWORD underline, DWORD strikeout,
                                     DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                                     DWORD pitchfam, LPCSTR face) {
    WCHAR wf[LF_FACESIZE];
    if (!font_on() || o_CreateFontA == NULL)
        return o_CreateFontA ? o_CreateFontA(height, width, escapement, orientation, weight,
                                             italic, underline, strikeout, charset, outprec,
                                             clipprec, quality, pitchfam, face) : NULL;
    if (g_cfg.font_face[0]) {
        lstrcpynW(wf, g_cfg.font_face, LF_FACESIZE);
    } else if (face != NULL && face[0]) {
        if (FvplTextA2W(face, wf, LF_FACESIZE) <= 0)
            return o_CreateFontA(height, width, escapement, orientation, weight, italic, underline,
                                 strikeout, charset, outprec, clipprec, quality, pitchfam, face);
    } else {
        wf[0] = 0;
    }
    return CreateFontW(height, width, escapement, orientation, weight, (BYTE)italic,
                       (BYTE)underline, (BYTE)strikeout, (BYTE)g_cfg.font_charset,
                       outprec, clipprec, quality, pitchfam, wf);
}

static HFONT WINAPI Hook_CreateFontW(int height, int width, int escapement, int orientation,
                                     int weight, DWORD italic, DWORD underline, DWORD strikeout,
                                     DWORD charset, DWORD outprec, DWORD clipprec, DWORD quality,
                                     DWORD pitchfam, LPCWSTR face) {
    if (!font_on() || o_CreateFontW == NULL)
        return o_CreateFontW ? o_CreateFontW(height, width, escapement, orientation, weight,
                                             italic, underline, strikeout, charset, outprec,
                                             clipprec, quality, pitchfam, face) : NULL;
    return o_CreateFontW(height, width, escapement, orientation, weight, italic, underline,
                         strikeout, (DWORD)g_cfg.font_charset, outprec, clipprec, quality,
                         pitchfam, g_cfg.font_face[0] ? g_cfg.font_face : face);
}

static HFONT WINAPI Hook_CreateFontIndirectW(const LOGFONTW *lf) {
    LOGFONTW c;
    if (!font_on() || o_CreateFontIndirectW == NULL)
        return o_CreateFontIndirectW ? o_CreateFontIndirectW(lf) : NULL;
    if (lf == NULL) return o_CreateFontIndirectW(lf);
    c = *lf;
    font_fix_logfont(&c);
    return o_CreateFontIndirectW(&c);
}

static HFONT WINAPI Hook_CreateFontIndirectA(const LOGFONTA *lf) {
    LOGFONTW w;
    if (!font_on() || o_CreateFontIndirectA == NULL)
        return o_CreateFontIndirectA ? o_CreateFontIndirectA(lf) : NULL;
    if (lf == NULL) return o_CreateFontIndirectA(lf);
    logfont_a2w(lf, &w);
    font_fix_logfont(&w);
    return CreateFontIndirectW(&w);
}

/* --- 字体枚举: 把报给引擎的字符集/脚本名改成目标语言 --- */

typedef struct {
    FONTENUMPROCW procw;
    FONTENUMPROCA proca;
    LPARAM lparam;
    DWORD  req_charset;
    int    ext;            /* 1 = Ex 版枚举, elfScript 可以安全改写 */
} FONT_ENUM_CTX;

static int CALLBACK enum_cb_w(const ENUMLOGFONTEXW *lf, const NEWTEXTMETRICEXW *tm, DWORD type,
                              LPARAM lp) {
    FONT_ENUM_CTX *ctx = (FONT_ENUM_CTX *)lp;
    ENUMLOGFONTEXW e;
    if (ctx->procw == NULL) return 0;
    if (lf == NULL) return ctx->procw(NULL, (const TEXTMETRICW *)tm, type, ctx->lparam);
    e = *lf;
    if (font_fix_face(e.elfLogFont.lfCharSet, ctx->req_charset)) {
        const WCHAR *sc = font_script_name();
        e.elfLogFont.lfCharSet = (BYTE)g_cfg.font_charset;
        if (g_cfg.font_face[0]) lstrcpynW(e.elfLogFont.lfFaceName, g_cfg.font_face, LF_FACESIZE);
        if (ctx->ext && sc != NULL && sc[0]) lstrcpynW(e.elfScript, sc, LF_FACESIZE);
    }
    return ctx->procw((const LOGFONTW *)&e, (const TEXTMETRICW *)tm, type, ctx->lparam);
}

static int CALLBACK enum_cb_a(const ENUMLOGFONTEXA *lf, const NEWTEXTMETRICEXA *tm, DWORD type,
                              LPARAM lp) {
    FONT_ENUM_CTX *ctx = (FONT_ENUM_CTX *)lp;
    ENUMLOGFONTEXA e;
    if (ctx->proca == NULL) return 0;
    if (lf == NULL) return ctx->proca(NULL, (const TEXTMETRICA *)tm, type, ctx->lparam);
    e = *lf;
    if (font_fix_face(e.elfLogFont.lfCharSet, ctx->req_charset)) {
        const WCHAR *sc = font_script_name();
        e.elfLogFont.lfCharSet = (BYTE)g_cfg.font_charset;
        if (g_cfg.font_face[0]) FvplTextW2A(g_cfg.font_face, e.elfLogFont.lfFaceName, LF_FACESIZE);
        if (ctx->ext && sc != NULL && sc[0]) FvplTextW2A(sc, (char *)e.elfScript, LF_FACESIZE);
    }
    return ctx->proca((const LOGFONTA *)&e, (const TEXTMETRICA *)tm, type, ctx->lparam);
}

static int WINAPI Hook_EnumFontFamiliesExW(HDC dc, LPLOGFONTW lf, FONTENUMPROCW proc, LPARAM lp,
                                           DWORD flags) {
    FONT_ENUM_CTX ctx;
    if (o_EnumFontFamiliesExW == NULL || proc == NULL) return 0;
    if (!font_on()) return o_EnumFontFamiliesExW(dc, lf, proc, lp, flags);
    ctx.procw = proc;
    ctx.proca = NULL;
    ctx.lparam = lp;
    ctx.ext = 1;
    ctx.req_charset = lf ? (DWORD)lf->lfCharSet : (DWORD)DEFAULT_CHARSET;
    return o_EnumFontFamiliesExW(dc, lf, (FONTENUMPROCW)enum_cb_w, (LPARAM)&ctx, flags);
}

static int WINAPI Hook_EnumFontFamiliesExA(HDC dc, LPLOGFONTA lf, FONTENUMPROCA proc, LPARAM lp,
                                           DWORD flags) {
    FONT_ENUM_CTX ctx;
    if (o_EnumFontFamiliesExA == NULL || proc == NULL) return 0;
    if (!font_on()) return o_EnumFontFamiliesExA(dc, lf, proc, lp, flags);
    ctx.procw = NULL;
    ctx.proca = proc;
    ctx.lparam = lp;
    ctx.ext = 1;
    ctx.req_charset = lf ? (DWORD)lf->lfCharSet : (DWORD)DEFAULT_CHARSET;
    return o_EnumFontFamiliesExA(dc, lf, (FONTENUMPROCA)enum_cb_a, (LPARAM)&ctx, flags);
}

/* 老式 EnumFontFamilies*: 系统同样传扩展结构, 但回调契约只保证 LOGFONT, 所以只改
 * 字符集, 不动 elfScript。 */
static int WINAPI Hook_EnumFontFamiliesW(HDC dc, LPCWSTR face, FONTENUMPROCW proc, LPARAM lp) {
    FONT_ENUM_CTX ctx;
    if (o_EnumFontFamiliesW == NULL || proc == NULL) return 0;
    if (!font_on()) return o_EnumFontFamiliesW(dc, face, proc, lp);
    ctx.procw = proc;
    ctx.proca = NULL;
    ctx.lparam = lp;
    ctx.ext = 0;
    ctx.req_charset = (DWORD)DEFAULT_CHARSET;
    return o_EnumFontFamiliesW(dc, face, (FONTENUMPROCW)enum_cb_w, (LPARAM)&ctx);
}

static int WINAPI Hook_EnumFontFamiliesA(HDC dc, LPCSTR face, FONTENUMPROCA proc, LPARAM lp) {
    FONT_ENUM_CTX ctx;
    if (o_EnumFontFamiliesA == NULL || proc == NULL) return 0;
    if (!font_on()) return o_EnumFontFamiliesA(dc, face, proc, lp);
    ctx.procw = NULL;
    ctx.proca = proc;
    ctx.lparam = lp;
    ctx.ext = 0;
    ctx.req_charset = (DWORD)DEFAULT_CHARSET;
    return o_EnumFontFamiliesA(dc, face, (FONTENUMPROCA)enum_cb_a, (LPARAM)&ctx);
}

/* 引擎按字节取字形: 把 uChar 里的 (前字节, 后字节) 组回虚拟代码页再查宽字符字形 */
static DWORD WINAPI Hook_GetGlyphOutlineA(HDC dc, UINT uchar, UINT format, LPGLYPHMETRICS gm,
                                          DWORD cbbuf, LPVOID buf, const MAT2 *m2) {
    char mb[2];
    int len = 1;
    UINT cp = FvplTextCp();
    WCHAR wch = 0;
    UINT u;
    if (!font_on() || o_GetGlyphOutlineA == NULL)
        return o_GetGlyphOutlineA ? o_GetGlyphOutlineA(dc, uchar, format, gm, cbbuf, buf, m2) : GDI_ERROR;
    /* 直接调真实的 IsDBCSLeadByteEx: 引擎并不导入它, o_IsDBCSLeadByteEx 恒为 NULL,
     * 用它判前导码会把双字节假名当单字节, 转换失败 -> 画成 '?'。
     * fvpl_hook.dll 自己的 IAT 从不被改写 (sweep 只过 CreateProcess), 所以这里是真 kernel32。 */
    if (IsDBCSLeadByteEx(cp, (BYTE)(uchar >> 8))) {
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
    return FvplTextA2W(src, dst, cch) > 0;
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
    { "user32.dll",   "SendMessageA",         (void *)Hook_SendMessageA,        (void **)&o_SendMessageA },
    { "gdi32.dll",    "CreateFontA",          (void *)Hook_CreateFontA,         (void **)&o_CreateFontA },
    { "gdi32.dll",    "CreateFontW",          (void *)Hook_CreateFontW,         (void **)&o_CreateFontW },
    { "gdi32.dll",    "CreateFontIndirectA",  (void *)Hook_CreateFontIndirectA, (void **)&o_CreateFontIndirectA },
    { "gdi32.dll",    "CreateFontIndirectW",  (void *)Hook_CreateFontIndirectW, (void **)&o_CreateFontIndirectW },
    { "gdi32.dll",    "EnumFontFamiliesExA",  (void *)Hook_EnumFontFamiliesExA, (void **)&o_EnumFontFamiliesExA },
    { "gdi32.dll",    "EnumFontFamiliesExW",  (void *)Hook_EnumFontFamiliesExW, (void **)&o_EnumFontFamiliesExW },
    { "gdi32.dll",    "EnumFontFamiliesA",    (void *)Hook_EnumFontFamiliesA,   (void **)&o_EnumFontFamiliesA },
    { "gdi32.dll",    "EnumFontFamiliesW",    (void *)Hook_EnumFontFamiliesW,   (void **)&o_EnumFontFamiliesW },
    { "gdi32.dll",    "GetGlyphOutlineA",     (void *)Hook_GetGlyphOutlineA,    (void **)&o_GetGlyphOutlineA },
};

const FvplPatch *FvplTableNls(int *count) {
    *count = (int)(sizeof(s_tbl) / sizeof(s_tbl[0]));
    return s_tbl;
}
