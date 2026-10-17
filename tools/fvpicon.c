/* tools/fvpicon.c — 轻量换图标工具 (只依赖 kernel32/msvcrt)
 *
 * 用法: FVPIcon.exe <目标.exe> <图标.ico> [-o <输出.exe>]
 *   不给 -o: 就地改; 改之前做一次性备份 <目标.exe>.bak (已存在就跳过)
 *   给 -o  : 先 CopyFileW(目标, 输出, FALSE), 再改输出文件
 *
 * 做法: 解出 .ico 的每张图 -> 写进 exe 的 RT_ICON (ID = 1..count);
 *       再拼一份 RT_GROUP_ICON (ID 1) 指回这些 ID;
 *       写之前先把旧图标组 (RT_GROUP_ICON 1 及其引用的 RT_ICON) 删掉。
 * 备注: 资源语言一律用 0 (neutral), 与系统"改图标"工具一致。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

/* 读目标文件里现有的图标组, 收集它引用的 RT_ICON ID (供删除)。
 * 返回 ID 个数; 文件不是 PE / 没有图标组都返回 0 (不是错误)。
 * *found 置 1 表示确实存在 RT_GROUP_ICON(1) 资源; 只有这时才允许删旧资源 ——
 * 对不存在的资源调 UpdateResourceW(…, NULL, 0) 会把整个更新会话搞坏
 * (后续 UpdateResourceW 全以 ERROR_INTERNAL_ERROR(1359) 失败)。 */
static int collect_old_icon_ids(const WCHAR *path, WORD *ids, int cap, int *found) {
    HMODULE h = LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    int nold = 0;
    *found = 0;
    if (!h) return 0;
    {
        HRSRC r = FindResourceW(h, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
        if (r) {
            *found = 1;
            DWORD sz = SizeofResource(h, r);
            HGLOBAL g = LoadResource(h, r);
            void *p = g ? LockResource(g) : NULL;
            if (p && sz >= sizeof(IcoHdr) && ((IcoHdr *)p)->type == 1) {
                int n = (int)((IcoHdr *)p)->count;
                if (((DWORD)(sizeof(IcoHdr) + (size_t)n * sizeof(GrpEnt))) <= sz) {
                    const GrpEnt *ge = (const GrpEnt *)((const char *)p + sizeof(IcoHdr));
                    for (int i = 0; i < n && nold < cap; i++) ids[nold++] = ge[i].nID;
                }
            }
        }
    }
    FreeLibrary(h);
    return nold;
}

/* 把 buf 里的 .ico 全部写进 work 的 RT_ICON + RT_GROUP_ICON, 顺便删掉旧图标组。
 * 成功返回 1; 失败返回 0 并把人类可读原因写进 *why。 */
static int replace_icons(const WCHAR *work, const unsigned char *buf,
                         const IcoEnt *ent, int count, const WCHAR **why) {
    static WCHAR ebuf[128];
    WORD old_ids[MAX_ICONS];
    int found = 0;
    int nold = collect_old_icon_ids(work, old_ids, MAX_ICONS, &found);
    HANDLE hu;
    unsigned char *gb;
    int gsize, i;
    int ok = 1;
    DWORD err = 0;

    hu = BeginUpdateResourceW(work, FALSE);
    if (!hu) { *why = L"打不开目标文件（只读、被占用或不是有效的 exe）"; return 0; }

    /* 先删旧: 组所引用的每个 RT_ICON, 再删组本身 (语言 0)。
     * 注意: 只有确实存在旧图标组时才删 —— 删除不存在的资源会破坏更新会话。 */
    if (found) {
        for (i = 0; i < nold; i++)
            UpdateResourceW(hu, RT_ICON, MAKEINTRESOURCEW(old_ids[i]), 0, NULL, 0);
        UpdateResourceW(hu, RT_GROUP_ICON, MAKEINTRESOURCEW(1), 0, NULL, 0);
    }

    /* 写每张图: ID = 1..count */
    for (i = 0; i < count && ok; i++) {
        if (!UpdateResourceW(hu, RT_ICON, MAKEINTRESOURCEW(i + 1), 0,
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
            if (!UpdateResourceW(hu, RT_GROUP_ICON, MAKEINTRESOURCEW(1), 0, gb, (DWORD)gsize)) {
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
