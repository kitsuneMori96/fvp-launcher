/* utf8w.h — UTF-8 <-> UTF-16 纯逻辑转换器 */
#ifndef FVPL_UTF8W_H
#define FVPL_UTF8W_H

#include "fvpl_types.h"

/* 成功返回写出的 wchar 数 (含结尾 0 之前的个数), 失败返回 -1 */
int uw_decode(const unsigned char *in, int nbytes, fvpl_wchar *out, int outcch);
/* 成功返回写出的字节数, 失败返回 -1 */
int uw_encode(const fvpl_wchar *s, int nchars, unsigned char *out, int outcap);

#endif /* FVPL_UTF8W_H */
