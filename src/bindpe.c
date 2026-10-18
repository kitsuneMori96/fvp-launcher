/* bindpe.c — 见 bindpe.h。全程边界校验: 越界一律判失败, 不碰缓冲区。 */
#include "bindpe.h"
#include "peinfo.h"
#include <string.h>

#define OFF_SIZEOF_INIT_DATA 8
#define OFF_SECTION_ALIGN   32
#define OFF_FILE_ALIGN      36
#define OFF_SIZEOF_IMAGE    56
#define SEC_HDR_SIZE        40

#define SCN_CNT_INITIALIZED_DATA 0x00000040u
#define SCN_MEM_READ             0x40000000u
#define SCN_MEM_WRITE            0x80000000u
/* 新节必须可写: 加载器要把解析好的函数地址写回 FirstThunk (ILAT) */
#define FVPL_SCN (SCN_CNT_INITIALIZED_DATA | SCN_MEM_READ | SCN_MEM_WRITE)

#define FVPL_SEC_NAME ".fvpl"

static unsigned int rd32(const unsigned char *b) {
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
           ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}
static unsigned short rd16(const unsigned char *b) {
    return (unsigned short)(b[0] | (b[1] << 8));
}
static void wr32(unsigned char *b, unsigned int v) {
    b[0] = (unsigned char)(v); b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24);
}
static void wr16(unsigned char *b, unsigned int v) {
    b[0] = (unsigned char)(v); b[1] = (unsigned char)(v >> 8);
}
static unsigned int al_up(unsigned int v, unsigned int a) {
    if (a == 0 || a > 0x10000000u || v > 0xFFFFFFFFu - (a - 1)) return 0xFFFFFFFFu;
    return (v + (a - 1)) / a * a;
}
static void say(char *err, int errcch, const char *msg) {
    int i;
    if (!err || errcch <= 0) return;
    for (i = 0; msg[i] && i < errcch - 1; i++) err[i] = msg[i];
    err[i] = 0;
}
static int blen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* 写进导入表的字符串会被加载器当文件名用, 只放 ASCII 文件名安全字符 */
static int name_ok(const char *s, int cap) {
    int i, n = blen(s);
    if (n <= 0 || n >= cap) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int good = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!good) return 0;
    }
    return 1;
}

/* 导入描述符表长度 (以 20 字节一项计, 遇 Name==0 结束)。越界返回 -1 */
static int count_descriptors(const unsigned char *buf, unsigned int size, unsigned int io) {
    int n = 0;
    while (n < 512) {
        unsigned int e = io + (unsigned)n * 20;
        if ((unsigned long long)e + 20 > size) return -1;
        if (rd32(buf + e + 12) == 0) return n;
        n++;
    }
    return -1;
}

/* 拿到绑定所需的一堆布局事实; 任何一条不合格返回 0 并写明原因 */
static int bind_layout(const unsigned char *buf, unsigned int size, const char *dll_name,
                       int *is32, unsigned int *opt, unsigned int *opt_dir,
                       unsigned int *sec_off, unsigned int *n_sec, unsigned int *io,
                       int *n_desc, unsigned int *first_raw, int *already,
                       char *err, int errcch) {
    unsigned int irva, ilen, nt, sal;
    *already = 0;
    if (!buf || size < 0x1000 || rd16(buf) != 0x5A4D) {
        say(err, errcch, "这不是一个有效的 exe (文件太小或缺少 MZ 头)");
        return 0;
    }
    if (!pe_layout(buf, size, is32, opt_dir, sec_off, n_sec)) {
        say(err, errcch, "这个 exe 的 PE 头看不懂 (不是 PE32/PE32+ 或头部被截断)");
        return 0;
    }
    nt = rd32(buf + 0x3C);
    *opt = nt + 24;
    if (*n_sec == 0 || *n_sec >= 256) {
        say(err, errcch, "这个 exe 的节数量不正常, 不敢改");
        return 0;
    }
    sal = rd32(buf + *opt + OFF_SECTION_ALIGN);
    if (sal == 0 || rd32(buf + *opt + OFF_FILE_ALIGN) == 0) {
        say(err, errcch, "这个 exe 的对齐参数为 0, 无法安全地追加一节");
        return 0;
    }
    if (!pe_data_dir(buf, size, 1, &irva, &ilen) || irva == 0 || ilen == 0) {
        say(err, errcch, "这个 exe 没有导入表, 绑不了 (钩子无从加载)");
        return 0;
    }
    if (!pe_rva_to_off(buf, size, irva, io)) {
        say(err, errcch, "导入表的位置对不到文件内容上, 不敢改");
        return 0;
    }
    *n_desc = count_descriptors(buf, size, *io);
    if (*n_desc <= 0) {
        say(err, errcch, "导入表内容读不出来 (描述符表为空或越界)");
        return 0;
    }
    if ((unsigned long long)*io + (unsigned long long)(*n_desc + 1) * 20 > size) {
        say(err, errcch, "导入表超出文件范围, 不敢改");
        return 0;
    }
    {
        unsigned int i;
        *first_raw = 0xFFFFFFFFu;
        for (i = 0; i < *n_sec; i++) {
            unsigned int s = *sec_off + i * SEC_HDR_SIZE;
            unsigned int raw = rd32(buf + s + 20);
            if (raw != 0 && raw < *first_raw) *first_raw = raw;
        }
        if (*first_raw == 0xFFFFFFFFu) {
            say(err, errcch, "这个 exe 各节都没有磁盘数据, 无法安全地追加一节");
            return 0;
        }
    }
    if (pe_has_import(buf, size, dll_name, "FvplAttach")) *already = 1;
    return 1;
}

