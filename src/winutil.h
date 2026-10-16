/* winutil.h — Windows 侧共用小工具 (读 ini 文本 / 远进程注入) */
#ifndef FVPL_WINUTIL_H
#define FVPL_WINUTIL_H

#include <windows.h>
#include "fvpl_types.h"

/* 从 dir 读 fvp-launcher.ini 并解成 UTF-16; 文件不存在返回 0 (不是错误) */
int FvplLoadIniText(const WCHAR *dir, fvpl_wchar *out, int outcch);

/* 把 dllpath 注入 hProcess (要求与目标同位数)。成功返回 1, fail_step 收到失败环节。 */
int FvplInjectDll(HANDLE hProcess, DWORD pid, const WCHAR *dllpath, DWORD timeout_ms,
                  ULONG_PTR *module_base, const WCHAR **fail_step);

#endif /* FVPL_WINUTIL_H */
