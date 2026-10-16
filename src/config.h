/* config.h — fvp-launcher.ini 解析与配置推导 (纯逻辑, host 可单测) */
#ifndef FVPL_CONFIG_H
#define FVPL_CONFIG_H

#include "fvpl_types.h"

#define FVPL_INI_NAME L"fvp-launcher.ini"

typedef struct {
    fvpl_wchar sec[FVPL_INI_SECCAP];
    fvpl_wchar key[FVPL_INI_KEYCAP];
    fvpl_wchar val[FVPL_INI_VALCAP];
} FvplEntry;

typedef struct {
    FvplEntry ent[FVPL_INI_MAXENTRY];
    int n;
} FvplIni;

typedef struct {
    /* [launcher] */
    fvpl_wchar target[FVPL_MAX_PATH];   /* 空 = 按导入表自动探测 */
    fvpl_wchar params[FVPL_MAX_PATH * 2];
    fvpl_wchar workdir[FVPL_MAX_PATH];  /* 空 = basedir */
    int        inject_children;         /* 汉化版链式启动时把 hook 带进子进程 */

    /* [save] */
    int        save_redirect;
    fvpl_wchar save_path[FVPL_MAX_PATH];  /* 已解析为绝对路径 */
    int        save_migrate;
    fvpl_wchar migrate_names[256];        /* ';' 分隔目录名, "*" = Documents 下全部 */
    fvpl_uint  folder_ids[8];
    int        n_folder_ids;

    /* [nls] */
    fvpl_uint  codepage;        /* 虚拟 ACP: 引擎 ANSI 字节串按此解释 */
    fvpl_uint  font_charset;    /* GDI charset, 128=SHIFTJIS 134=GB2312 136=Big5 */
    int        virtualize_files;
    int        virtualize_ui;
    fvpl_uint  lcid;            /* 0 = 不动 CompareStringA 的 locale */
} FvplConfig;

int  cfg_ini_parse(const fvpl_wchar *text, FvplIni *ini);
const fvpl_wchar *cfg_ini_get(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key);
fvpl_int cfg_ini_int(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key, fvpl_int dflt);

/* basedir = 游戏目录. err 收到人类可读的失败原因 (可为 NULL). */
int  cfg_build(const FvplIni *ini, const fvpl_wchar *basedir, FvplConfig *out,
               fvpl_wchar *err, int errcch);

#endif /* FVPL_CONFIG_H */
