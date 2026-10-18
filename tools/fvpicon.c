/* tools/fvpicon.c — 轻量换图标工具 (只依赖 kernel32/msvcrt)
 *
 * 用法: FVPIcon.exe <目标.exe> <图标.ico> [-o <输出.exe>]
 *   不给 -o: 就地改; 改之前做一次性备份 <目标.exe>.bak (已存在就跳过)
 *   给 -o  : 先 CopyFileW(目标, 输出, FALSE), 再改输出文件
 *
 * 做法: 解出 .ico 的每张图 -> 写进 exe 的 RT_ICON (ID = 1..count);
 *       再拼一份 RT_GROUP_ICON 指回这些 ID, 沿用目标原有那组的 ID 与语言
 *       (引擎代码是按自己的 ID 取图标的, 写成 ID 1 会让窗口图标退回系统默认);
 *       写之前把 exe 里**所有**旧图标资源 (任意 ID、任意语言) 全删掉。
 * 备注: 只当 ID 1 / 语言 0 的话旧组删不掉 (日文引擎那组是 ID 104 / 语言 1041),
 *       换完 exe 里会躺着两组图标。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../src/bindpe.h"

/* 磁盘上的图标结构都是紧凑排布的, 用 pack(1) 避免编译器插入对齐填充 */
#pragma pack(push, 1)
typedef struct { WORD reserved, type, count; } IcoHdr;                 /* ICONDIR */
typedef struct {                                                        /* ICONDIRENTRY */
    BYTE bWidth, bHeight, bColorCount, bReserved;
    WORD wPlanes, wBitCount;
    DWORD dwBytesInRes, dwImageOffset;
} IcoEnt;
typedef struct {                                                        /* GRPICONDIRENTRY */
    BYTE bWidth, bHeight, bColorCount, bReserved;
    WORD wPlanes, wBitCount;
    DWORD dwBytesInRes;
    WORD nID;
} GrpEnt;
#pragma pack(pop)

#define MAX_ICONS 256

/* 控制台按 UTF-8 输出 (中文提示 + 非 ASCII 路径都能正确显示/落盘) */
static void outw(const WCHAR *fmt, ...) {
    WCHAR w[2048];
    char m[8192];
    va_list ap;
    int n;
    va_start(ap, fmt);
    _vsnwprintf(w, 2048, fmt, ap);
    va_end(ap);
    w[2047] = 0;
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, m, (int)sizeof(m), NULL, NULL);
    if (n > 0) fwrite(m, 1, (size_t)(n - 1), stdout);
    fflush(stdout);
}

static unsigned char *read_file(const WCHAR *path, DWORD *size) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER li;
    unsigned char *buf;
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 ||
        li.QuadPart > (LONGLONG)(64 * 1024 * 1024)) {
        CloseHandle(h);
        return NULL;
    }
    buf = (unsigned char *)malloc((size_t)li.QuadPart);
    if (!buf) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, buf, (DWORD)li.QuadPart, &got, NULL) ||
        got != (DWORD)li.QuadPart) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    *size = got;
    return buf;
}

/* 枚举结果: (资源 ID, 语言) 成对收集。具名资源不收集 (图标组都按 ID 寻址)。 */
#define RES_CAP 512
typedef struct { WORD id[RES_CAP]; WORD lang[RES_CAP]; int n; int overflow; } ResList;

static BOOL CALLBACK add_lang(HMODULE h, LPCWSTR type, LPCWSTR name, WORD lang, LONG_PTR l) {
    ResList *L = (ResList *)l;
    int i;
    (void)h; (void)type;
    if (!IS_INTRESOURCE(name)) return TRUE;
    for (i = 0; i < L->n; i++)
        if (L->id[i] == (WORD)(UINT_PTR)name && L->lang[i] == lang) return TRUE;
    if (L->n >= RES_CAP) { L->overflow = 1; return TRUE; }
    L->id[L->n] = (WORD)(UINT_PTR)name;
    L->lang[L->n] = lang;
    L->n++;
    return TRUE;
}

static BOOL CALLBACK add_id(HMODULE h, LPCWSTR type, LPWSTR name, LONG_PTR l) {
    (void)type;
    if (!IS_INTRESOURCE(name)) return TRUE;
    EnumResourceLanguagesW(h, type, name, add_lang, l);
    return TRUE;
}

static void list_all(HMODULE h, LPCWSTR type, ResList *L) {
    L->n = 0;
    L->overflow = 0;
    EnumResourceNamesW(h, type, add_id, (LONG_PTR)L);
}

/* 列出目标 exe 里现有的全部图标资源 (任意 ID / 任意语言)。
 * 文件不是 PE / 一个图标都没有 = 空列表, 不算错误。
 * 溢出 (图标条目多到 512 条) 才算失败: 那样会删不干净, 宁可不动。 */
