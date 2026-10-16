/* iat.c — 见 iat.h
 *
 * 为什么只做 IAT 替换: FVP 的文本/文件/GDI 调用全部直接来自游戏主模块的导入表
 * (实测 Sakura.exe 导入 GetACP/GetCPInfo/LCMapStringA/LoadStringA/GetGlyphOutlineA
 * 等, 无 MSVCR, 没有运行时动态取址), 换表项即可截获, 不必往代码段写跳转。
 * 好处: 原 exe 零字节改动、与汉化组自带的 Detours 补丁不冲突、删文件即回滚。
 * 代价: 按序号导入的目标名匹配不到 —— 用第二遍地址匹配兜住。
 *
 * 同一张表可以重复 apply 到多个模块: 已经是 hook 的槽位会被跳过,
 * 各模块导入的都是同一个系统函数地址, 所以 *orig 存一次就够。 */
#include "iat.h"
#include <string.h>

static int ci_ends(const char *s, const char *suffix) {
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return 0;
    return _stricmp(s + (ls - lf), suffix) == 0;
}

static int write_slot(ULONG_PTR *slot, void *hook, void **save) {
    DWORD old;
    *save = (void *)*slot;
    if (*slot == 0) return 0;
    if (!VirtualProtect(slot, sizeof(ULONG_PTR), PAGE_READWRITE, &old)) return 0;
    *slot = (ULONG_PTR)hook;
    VirtualProtect(slot, sizeof(ULONG_PTR), old, &old);
    return 1;
}

int FvplIatApply(HMODULE module, const FvplPatch *table, int count) {
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)module;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *desc;
    ULONG_PTR ordflag = IMAGE_ORDINAL_FLAG;
    int done = 0, pass, slot_total = 0;

    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return -1;
    {
        DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (!rva) return -1;
        desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva);
    }

    for (pass = 0; pass < 2; pass++) {
        IMAGE_IMPORT_DESCRIPTOR *d;
        for (d = desc; d->Name != 0; d++) {
            const char *dllname = (const char *)(base + d->Name);
            IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
            IMAGE_THUNK_DATA *names = d->OriginalFirstThunk ?
                (IMAGE_THUNK_DATA *)(base + d->OriginalFirstThunk) : NULL;
            int i;
            if (pass == 0 && !names) continue;      /* 无名可配, 交给第二遍 */
            for (i = 0; thunk[i].u1.Function; i++) {
                int k;
                slot_total++;
                if (pass == 0 && (names[i].u1.Ordinal & ordflag)) continue;
                for (k = 0; k < count; k++) {
                    const FvplPatch *p = &table[k];
                    if (!p->orig) continue;
                    if ((ULONG_PTR)p->hook == thunk[i].u1.Function) continue;   /* 本模块已打过 */
                    if (pass == 0) {
                        IMAGE_IMPORT_BY_NAME *ibn;
                        if (p->dll && !ci_ends(dllname, p->dll)) continue;
                        ibn = (IMAGE_IMPORT_BY_NAME *)(base + names[i].u1.AddressOfData);
                        if (_stricmp((const char *)ibn->Name, p->func) != 0) continue;
                    } else {
                        HMODULE h;
                        void *real;
                        if (!p->dll || *p->orig) continue;
                        /* 不用 LoadLibraryA: 安装动作可能发生在 DllMain 的 loader lock 里。
                           注入时进程停在入口点之前, 静态导入的 DLL 必然已加载。 */
                        h = GetModuleHandleA(p->dll);
                        if (!h) continue;
                        real = (void *)GetProcAddress(h, p->func);
                        if (!real) continue;
                        if ((ULONG_PTR)real != thunk[i].u1.Function) continue;
                    }
                    if (write_slot(&thunk[i].u1.Function, p->hook, p->orig)) done++;
                    break;
                }
            }
        }
    }
    (void)slot_total;
    return done;
}