/* 只读体检, 供 -check 与单测使用。任一输出指针可为 NULL。
 * 返回 1 = 这个 exe 可以绑 (位数/导入表/头部空位都合格), 0 = 不能。 */
int pe_bind_inspect(const unsigned char *buf, unsigned int size, const char *dll_name,
                    int *is32, int *n_dlls, int *header_slack, int *already) {
    unsigned int opt, opt_dir, sec_off, n_sec, io, sl;
    int n_desc, al, i32;
    char err[128];
    if (!bind_layout(buf, size, (dll_name && *dll_name) ? dll_name : "fvpl_hook.dll",
                     &i32, &opt, &opt_dir, &sec_off, &n_sec, &io, &n_desc, &sl, &al,
                     err, (int)sizeof(err)))
        return 0;
    if (is32) *is32 = i32;
    if (n_dlls) *n_dlls = n_desc;
    if (header_slack) *header_slack = (int)(sl - (sec_off + n_sec * SEC_HDR_SIZE));
    if (already) *already = al;
    return 1;
}

int pe_bind_apply(unsigned char *buf, unsigned int cap, unsigned int *size,
                  const char *dll_name, const char *func_name,
                  char *err, int errcch) {
    unsigned int opt, opt_dir, sec_off, n_sec, io, first_raw, new_va, hdr_off;
    unsigned int desc_len, ilt_off, iat_off, name_off, dll_off, vsize, raw_len, raw_off;
    unsigned int sal, fal, i, max_end = 0, sz;
    int n_desc, already, is32, tsize, dl_len, fn_len;

    if (!buf || !size || !err || errcch <= 0) { return FVPL_BIND_FAIL; }
    if (!name_ok(dll_name, 64)) {
        say(err, errcch, "DLL 名字只能用字母/数字/. _ - (例如 fvpl_hook.dll), 不要带路径");
        return FVPL_BIND_FAIL;
    }
    if (!name_ok(func_name, 64)) {
        say(err, errcch, "导出符号名只能用字母/数字/. _ - (例如 FvplAttach)");
        return FVPL_BIND_FAIL;
    }
    if (!bind_layout(buf, *size, dll_name, &is32, &opt, &opt_dir, &sec_off, &n_sec,
                     &io, &n_desc, &first_raw, &already, err, errcch))
        return FVPL_BIND_FAIL;
    if (already) {
        say(err, errcch, "这个 exe 已经绑过 fvpl_hook 了, 不需要再绑一次");
        return FVPL_BIND_ALREADY;
    }

    tsize = is32 ? 4 : 8;
    sal = rd32(buf + opt + OFF_SECTION_ALIGN);
    fal = rd32(buf + opt + OFF_FILE_ALIGN);
    dl_len = blen(dll_name);
    fn_len = blen(func_name);

    /* 新节里的摆放: 描述符表(含结尾空项) -> INT -> IAT -> 名字表 -> DLL 名 */
    desc_len = (unsigned)(n_desc + 2) * 20u;
    ilt_off  = al_up(desc_len, 8);
    iat_off  = al_up(ilt_off + (unsigned)tsize * 2u, 8);
    name_off = iat_off + (unsigned)tsize * 2u;
    dll_off  = (unsigned)(name_off + 2 + fn_len + 1);
    vsize    = (unsigned)(dll_off + dl_len + 1);
    raw_len  = al_up(vsize, fal);
    if (raw_len == 0xFFFFFFFFu) { say(err, errcch, "新节大小算不出来 (对齐值异常)"); return FVPL_BIND_FAIL; }

    /* 节头还得挤在节表和第一段磁盘数据之间: MSVC 链接器通常会留几百字节 */
    hdr_off = sec_off + n_sec * SEC_HDR_SIZE;
    if ((unsigned long long)hdr_off + SEC_HDR_SIZE > first_raw) {
        say(err, errcch, "这个 exe 的头部没有空位再加一个节, 绑不了 (改用启动器方式)");
        return FVPL_BIND_FAIL;
    }

    /* 新节的虚拟地址 = 所有节占用的最高处再按节对齐抬上去 */
    for (i = 0; i < n_sec; i++) {
        unsigned int s = sec_off + i * SEC_HDR_SIZE;
        unsigned int va = rd32(buf + s + 12);
        unsigned int vs = rd32(buf + s + 8);
        unsigned int rs = rd32(buf + s + 16);
        unsigned int used = vs > rs ? vs : rs;
        unsigned int end = al_up(va + al_up(used, sal), sal);
        if (end == 0xFFFFFFFFu) { say(err, errcch, "节地址超出可表示范围, 不敢改"); return FVPL_BIND_FAIL; }
        if (end > max_end) max_end = end;
    }
    new_va = max_end;
    if (new_va == 0 || new_va >= 0x80000000u) {
        say(err, errcch, "算出来的新节地址不可用, 不敢改");
        return FVPL_BIND_FAIL;
    }

    sz = *size;
    raw_off = al_up(sz, fal);
    if (raw_off == 0xFFFFFFFFu || (unsigned long long)raw_off + raw_len > cap) {
        say(err, errcch, "缓冲区放不下新节 (文件太大)");
        return FVPL_BIND_FAIL;
    }

    memset(buf + sz, 0, (size_t)(raw_off - sz) + raw_len);

    /* 1) 原描述符表整搬过来 (里面的 RVA 指向 .rdata, 我们没挪动 .rdata, 照抄即可),
     *    后面接我们自己的那一项, 再接一项全 0 作为结束标记。 */
    memcpy(buf + raw_off, buf + io, (size_t)n_desc * 20u);
    {
        unsigned int d = raw_off + (unsigned)n_desc * 20u;
        wr32(buf + d + 0,  new_va + ilt_off);       /* OriginalFirstThunk */
        wr32(buf + d + 4,  0);                      /* TimeDateStamp */
        wr32(buf + d + 8,  0xFFFFFFFFu);            /* ForwarderChain: 链接器就是这么写的 */
        wr32(buf + d + 12, new_va + dll_off);       /* DLL 名 */
        wr32(buf + d + 16, new_va + iat_off);       /* FirstThunk (加载器要写, 所以给新节可写) */
    }

    /* 2) INT/IAT: 只有一个按名导入的符号 + 结束 0。加载器只要求"解析得到", 引擎永远不会调用它。
     *    thunk 要指向 IMAGE_IMPORT_BY_NAME 的**开头** (含 2 字节 hint), 名字从 +2 起;
     *    指到名字上会让加载器把 "Fv" 当 hint, 拿去查 "plAttach" 这个不存在的导出。 */
    {
        unsigned int nb = new_va + (unsigned)name_off;
        wr32(buf + raw_off + ilt_off, nb);
        if (!is32) wr32(buf + raw_off + ilt_off + 4, 0);
        wr32(buf + raw_off + ilt_off + (unsigned)tsize, 0);
        if (!is32) wr32(buf + raw_off + ilt_off + (unsigned)tsize + 4, 0);
        wr32(buf + raw_off + iat_off, nb);
        if (!is32) wr32(buf + raw_off + iat_off + 4, 0);
        wr32(buf + raw_off + iat_off + (unsigned)tsize, 0);
        if (!is32) wr32(buf + raw_off + iat_off + (unsigned)tsize + 4, 0);
    }
    memcpy(buf + raw_off + name_off, "\x00\x00", 2);            /* hint = 0 */
    memcpy(buf + raw_off + name_off + 2, func_name, (size_t)fn_len + 1);
    memcpy(buf + raw_off + dll_off, dll_name, (size_t)dl_len + 1);

    /* 3) 节头 */
    {
        unsigned int s = hdr_off;
        const char *nm = FVPL_SEC_NAME;
        int k;
        for (k = 0; nm[k] && k < 8; k++) buf[s + k] = (unsigned char)nm[k];
        for (; k < 8; k++) buf[s + k] = 0;
        wr32(buf + s + 8,  vsize);
        wr32(buf + s + 12, new_va);
        wr32(buf + s + 16, raw_len);
        wr32(buf + s + 20, raw_off);
        wr32(buf + s + 24, 0);
        wr32(buf + s + 28, 0);
        wr16(buf + s + 32, 0);
        wr16(buf + s + 34, 0);
        wr32(buf + s + 36, FVPL_SCN);
    }

    /* 4) 头部事实: 多一节、导入目录改指新表、SizeOfImage 抬上去。
     *    绑定导入表(Bound Import)描述的是老那张表, 留着会让加载器按陈旧信息比对, 清掉。 */
    wr16(buf + rd32(buf + 0x3C) + 6, n_sec + 1);
    wr32(buf + opt_dir + 1 * 8 + 0, new_va);
    wr32(buf + opt_dir + 1 * 8 + 4, desc_len);
    wr32(buf + opt_dir + 11 * 8 + 0, 0);
    wr32(buf + opt_dir + 11 * 8 + 4, 0);
    wr32(buf + opt + OFF_SIZEOF_IMAGE, al_up(new_va + al_up(vsize, sal), sal));
    wr32(buf + opt + OFF_SIZEOF_INIT_DATA,
         rd32(buf + opt + OFF_SIZEOF_INIT_DATA) + raw_len);

    *size = raw_off + raw_len;
    return FVPL_BIND_OK;
}