static int collect_icons(const WCHAR *path, ResList *grp, ResList *ico, const WCHAR **why) {
    HMODULE h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!h) return 1;
    list_all(h, RT_GROUP_ICON, grp);
    list_all(h, RT_ICON, ico);
    FreeLibrary(h);
    if (grp->overflow || ico->overflow) {
        *why = L"这个 exe 的图标条目多到处理不完（超过 512 条），为免留下半套旧图标没有改动";
        return 0;
    }
    return 1;
}

/* 把 buf 里的 .ico 全部写进 work 的 RT_ICON + RT_GROUP_ICON, 顺便删掉旧图标组。
 * 成功返回 1; 失败返回 0 并把人类可读原因写进 *why。 */
static int replace_icons(const WCHAR *work, const unsigned char *buf,
                         const IcoEnt *ent, int count, const WCHAR **why) {
    static WCHAR ebuf[128];
    ResList grp, ico;
    WORD write_lang = 0, write_id = 1;
    HANDLE hu;
    unsigned char *gb;
    int gsize, i;
    int ok = 1;
    DWORD err = 0;

    if (!collect_icons(work, &grp, &ico, why)) return 0;
    if (grp.n > 0) {
        /* 跟着目标原有那组的 ID 和语言写。语言是为了不混进第三种语言; ID 更要紧 ——
         * 引擎代码里是按自己那个 ID LoadIcon 的 (樱花/FAVORITE 是 104), 写成 ID 1
         * 等于把它要的那组删了: 文件图标是新的, 窗口标题栏却退回系统默认图标。
         * 旧组若被删的是 ID 1 (我们自己的启动器), 而新图占用了 1..count 这些 RT_ICON
         * 号, 就把新组挪到 count+1, 总之不与残留条目撞号。 */
        write_lang = grp.lang[0];
        write_id = grp.id[0];
        if (write_id >= 1 && write_id <= (WORD)count) write_id = (WORD)(count + 1);
    }

    hu = BeginUpdateResourceW(work, FALSE);
    if (!hu) { *why = L"打不开目标文件（只读、被占用或不是有效的 exe）"; return 0; }

    /* 先删旧: 文件里**所有**图标组和图标条目 (任意 ID、任意语言)。
     * 只删枚举到确实存在的 —— 对不存在的资源调 UpdateResourceW(…, NULL, 0)
     * 会把整个更新会话搞坏 (后续写入全以 ERROR_INTERNAL_ERROR(1359) 失败)。
     * 以前只删 "RT_GROUP_ICON(1) + 语言 0", 而日文引擎那组是 ID 104 / 语言 1041,
     * 结果旧组根本没删掉, 换完图标 exe 里躺着两组, 资源管理器还显示旧的。 */
    for (i = 0; i < ico.n; i++)
        UpdateResourceW(hu, RT_ICON, MAKEINTRESOURCEW(ico.id[i]), ico.lang[i], NULL, 0);
    for (i = 0; i < grp.n; i++)
        UpdateResourceW(hu, RT_GROUP_ICON, MAKEINTRESOURCEW(grp.id[i]), grp.lang[i], NULL, 0);

    /* 写每张图: ID = 1..count */
    for (i = 0; i < count && ok; i++) {
        if (!UpdateResourceW(hu, RT_ICON, MAKEINTRESOURCEW(i + 1), write_lang,
                             (LPVOID)(buf + ent[i].dwImageOffset),
                             ent[i].dwBytesInRes)) {
            ok = 0;
            err = GetLastError();
        }
    }

    /* 再写组: GRPICONDIR + count × GRPICONDIRENTRY, nID = i+1 */
    if (ok) {
        gsize = (int)(sizeof(IcoHdr) + (size_t)count * sizeof(GrpEnt));
        gb = (unsigned char *)malloc((size_t)gsize);
        if (!gb) {
            ok = 0;
            *why = L"内存不足";
        } else {
            IcoHdr gh;
            gh.reserved = 0;
            gh.type = 1;
            gh.count = (WORD)count;
            memcpy(gb, &gh, sizeof(gh));
            for (i = 0; i < count; i++) {
                GrpEnt ge;
                ge.bWidth = ent[i].bWidth;
                ge.bHeight = ent[i].bHeight;
                ge.bColorCount = ent[i].bColorCount;
                ge.bReserved = ent[i].bReserved;
                ge.wPlanes = ent[i].wPlanes;
                ge.wBitCount = ent[i].wBitCount;
                ge.dwBytesInRes = ent[i].dwBytesInRes;
                ge.nID = (WORD)(i + 1);
                memcpy(gb + sizeof(gh) + (size_t)i * sizeof(ge), &ge, sizeof(ge));
            }
            if (!UpdateResourceW(hu, RT_GROUP_ICON, MAKEINTRESOURCEW(write_id), write_lang,
                                 gb, (DWORD)gsize)) {
                ok = 0;
                err = GetLastError();
            }
            free(gb);
        }
    }

    if (!ok && err) {
        _snwprintf(ebuf, 128, L"写入图标资源失败 (GetLastError=%lu)", (unsigned long)err);
        ebuf[127] = 0;
        *why = ebuf;
    }

    /* 失败就丢弃全部改动 (TRUE = discard) */
    if (!EndUpdateResourceW(hu, !ok) || !ok) {
        if (ok) *why = L"保存目标文件失败";
        return 0;
    }
    return 1;
}

