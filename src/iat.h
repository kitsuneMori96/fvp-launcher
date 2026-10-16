/* iat.h — 只改本进程导入表的 hook 安装器 (不写代码段, 不做 inline detour) */
#ifndef FVPL_IAT_H
#define FVPL_IAT_H

#include <windows.h>

typedef struct {
    const char *dll;     /* 大小写无关, 允许只写后缀 (兼容 api-ms 别名) */
    const char *func;    /* 按名匹配的导入函数 */
    void       *hook;
    void      **orig;    /* 收原实现地址, 供 hook 内透传 */
} FvplPatch;

/* 两遍安装: 先按导入名匹配; 再按函数地址匹配 (覆盖按序号导入的目标)。
 * 返回成功替换的槽位数, 负数表示模块头解析失败。 */
int FvplIatApply(HMODULE module, const FvplPatch *table, int count);

#endif /* FVPL_IAT_H */
