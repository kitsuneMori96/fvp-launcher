/* config.c — 见 config.h */
#include "config.h"
#include "pathutil.h"

static int ci_is_fw(const fvpl_wchar c) { return c == L' ' || c == L'\t' || c == L'\r'; }

static int ci_eq(const fvpl_wchar *a, const fvpl_wchar *b) {
    while (*a && *b) {
        fvpl_wchar x = (*a >= L'A' && *a <= L'Z') ? *a + 32 : *a;
        fvpl_wchar y = (*b >= L'A' && *b <= L'Z') ? *b + 32 : *b;
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void ci_trim(fvpl_wchar *s) {
    int n = pv_len(s), i = 0;
    while (i < n && ci_is_fw(s[i])) i++;
    if (i > 0) {
        for (int k = i; k <= n; k++) s[k - i] = s[k];
        n -= i;
    }
    while (n > 0 && ci_is_fw(s[n - 1])) s[--n] = 0;
    if (n >= 2 && ((s[0] == L'"' && s[n - 1] == L'"') ||
                   (s[0] == L'\'' && s[n - 1] == L'\''))) {
        s[n - 1] = 0;
        for (int k = 1; k < n; k++) s[k - 1] = s[k];
    }
}

/* 行内注释: 值后面跟 " ; ..." 或 " # ..." 时截断。
 * 带引号的值取引号内的原文, 里面的分号是内容不是注释。 */
static void ci_strip_comment(fvpl_wchar *v) {
    fvpl_wchar q = v[0];
    if (q == L'"' || q == L'\'') {
        fvpl_wchar *end = v + 1;
        while (*end && *end != q) end++;
        if (*end) {
            *end = 0;
            for (int i = 1; v[i]; i++) v[i - 1] = v[i];
        }
        return;
    }
    for (int i = 0; v[i]; i++) {
        if ((v[i] == L';' || v[i] == L'#') &&
            (i == 0 || v[i - 1] == L' ' || v[i - 1] == L'\t')) {
            v[i] = 0;
            return;
        }
    }
}

int cfg_ini_parse(const fvpl_wchar *text, FvplIni *ini) {
    fvpl_wchar sec[FVPL_INI_SECCAP] = {0};
    int i = 0;
    if (!text || !ini) return 0;
    ini->n = 0;
    while (text[i]) {
        fvpl_wchar line[FVPL_INI_VALCAP];
        int n = 0;
        while (text[i] && text[i] != L'\n') {
            if (n < FVPL_INI_VALCAP - 1) line[n++] = text[i];
            i++;
        }
        line[n] = 0;
        if (text[i] == L'\n') i++;
        ci_trim(line);
        if (!line[0] || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            fvpl_wchar *end;
            line[0] = 0;
            end = line + 1;
            while (*end && *end != L']') end++;
            *end = 0;
            pv_copy(sec, FVPL_INI_SECCAP, line + 1);
            continue;
        }
        {
            fvpl_wchar *eq = line;
            while (*eq && *eq != L'=') eq++;
            if (!*eq) continue;
            *eq = 0;
            if (ini->n >= FVPL_INI_MAXENTRY) break;
            pv_copy(ini->ent[ini->n].sec, FVPL_INI_SECCAP, sec);
            pv_copy(ini->ent[ini->n].key, FVPL_INI_KEYCAP, line);
            pv_copy(ini->ent[ini->n].val, FVPL_INI_VALCAP, eq + 1);
            ci_trim(ini->ent[ini->n].key);
            ci_strip_comment(ini->ent[ini->n].val);
            ci_trim(ini->ent[ini->n].val);
            if (ini->ent[ini->n].key[0]) ini->n++;
        }
    }
    return 1;
}

const fvpl_wchar *cfg_ini_get(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key) {
    if (!ini) return 0;
    for (int i = 0; i < ini->n; i++)
        if (ci_eq(ini->ent[i].sec, sec) && ci_eq(ini->ent[i].key, key))
            return ini->ent[i].val;
    return 0;
}

static fvpl_uint ci_num(const fvpl_wchar *s) {
    fvpl_uint v = 0;
    int base = 10;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) { base = 16; s += 2; }
    while (*s) {
        fvpl_wchar c = *s++;
        int d = -1;
        if (c >= L'0' && c <= L'9') d = c - L'0';
        else if (base == 16 && c >= L'a' && c <= L'f') d = c - L'a' + 10;
        else if (base == 16 && c >= L'A' && c <= L'F') d = c - L'A' + 10;
        if (d < 0 || d >= base) break;
        v = v * base + (fvpl_uint)d;
    }
    return v;
}

fvpl_int cfg_ini_int(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key, fvpl_int dflt) {
    const fvpl_wchar *v = cfg_ini_get(ini, sec, key);
    if (!v || !*v) return dflt;
    if (*v == L'-') return -(fvpl_int)ci_num(v + 1);
    return (fvpl_int)ci_num(v);
}

/* Documents 一级目录默认只迁 FAVORITE: FVP 系列都用它, 全量扫会把用户别的文档也拖进游戏目录。 */
static const fvpl_wchar DEF_MIGRATE[] = { L'F','A','V','O','R','I','T','E',0 };
static const fvpl_wchar SEC_LAU[] = { L'l','a','u','n','c','h','e','r',0 };
static const fvpl_wchar SEC_SAVE[] = { L's','a','v','e',0 };
static const fvpl_wchar SEC_NLS[] = { L'n','l','s',0 };

/* ---- 宿主码页与 NLS 默认值 ------------------------------------------------
 * 「本机码页」= 宿主进程真实的 ANSI 码页 (中文 Windows 通常是 936), 由调用方
 * 用 cfg_set_host_acp() 注入 (纯逻辑模块不能 include windows.h)。
 * 没写 [nls] 时就用它: 谎报一个宿主并不使用的码页, 会让非 ASCII 路径在
 * A↔W 往返时被改写成别的字节, 游戏自己开资源就会失败。 */
static fvpl_uint g_host_acp = 932;

void cfg_set_host_acp(fvpl_uint cp) {
    if (cp) g_host_acp = cp;
}

/* 整行删掉 / 留空 / 写 auto = 用本机码页自动推断 */
static int ci_is_auto(const fvpl_wchar *v) {
    return !v || !*v || ci_eq(v, L"auto");
}

/* GDI charset: 和 codepage 配套, CreateFontA 造出来的字体才不会选错字库 */
static fvpl_uint ci_charset_of(fvpl_uint cp) {
    switch (cp) {
    case 932:  return 128;  /* SHIFTJIS_CHARSET */
    case 936:  return 134;  /* GB2312_CHARSET */
    case 949:  return 129;  /* HANGUL_CHARSET */
    case 950:  return 136;  /* CHINESEBIG5_CHARSET */
    case 1252: return 0;    /* ANSI_CHARSET */
    default:   return 1;    /* DEFAULT_CHARSET: 系统按当前区域自己挑 */
    }
}

int cfg_build(const FvplIni *ini, const fvpl_wchar *basedir, FvplConfig *out,
              fvpl_wchar *err, int errcch) {
    const fvpl_wchar *s;
    if (!ini || !basedir || !out) return 0;
    for (int i = 0; i < (int)sizeof(*out); i++) ((unsigned char *)out)[i] = 0;

    out->inject_children = cfg_ini_int(ini, SEC_LAU, L"inject_children", 0);
    s = cfg_ini_get(ini, SEC_LAU, L"exe");
    if (s && *s) pv_copy(out->target, FVPL_MAX_PATH, s);

    s = cfg_ini_get(ini, SEC_LAU, L"params");
    if (s) pv_copy(out->params, (int)(sizeof(out->params) / sizeof(fvpl_wchar)), s);

    s = cfg_ini_get(ini, SEC_LAU, L"workdir");
    if (s && *s) {
        if (pv_is_abs(s)) pv_copy(out->workdir, FVPL_MAX_PATH, s);
        else pv_join(out->workdir, FVPL_MAX_PATH, basedir, s);
        pv_norm(out->workdir, FVPL_MAX_PATH);
    }

    out->save_redirect = cfg_ini_int(ini, SEC_SAVE, L"redirect", 1);
    out->save_migrate  = cfg_ini_int(ini, SEC_SAVE, L"migrate", 1);
    s = cfg_ini_get(ini, SEC_SAVE, L"migrate_names");
    pv_copy(out->migrate_names, (int)(sizeof(out->migrate_names) / sizeof(fvpl_wchar)),
            (s && *s) ? s : DEF_MIGRATE);

    s = cfg_ini_get(ini, SEC_SAVE, L"path");
    if (!s || !*s) s = L".save";
    if (pv_is_abs(s)) pv_copy(out->save_path, FVPL_MAX_PATH, s);
    else if (!pv_join(out->save_path, FVPL_MAX_PATH, basedir, s)) {
        if (err) pv_copy(err, errcch, L"[save] path 写的路径太长了，请把游戏文件夹移到更短的位置");
        return 0;
    }
    if (!pv_norm(out->save_path, FVPL_MAX_PATH)) {
        if (err) pv_copy(err, errcch, L"[save] path 看不懂；可以填 .（=游戏目录）、.save，\n或 D:\\saves\\xxx 这样的完整路径");
        return 0;
    }
    /* path=. (存档就在游戏目录) 现在也允许: 迁移改成按需搬本游戏那一份,
     * 不会再出现"把 Documents 下所有游戏一起灌进来"的问题 */
    pv_strip_sep(out->save_path);

    {
        const fvpl_wchar *ids = cfg_ini_get(ini, SEC_SAVE, L"folder_ids");
        fvpl_wchar buf[64];
        int k = 0, slot = 0;
        if (ids && *ids) {
            pv_copy(buf, 64, ids);
            for (int i = 0; ; i++) {
                int term = (buf[i] == 0);
                if (term || buf[i] == L',' || buf[i] == L';' ||
                    buf[i] == L' ' || buf[i] == L'\t') {
                    buf[i] = 0;
                    if (i > k && slot < 8) out->folder_ids[slot++] = ci_num(buf + k);
                    k = i + 1;
                    if (term) break;
                }
            }
        }
        out->n_folder_ids = slot;
        if (!out->n_folder_ids) { out->folder_ids[0] = 5; out->n_folder_ids = 1; }
    }

    /* 不写 [nls] 也能跑: codepage 跟随本机, charset 按 codepage 推导, 不动界面文字。 */
    s = cfg_ini_get(ini, SEC_NLS, L"codepage");
    out->codepage = ci_is_auto(s)
                  ? g_host_acp
                  : (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"codepage", (fvpl_int)g_host_acp);
    s = cfg_ini_get(ini, SEC_NLS, L"font_charset");
    out->font_charset = ci_is_auto(s)
                      ? ci_charset_of(out->codepage)
                      : (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"font_charset", 0);
    /* 文本层: 留空 = 与 codepage 相同 (不做文本层转换);
     * 填了 = 游戏台词/标题所用的码页 (日文原版 932)。只影响字体/字形/文本 API。
     * 兼容旧键 font_fix = 1 (等价于"按 codepage 做字体修正")。 */
    s = cfg_ini_get(ini, SEC_NLS, L"text_codepage");
    if (ci_is_auto(s)) {
        out->text_codepage = cfg_ini_int(ini, SEC_NLS, L"font_fix", 0) ? out->codepage : 0;
    } else {
        out->text_codepage = (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"text_codepage", 0);
    }
    s = cfg_ini_get(ini, SEC_NLS, L"font_charset");
    out->font_charset = ci_is_auto(s)
                      ? ci_charset_of(out->text_codepage ? out->text_codepage : out->codepage)
                      : (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"font_charset", 0);
    out->virtualize_files = cfg_ini_int(ini, SEC_NLS, L"virtualize_files", 1);
    out->virtualize_ui    = cfg_ini_int(ini, SEC_NLS, L"virtualize_ui", out->text_codepage ? 1 : 0);
    out->lcid             = (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"lcid", 0);
    {
        const fvpl_wchar *ff = cfg_ini_get(ini, SEC_NLS, L"font_face");
        pv_copy(out->font_face, 64, (ff && *ff) ? ff : L"");
    }
    if (out->codepage == 0 || out->codepage == 1) {
        /* 0=CP_ACP(正是要虚拟掉的), 1=CP_OEMCP —— 都会让 hook 递归或语义反转 */
        if (err) pv_copy(err, errcch, L"[nls] codepage 不能填 0 或 1。\n"
                                      L"想用本机码页就把这一行留空或整行删掉。\n"
                                      L"也可以填 932 / 936 / 950 / 949 / 1252 / 65001。");
        return 0;
    }
    if (out->text_codepage == 1) {
        /* 1=CP_OEMCP 语义相反; 0 是合法值(= 不做文本层转换), 不在这里拦 */
        if (err) pv_copy(err, errcch, L"[nls] text_codepage 不能填 1。\n"
                                      L"0 或留空或整行删掉 = 不做文本层转换。\n"
                                      L"日文原版填 932, 简中汉化填 936。");
        return 0;
    }
    return 1;
}
