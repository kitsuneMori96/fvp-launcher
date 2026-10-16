/* wizard.h — 首次设定 / 设置界面 (没有 ini 时弹一次) */
#ifndef FVPL_WIZARD_H
#define FVPL_WIZARD_H

#include <windows.h>
#include "config.h"
#include "pathutil.h"

#define WIZ_MAX_CAND 16

/* 首次设定界面的候选主程序。
 * path = 完整路径; cp = 该 exe 资源语言推出来的游戏文本码页 (0 = 认不出);
 * is_fvp = 导入表指纹认出来是 FVP 引擎 (否则可能只是汉化/中文版启动器)。 */
typedef struct {
    fvpl_wchar path[FVPL_MAX_PATH];
    int        cp;
    int        is_fvp;
} WizCand;

/* 弹出设定界面。basedir = 启动器目录(ini 写在这里); cands/n = 目录里扫到的候选;
 * preselect = 预选中的完整路径(可为 NULL); cfg = 当前配置(用来预填, 返回时按用户选择更新)。
 * 返回 1 = 用户点了“开始游戏”(cfg 已更新, ini 已写); 0 = 取消。 */
int WizardRun(const fvpl_wchar *basedir, const WizCand *cands, int n,
              const fvpl_wchar *preselect, FvplConfig *cfg);

/* 按 cfg 生成 fvp-launcher.ini(UTF-8 带 BOM)。lang_idx = 语言组合框索引(0 = 自动)。
 * 成功返回 1。 */
int WizardWriteIni(const fvpl_wchar *basedir, const FvplConfig *cfg, int lang_idx);

#endif /* FVPL_WIZARD_H */
