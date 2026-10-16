/* pathutil.c — 见 pathutil.h。只用 '\' 作分隔符 (Windows 唯一合法分隔符)。 */
#include "pathutil.h"

static int pv_up(fvpl_wchar c) {
    if (c >= L'a' && c <= L'z') return c - L'a' + L'A';
    return c;
}

int pv_len(const fvpl_wchar *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int pv_is_abs(const fvpl_wchar *s) {
    if (!s) return 0;
    if (s[0] == L'\\' && s[1] == L'\\') return 1;          /* UNC */
    if (((s[0] >= L'A' && s[0] <= L'Z') || (s[0] >= L'a' && s[0] <= L'z')) &&
        s[1] == L':' && (s[2] == L'\\' || s[2] == L'/')) return 1;
    return 0;
}

int pv_copy(fvpl_wchar *dst, int cch, const fvpl_wchar *src) {
    int n;
    if (!dst || cch <= 0) return 0;
    n = pv_len(src);
    if (n >= cch) { dst[0] = 0; return 0; }
    for (int i = 0; i <= n; i++) dst[i] = src[i];
    return 1;
}

int pv_dirname(fvpl_wchar *dst, int cch, const fvpl_wchar *path) {
    int n, i, cut = -1;
    n = pv_len(path);
    if (n <= 0 || n >= cch) return 0;
    for (i = n - 1; i >= 0; i--)
        if (path[i] == L'\\' || path[i] == L'/') { cut = i; break; }
    if (cut < 0) return 0;
    while (cut > 0 && (path[cut - 1] == L'\\' || path[cut - 1] == L'/')) cut--;
    /* "C:\" 这类根目录不能裁成 "C:" */
    if (cut == 2 && path[1] == L':') cut = 3;
    for (i = 0; i < cut; i++) dst[i] = path[i];
    dst[cut] = 0;
    return 1;
}

int pv_join(fvpl_wchar *dst, int cch, const fvpl_wchar *a, const fvpl_wchar *b) {
    int la, lb, need;
    if (pv_is_abs(b)) return pv_copy(dst, cch, b);
    la = pv_len(a);
    lb = pv_len(b);
    if (la + lb + 2 > cch) return 0;
    need = 1;
    if (la > 0 && (a[la - 1] == L'\\' || a[la - 1] == L'/')) need = 0;
    if (lb > 0 && (b[0] == L'\\' || b[0] == L'/')) { lb--; b++; }
    for (int i = 0; i < la; i++) dst[i] = a[i];
    int n = la;
    if (need && n > 0) dst[n++] = L'\\';
    for (int i = 0; i < lb; i++) dst[n + i] = b[i];
    dst[n + lb] = 0;
    return 1;
}

void pv_strip_sep(fvpl_wchar *s) {
    int n = pv_len(s);
    while (n > 0 && (s[n - 1] == L'\\' || s[n - 1] == L'/')) {
        if (n == 3 && s[1] == L':') break;   /* 保留 "C:\" */
        s[--n] = 0;
    }
}

/* 归一化: '/' 换 '\'、折叠重复分隔符、吃掉 "." 段、去掉尾部斜杠。
 * ".." 有意不解析 —— 配置里的相对上跳保持原样交给系统, 免得拼出的路径和字面不符。
 * 不支持 "\\?\" 设备命名空间 (配置里不该出现)。 */
int pv_norm(fvpl_wchar *s, int cch) {
    fvpl_wchar tmp[FVPL_MAX_PATH];
    int i = 0, o = 0, n, unc, segs;

    if (!s) return 0;
    n = pv_len(s);
    if (n == 0) return 1;
    if (n >= (int)sizeof(tmp) || cch <= 0) return 0;
    unc = (s[0] == L'\\' && s[1] == L'\\');
    segs = 0;
    if (unc) { tmp[0] = L'\\'; tmp[1] = L'\\'; o = 2; }

    while (i < n) {
        int k;
        while (i < n && (s[i] == L'\\' || s[i] == L'/')) i++;
        k = i;
        while (k < n && s[k] != L'\\' && s[k] != L'/') k++;
        if (k - i == 1 && s[i] == L'.') { i = k; continue; }
        if (k > i) {
            if (segs > 0) tmp[o++] = L'\\';
            for (int j = i; j < k; j++) tmp[o++] = s[j];
            segs++;
        }
        i = k;
    }
    if (segs == 0) {
        tmp[0] = L'.';
        tmp[1] = 0;
    } else if (o == 2 && tmp[1] == L':') {   /* "C:" -> "C:\" */
        tmp[2] = L'\\';
        tmp[3] = 0;
    } else {
        tmp[o] = 0;
    }
    return pv_copy(s, cch, tmp) ? 1 : 0;
}

int pv_ci_equal(const fvpl_wchar *a, const fvpl_wchar *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (pv_up(*a) != pv_up(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

int pv_ends_with_ci(const fvpl_wchar *s, const fvpl_wchar *suffix) {
    int ls = pv_len(s), lf = pv_len(suffix);
    if (lf > ls) return 0;
    return pv_ci_equal(s + (ls - lf), suffix);
}
