/* pathutil.h — 宽字符路径纯逻辑 (Windows 与 host 单测共用)
 * 返回 0 表示缓冲区不足或被拒绝, 非 0 表示成功。 */
#ifndef FVPL_PATHUTIL_H
#define FVPL_PATHUTIL_H

#include "fvpl_types.h"

int  pv_len(const fvpl_wchar *s);
int  pv_is_abs(const fvpl_wchar *s);
int  pv_copy(fvpl_wchar *dst, int cch, const fvpl_wchar *src);
int  pv_dirname(fvpl_wchar *dst, int cch, const fvpl_wchar *path);
int  pv_join(fvpl_wchar *dst, int cch, const fvpl_wchar *a, const fvpl_wchar *b);
void pv_strip_sep(fvpl_wchar *s);
int  pv_norm(fvpl_wchar *s, int cch);
int  pv_ci_equal(const fvpl_wchar *a, const fvpl_wchar *b);
int  pv_ends_with_ci(const fvpl_wchar *s, const fvpl_wchar *suffix);

#endif /* FVPL_PATHUTIL_H */
