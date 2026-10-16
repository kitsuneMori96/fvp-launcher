/* peinfo.c — 见 peinfo.h. 全程带边界校验, 任何越界一律判失败。 */
#include "peinfo.h"

static unsigned int rd32(const unsigned char *b) {
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
           ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}
static unsigned int rd16(const unsigned char *b) {
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8);
}
static unsigned long long rd64(const unsigned char *b) {
    return (unsigned long long)rd32(b) | ((unsigned long long)rd32(b + 4) << 32);
}

static int ok(unsigned long long off, unsigned long long len, unsigned int size) {
    return len <= size && off <= size - len;
}

static int ci_a_eq(const char *a, const char *b) {
    while (*a && *b) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* 导入表里的 DLL 名常带扩展名, 允许 ".dll"/".DLL" 后缀差异 */
static int ci_a_ends(const char *s, const char *suffix) {
    int ls = 0, lf = 0;
    while (s[ls]) ls++;
    while (suffix[lf]) lf++;
    if (lf > ls) return 0;
    return ci_a_eq(s + (ls - lf), suffix);
}

static int pe_layout(const unsigned char *b, unsigned int size, int *is32,
                     unsigned int *opt_dir, unsigned int *sec_off, unsigned int *n_sec) {
    unsigned int nt, opt, magic;
    if (!b || size < 0x40) return 0;
    nt = rd32(b + 0x3C);
    if (!ok(nt, 24 + 2, size) || rd32(b + nt) != 0x00004550) return 0;
    opt = nt + 24;
    magic = rd16(b + opt);
    if (magic != 0x10b && magic != 0x20b) return 0;
    if (!ok(opt, 112, size)) return 0;
    if (is32) *is32 = (magic == 0x10b);
    if (opt_dir) *opt_dir = opt + (magic == 0x10b ? 96 : 112);
    if (sec_off) *sec_off = opt + rd16(b + nt + 20);
    if (n_sec) *n_sec = rd16(b + nt + 6);
    return 1;
}

int pe_machine(const unsigned char *blob, unsigned int size) {
    if (!pe_layout(blob, size, 0, 0, 0, 0)) return -1;
    return (int)rd16(blob + rd32(blob + 0x3C) + 4);
}

static int rva_to_off(const unsigned char *b, unsigned int size, unsigned int rva,
                      unsigned int *out) {
    unsigned int sec_off, n_sec;
    if (!pe_layout(b, size, 0, 0, &sec_off, &n_sec)) return 0;
    if (n_sec > 96 || !ok(sec_off, (unsigned long long)n_sec * 40, size)) return 0;
    for (unsigned int i = 0; i < n_sec; i++) {
        unsigned int s = sec_off + i * 40;
        unsigned int va = rd32(b + s + 12), rsz = rd32(b + s + 16);
        unsigned int raw = rd32(b + s + 20);
        if (rsz && rva >= va && (unsigned long long)rva - va < rsz &&
            ok((unsigned long long)raw + (rva - va), 1, size)) {
            *out = raw + (rva - va);
            return 1;
        }
    }
    return 0;
}

/* 第 index 个数据目录的 RVA/大小 */
static int data_dir(const unsigned char *b, unsigned int size, int index,
                    unsigned int *rva, unsigned int *len) {
    unsigned int od;
    if (!pe_layout(b, size, 0, &od, 0, 0)) return 0;
    if (!ok(od + (unsigned)index * 8 + 8, 8, size)) return 0;
    *rva = rd32(b + od + index * 8);
    *len = rd32(b + od + index * 8 + 4);
    return 1;
}

int pe_has_import(const unsigned char *blob, unsigned int size,
                  const char *dll_name, const char *func_name) {
    unsigned int rva, len, io, desc, is32, tsize;
    unsigned long long ordflag;
    if (!blob || !dll_name || !func_name) return 0;
    if (!pe_layout(blob, size, (int *)&is32, 0, 0, 0)) return 0;
    if (!data_dir(blob, size, 1, &rva, &len) || !rva) return 0;
    if (!rva_to_off(blob, size, rva, &io)) return 0;
    tsize = is32 ? 4u : 8u;
    ordflag = is32 ? 0x80000000ULL : 0x8000000000000000ULL;

    for (desc = io; ; desc += 20) {
        unsigned int name_rva, int_rva, iat_rva, nameoff;
        if (!ok(desc, 20, size)) return 0;
        name_rva = rd32(blob + desc + 12);
        if (!name_rva) return 0;                      /* 描述符表结束 */
        if (!rva_to_off(blob, size, name_rva, &nameoff)) return 0;
        if (!ci_a_ends((const char *)blob + nameoff, dll_name)) continue;

        int_rva = rd32(blob + desc);                  /* OriginalFirstThunk */
        iat_rva = rd32(blob + desc + 16);             /* FirstThunk */
        {
            unsigned int to = 0;
            if (int_rva && rva_to_off(blob, size, int_rva, &to)) { /* 用 INT */ }
            else if (iat_rva && rva_to_off(blob, size, iat_rva, &to)) { /* 退化成 IAT */ }
            else return 0;                            /* 只有序号或表丢失, 无法按名匹配 */
            for (unsigned int i = 0; ; i++) {
                unsigned long long thunk;
                unsigned int noff;
                if (!ok((unsigned long long)to + (unsigned long long)i * tsize, tsize, size)) return 0;
                thunk = is32 ? rd32(blob + to + i * 4) : rd64(blob + to + i * 8);
                if (thunk == 0) return 0;             /* 该 DLL 的导入名表结束 */
                if (thunk & ordflag) continue;        /* 按序号导入, 无名可比 */
                if (!rva_to_off(blob, size, (unsigned int)thunk, &noff)) return 0;
                if (!ok(noff, 3, size)) return 0;
                if (ci_a_eq((const char *)blob + noff + 2, func_name)) return 1;
            }
        }
    }
}
