/* fvpl_types.h — 跨 host/Windows 的公共类型 (纯逻辑模块不得 include windows.h) */
#ifndef FVPL_TYPES_H
#define FVPL_TYPES_H

typedef unsigned short fvpl_wchar;
typedef unsigned int   fvpl_uint;
typedef int            fvpl_int;
typedef unsigned int   fvpl_bool;

#define FVPL_MAX_PATH 260
#define FVPL_INI_SECCAP 32
#define FVPL_INI_KEYCAP 64
#define FVPL_INI_VALCAP 512
#define FVPL_INI_MAXENTRY 96

#endif /* FVPL_TYPES_H */
