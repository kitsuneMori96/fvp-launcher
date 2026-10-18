/* tools/fvpbind.c — 把钩子 DLL 绑进游戏主程序自己的导入表 (B 档: 目录里只留一个 exe)
 *
 * 用法: FVPBind.exe <目标.exe> [-o 输出.exe] [-dll 钩子DLL名] [-check] [-restore]
 *   不给 -o : 就地改; 动手前先把 <目标.exe> 备份成 <目标.exe>.bak (留不下备份就不改)
 *   给 -o   : 复制一份到输出文件再改, 原文件不动
 *   -check  : 只体检不改 (位数 / 导入表几座 / 头部余量 / 绑过没有)
 *   -restore: 用 .bak 覆盖回来, 等于把这档用法撤销
 *
 * 改完之后目录里是「游戏.exe + fvpl_hook.dll + fvp-launcher.ini」: 双击游戏 exe 就有
 * 存档隔离和文字编码纠正, 不再需要 FVPLauncher.exe。
 * 注意这一步真的动了 exe 本体 (与启动器"只改内存里的导入表"不同), 所以备份是强制的。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "../src/bindpe.h"
#include "../src/peinfo.h"

#define GROW (64u * 1024u)          /* 新节最多要一节的对齐块 + 节头, 64KB 足够富余 */

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

/* bindpe 的原因是 UTF-8 的 char* (纯逻辑模块不能带 windows.h), 打出来前转回 WCHAR */
static void outs(const char *utf8) {
    WCHAR w[512];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 511) > 0) {
        w[511] = 0;
        outw(L"%s\n", w);
    } else {
        outw(L"(有句说明显示不出来, 详见 fvp-launcher 文档)\n");
    }
}

static int utf8_of(const WCHAR *w, char *out, int cap) {
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL);
}

/* 读出整个文件, 并额外留出 extra 字节给新节用 (返回的容量 = *size + extra) */
static unsigned char *read_whole(const WCHAR *path, unsigned int *size, unsigned int extra,
                                 unsigned int *cap_out) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER li;
    unsigned char *buf;
    DWORD got = 0;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 ||
        li.QuadPart > (LONGLONG)(512u * 1024u * 1024u)) {
        CloseHandle(h);
        return NULL;
    }
    *size = (unsigned int)li.QuadPart;
    *cap_out = (unsigned int)li.QuadPart + extra;
    buf = (unsigned char *)calloc(*cap_out, 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (!ReadFile(h, buf, *size, &got, NULL) || got != *size) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    return buf;
}

static int write_whole(const WCHAR *path, const unsigned char *buf, unsigned int size) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    BOOL ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, buf, size, &put, NULL) && put == size;
    CloseHandle(h);
    return ok;
}

static void dir_of(const WCHAR *path, WCHAR *out, int cch) {
    int i, n = lstrlenW(path);
    if (n >= cch) n = cch - 1;
    lstrcpynW(out, path, n + 1);
    i = n;
    while (i > 0 && out[i - 1] != L'\\' && out[i - 1] != L'/') i--;
    out[i] = 0;
}

static void usage(void) {
    outw(L"用法: FVPBind.exe <游戏主程序.exe> [-o 输出.exe] [-dll 钩子DLL名] [-check] [-restore]\n");
    outw(L"  绑好之后目录里是「游戏.exe + 钩子DLL + fvp-launcher.ini」, 双击游戏 exe 就生效,\n");
    outw(L"  不用再带 FVPLauncher.exe。就地改会自动留 .bak; -restore 撤销。\n");
}

