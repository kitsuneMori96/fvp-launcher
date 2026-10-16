/* peinfo.h — 只读 PE 解析: 判断目标位数与导入表内容 (host 可拿真 exe 单测) */
#ifndef FVPL_PEINFO_H
#define FVPL_PEINFO_H

#include "fvpl_types.h"

#define FVPL_MACHINE_I386   0x014c
#define FVPL_MACHINE_AMD64  0x8664

int pe_machine(const unsigned char *blob, unsigned int size);
int pe_has_import(const unsigned char *blob, unsigned int size,
                  const char *dll_name, const char *func_name);

#endif /* FVPL_PEINFO_H */
