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
    if (!s || !*s) s = L"userdata";
    if (pv_is_abs(s)) pv_copy(out->save_path, FVPL_MAX_PATH, s);
    else if (!pv_join(out->save_path, FVPL_MAX_PATH, basedir, s)) {
        if (err) pv_copy(err, errcch, L"save path 过长");
        return 0;
    }
    if (!pv_norm(out->save_path, FVPL_MAX_PATH)) {
        if (err) pv_copy(err, errcch, L"save path 过长或无法归一化");
        return 0;
    }
    if (pv_ci_equal(out->save_path, basedir) && out->save_migrate) {
        /* 存档目录 == 游戏目录时, 迁移会把 Documents\FAVORITE 灌进游戏根, 风险不可控 */
        if (err) pv_copy(err, errcch, L"save path 不能等于游戏目录 (除非关闭 migrate)");
        return 0;
    }
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

    out->codepage         = (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"codepage", 932);
    out->font_charset     = (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"font_charset", 128);
    out->virtualize_files = cfg_ini_int(ini, SEC_NLS, L"virtualize_files", 1);
    out->virtualize_ui    = cfg_ini_int(ini, SEC_NLS, L"virtualize_ui", 1);
    out->lcid             = (fvpl_uint)cfg_ini_int(ini, SEC_NLS, L"lcid", 0);
    if (out->codepage == 0 || out->codepage == 1) {
        /* 0=CP_ACP(正是要虚拟掉的), 1=CP_OEMCP —— 都会让 hook 递归或语义反转 */
        if (err) pv_copy(err, errcch, L"codepage 不能是 0 或 1");
        return 0;
    }
    return 1;
}
