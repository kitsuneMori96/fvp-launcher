/* utf8w.c — UTF-8 <-> UTF-16LE, 纯逻辑, 不依赖任何系统代码页。
 * 配置文件必须按 UTF-8 解析: 引擎侧的字节串含义由虚拟 ACP 决定,
 * 若启动器自己用 ACP 读配置, 换 codepage 时配置会跟着漂移。 */
#include "utf8w.h"

int uw_decode(const unsigned char *in, int nbytes, fvpl_wchar *out, int outcch) {
    int i = 0, o = 0;
    unsigned int cp;
    int need;
    while (i < nbytes) {
        unsigned char b = in[i];
        if (b < 0x80) { cp = b; need = 0; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1Fu; need = 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0Fu; need = 2; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07u; need = 3; }
        else return -1;
        if (i + need >= nbytes) return -1;
        for (int k = 1; k <= need; k++) {
            unsigned char c = in[i + k];
            if ((c & 0xC0) != 0x80) return -1;
            cp = (cp << 6) | (c & 0x3Fu);
        }
        i += need + 1;
        if (cp <= 0xFFFF) {
            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;
            if (o + 1 >= outcch) return -1;
            out[o++] = (fvpl_wchar)cp;
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            if (o + 2 >= outcch) return -1;
            out[o++] = (fvpl_wchar)(0xD800 + (cp >> 10));
            out[o++] = (fvpl_wchar)(0xDC00 + (cp & 0x3FF));
        } else return -1;
    }
    out[o] = 0;
    return o;
}

int uw_encode(const fvpl_wchar *s, int nchars, unsigned char *out, int outcap) {
    int o = 0;
    for (int i = 0; i < nchars; i++) {
        unsigned int cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < nchars &&
            s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
        }
        if (cp < 0x80) {
            if (o + 1 > outcap) return -1;
            out[o++] = (unsigned char)cp;
        } else if (cp < 0x800) {
            if (o + 2 > outcap) return -1;
            out[o++] = (unsigned char)(0xC0 | (cp >> 6));
            out[o++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            if (o + 3 > outcap) return -1;
            out[o++] = (unsigned char)(0xE0 | (cp >> 12));
            out[o++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            if (o + 4 > outcap) return -1;
            out[o++] = (unsigned char)(0xF0 | (cp >> 18));
            out[o++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else return -1;
    }
    return o;
}