int wmain(int argc, wchar_t *argv[]) {
    const WCHAR *target = NULL, *outpath = NULL;
    const WCHAR *dllname = L"fvpl_hook.dll";
    int check_only = 0, do_restore = 0, i;
    WCHAR work[1024], bak[1024], dir[1024], dllpath[1024];
    char dll_a[80], err[256];
    unsigned char *buf;
    unsigned int size = 0, cap;
    int is32 = 0, ndll = 0, slack = 0, already = 0;
    DWORD attr;

    SetConsoleOutputCP(CP_UTF8);

    for (i = 1; i < argc; i++) {
        if (lstrcmpW(argv[i], L"-o") == 0 && i + 1 < argc) outpath = argv[++i];
        else if (lstrcmpW(argv[i], L"-dll") == 0 && i + 1 < argc) dllname = argv[++i];
        else if (lstrcmpW(argv[i], L"-check") == 0) check_only = 1;
        else if (lstrcmpW(argv[i], L"-restore") == 0) do_restore = 1;
        else if (lstrcmpW(argv[i], L"-h") == 0 || lstrcmpW(argv[i], L"--help") == 0) {
            usage();
            return 0;
        } else if (target == NULL) target = argv[i];
        else { outw(L"没看懂这个参数: %s\n", argv[i]); usage(); return 1; }
    }
    if (target == NULL) { usage(); return 1; }

    lstrcpynW(work, outpath ? outpath : target, 1024);
    _snwprintf(bak, 1024, L"%s.bak", target);
    bak[1023] = 0;

    if (do_restore) {
        attr = GetFileAttributesW(bak);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            outw(L"没有找到备份文件 %s\n没有备份就不撤销, 免得把没改过的程序弄坏。\n", bak);
            return 1;
        }
        if (!CopyFileW(bak, target, FALSE)) {
            outw(L"撤销失败: 备份没能覆盖回 %s (游戏正在运行? 文件被占用?)\n", target);
            return 1;
        }
        outw(L"已撤销: %s 恢复成绑定前的样子。\n", target);
        return 0;
    }

    if (utf8_of(dllname, dll_a, (int)sizeof(dll_a)) <= 0) {
        outw(L"钩子 DLL 的名字太长了。\n");
        return 1;
    }

    cap = 0;
    buf = read_whole(target, &size, GROW, &cap);
    if (buf == NULL) {
        outw(L"读不了 %s (文件不存在、正在运行, 或大到不合理)\n", target);
        return 1;
    }

    if (!pe_bind_inspect(buf, size, dll_a, &is32, &ndll, &slack, &already)) {
        outs(err);
        outw(L"-> %s 不能这么绑, 请继续用 FVPLauncher.exe 启动。\n", target);
        free(buf);
        return 1;
    }
    outw(L"%s: %s 位, 导入 %d 座 DLL, 节表后面还有 %d 字节空位\n",
         target, is32 ? L"32" : L"64", ndll, slack);
    if (check_only) {
        outw(already ? L"体检: 已经绑过钩子了, 双击游戏 exe 就会加载。\n"
                     : L"体检: 可以绑。执行不带 -check 的命令即可写入。\n");
        free(buf);
        return 0;
    }
    if (already) {
        outw(L"这个 exe 已经绑过钩子了, 没有再改。\n");
        free(buf);
        return 0;
    }

    if (pe_bind_apply(buf, cap, &size, dll_a, "FvplAttach", err, (int)sizeof(err))
            != FVPL_BIND_OK) {
        outs(err);
        free(buf);
        return 1;
    }

    if (outpath) {
        if (!CopyFileW(target, outpath, FALSE)) {
            outw(L"复制到输出文件失败 (目标目录不存在或已有同名文件被占用)\n");
            free(buf);
            return 1;
        }
    } else {
        /* 这次改的是 exe 本体, 留不下备份就绝不写回 */
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES) {
            if (!CopyFileW(target, bak, FALSE)) {
                outw(L"备份失败, 所以没有改动 %s\n请把游戏做成一份副本再来绑, 或先用 -o 输出到新文件。\n",
                     target);
                free(buf);
                return 1;
            }
            outw(L"备份: %s\n", bak);
        } else {
            outw(L"已有备份 %s, 本次直接覆盖 exe\n", bak);
        }
    }

    if (!write_whole(work, buf, size)) {
        outw(L"写入 %s 失败 (文件被占用? 没有写权限?)\n", work);
        free(buf);
        return 1;
    }
    free(buf);
    outw(L"已绑定: 启动这个 exe 会自动加载 %s\n", dllname);

    dir_of(work, dir, 1024);
    _snwprintf(dllpath, 1024, L"%s%s", dir, dllname);
    dllpath[1023] = 0;
    if (GetFileAttributesW(dllpath) == INVALID_FILE_ATTRIBUTES) {
        outw(L"还差一步: 这个目录里没有 %s\n把它复制到 %s 旁边, 否则游戏会报错启动不了。\n",
             dllname, work);
    }
    outw(L"要撤销就运行: FVPBind.exe \"%s\" -restore\n", work);
    return 0;
}
