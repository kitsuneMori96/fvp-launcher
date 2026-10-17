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
    fvpl_uint  codepage;        /* 系统/路径层: 虚拟 ACP, 引擎的 ANSI 路径按此解释 */
    fvpl_uint  text_codepage;   /* 文本层: 0 = 与 codepage 相同 (不做文本层转换);
                                 * 显式值 = 游戏台词/标题所用码页 (日文原版 932)。
                                 * 只影响文本/字体/字形 API, GetACP 与文件路径仍用 codepage */
    fvpl_uint  font_charset;    /* GDI charset, 128=SHIFTJIS 134=GB2312 136=Big5 */
    fvpl_wchar font_face[64];   /* 非空 = 连字型一起强制 (最多 31 字符) */
    fvpl_wchar icon[FVPL_MAX_PATH]; /* 非空 = 换成这个 .ico / .exe 的图标 (已解析为绝对路径) */
    int        virtualize_files;
    int        virtualize_ui;
    fvpl_uint  lcid;            /* 0 = 不动 CompareStringA 的 locale */
} FvplConfig;

/* 宿主进程真实 ANSI 码页 (Windows 侧用 FvplHostAcp() 取, 见 winutil.h)。
 * 不写 [nls] 时 codepage/font_charset 就按它推导 —— 纯逻辑模块不能 include
 * windows.h, 所以由调用方在 cfg_build 之前注入。0 = 保持内置默认 (932)。 */
void cfg_set_host_acp(fvpl_uint cp);

int  cfg_ini_parse(const fvpl_wchar *text, FvplIni *ini);
const fvpl_wchar *cfg_ini_get(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key);
fvpl_int cfg_ini_int(const FvplIni *ini, const fvpl_wchar *sec, const fvpl_wchar *key, fvpl_int dflt);

/* basedir = 游戏目录. err 收到人类可读的失败原因 (可为 NULL). */
int  cfg_build(const FvplIni *ini, const fvpl_wchar *basedir, FvplConfig *out,
               fvpl_wchar *err, int errcch);

#endif /* FVPL_CONFIG_H */
