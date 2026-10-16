/* hooks.h — hook DLL 内部接口 */
#ifndef FVPL_HOOKS_H
#define FVPL_HOOKS_H

#include <windows.h>
#include <ole2.h>
#include "iat.h"
#include "../src/config.h"

/* core.c */
extern FvplConfig g_cfg;
void  FvplInitOnce(void);          /* 首次进入任何 hook 时惰性初始化 */
int   FvplReady(void);             /* 初始化完成且配置可用 */
UINT  FvplMapCP(UINT cp);          /* CP_ACP / CP_THREAD_ACP -> 虚拟 ACP */
int   FvplA2W(const char *a, WCHAR *w, int cch);
int   FvplW2A(const WCHAR *w, char *a, int cch);
const WCHAR *FvplSaveDirW(void);   /* 重定向后的存档根 (未启用时为 NULL) */
void  FvplLog(const char *what);   /* 可选诊断, 输出到 fvpl_hook.log */

/* hook_nls.c / hook_io.c / hook_proc.c */
const FvplPatch *FvplTableNls(int *count);
const FvplPatch *FvplTableIo(int *count);
const FvplPatch *FvplTableProc(int *count);

#endif /* FVPL_HOOKS_H */