int wmain(int argc, wchar_t *argv[]) {
    const WCHAR *target, *icopath, *outpath = NULL, *work, *why = L"未知原因";
    WCHAR bak[1024];
    unsigned char *buf;
    DWORD size = 0;
    IcoEnt ent[MAX_ICONS];
    int count, i;

    SetConsoleOutputCP(CP_UTF8);

    for (i = 3; i < argc; i++)
        if (lstrcmpW(argv[i], L"-o") == 0 && i + 1 < argc) outpath = argv[++i];

    if (argc < 3 || lstrcmpW(argv[1], L"-h") == 0 || lstrcmpW(argv[1], L"--help") == 0) {
        outw(L"用法: FVPIcon.exe <目标.exe> <图标.ico> [-o 输出.exe]\n");
        return argc < 3 ? 1 : 0;
    }

    target = argv[1];
    icopath = argv[2];

    buf = read_file(icopath, &size);
    if (!buf) { outw(L"换了失败: %s\n", L"读不到图标文件"); return 1; }

    /* 解析 ICONDIR / ICONDIRENTRY, 并校验每张图的数据都落在文件内 */
    if (size < sizeof(IcoHdr)) { outw(L"换了失败: %s\n", L"图标文件格式不对"); free(buf); return 1; }
    {
        const IcoHdr *hdr = (const IcoHdr *)buf;
        if (hdr->reserved != 0 || hdr->type != 1 || hdr->count == 0 ||
            hdr->count > MAX_ICONS ||
            (DWORD)sizeof(IcoHdr) + (DWORD)hdr->count * sizeof(IcoEnt) > size) {
            outw(L"换了失败: %s\n", L"图标文件格式不对");
            free(buf);
            return 1;
        }
        count = (int)hdr->count;
        memcpy(ent, buf + sizeof(IcoHdr), (size_t)count * sizeof(IcoEnt));
        for (i = 0; i < count; i++) {
            if (ent[i].dwImageOffset > size ||
                ent[i].dwBytesInRes == 0 ||
                ent[i].dwImageOffset + ent[i].dwBytesInRes > size) {
                outw(L"换了失败: %s\n", L"图标里的图片数据越界");
                free(buf);
                return 1;
            }
        }
    }

    /* 打印每张图的尺寸 (bWidth==0 表示 256) */
    {
        WCHAR sizes[2048];
        int sp = 0;
        for (i = 0; i < count; i++) {
            int w = ent[i].bWidth ? ent[i].bWidth : 256;
            int hh = ent[i].bHeight ? ent[i].bHeight : 256;
            sp += _snwprintf(sizes + sp, 2048 - sp, i ? L", %dx%d" : L"%dx%d", w, hh);
        }
        outw(L"图标: %d 张 (%s)\n", count, sizes);
    }

    /* B 档绑过的 exe 不能直接换图标: 资源重写会重排节地址, 绑好的导入表会指向
     * 失效的 RVA, 游戏 exe 当场起不来。让它先撤销, 换完图标再绑回去。 */
    {
        DWORD tsize = 0;
        unsigned char *tbuf = read_file(target, &tsize);
        if (tbuf && pe_bind_has_marker(tbuf, tsize)) {
            free(tbuf);
            outw(L"换了失败: 这个 exe 已经做过 B 档绑定, 现在换图标会把它的导入表挪位, "
                 L"游戏会打不开。\n"
                 L"请按这个顺序来:\n"
                 L"  1) FVPBind.exe \"%s\" -restore\n"
                 L"  2) FVPIcon.exe \"%s\" \"%s\"\n"
                 L"  3) FVPBind.exe \"%s\"\n",
                 target, target, icopath, target);
            free(buf);
            return 1;
        }
        free(tbuf);
    }

    /* 备份 / 复制到输出 */
    if (outpath) {
        if (!CopyFileW(target, outpath, FALSE)) {
            outw(L"换了失败: %s\n", L"复制到输出文件失败");
            free(buf);
            return 1;
        }
        work = outpath;
    } else {
        work = target;
        _snwprintf(bak, 1024, L"%s.bak", target);
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES) {
            if (CopyFileW(target, bak, FALSE))
                outw(L"备份: %s\n", bak);
            else
                outw(L"备份失败（继续就地改）: %s\n", bak);
        }
    }

    if (!replace_icons(work, buf, ent, count, &why)) {
        outw(L"换了失败: %s\n", why);
        free(buf);
        return 1;
    }

    outw(L"已换图标: %s\n", work);
    free(buf);
    return 0;
}
