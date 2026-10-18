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

/* ---- 供 bindpe.c 复用的解析原语 (同一套布局规则, 避免两处各写一遍而跑偏) ----
 * 全部带边界校验, 任何越界返回 0。 */

/* 定位 NT 头之后的关键位置: is32=TRUE 表示 PE32, opt_dir=数据目录起点,
 * sec_off=节表起点, n_sec=节数量。 */
int pe_layout(const unsigned char *b, unsigned int size, int *is32,
              unsigned int *opt_dir, unsigned int *sec_off, unsigned int *n_sec);

/* RVA -> 文件偏移。落在任何节的"已存在数据"范围外则返回 0。 */
int pe_rva_to_off(const unsigned char *b, unsigned int size, unsigned int rva,
                  unsigned int *out);

/* 第 index 个数据目录的 RVA/大小 (index 1 = 导入表, 11 = 绑定导入表)。 */
int pe_data_dir(const unsigned char *b, unsigned int size, int index,
                unsigned int *rva, unsigned int *len);

#endif /* FVPL_PEINFO_H */
