/* peinfo.h — 只读 PE 解析: 判断目标位数与导入表内容 (host 可拿真 exe 单测) */
#ifndef FVPL_PEINFO_H
#define FVPL_PEINFO_H

#include "fvpl_types.h"

#define FVPL_MACHINE_I386   0x014c
#define FVPL_MACHINE_AMD64  0x8664

int pe_machine(const unsigned char *blob, unsigned int size);
int pe_has_import(const unsigned char *blob, unsigned int size,
                  const char *dll_name, const char *func_name);

/* 从 PE 资源目录里取语言 id (推断游戏文本码页用)。
 * 优先 RT_VERSION(type 16) 子树的第三层; 没有就取任意类型下第一个语言项。
 * 全程边界校验, 畸形文件不会崩; 找不到返回 0。 */
int pe_resource_lang(const unsigned char *blob, unsigned int size);

#endif /* FVPL_PEINFO_H */
