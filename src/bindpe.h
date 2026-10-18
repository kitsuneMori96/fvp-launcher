/* bindpe.h — 往目标 exe 的导入表里加一条「进程启动就加载 hook DLL」的依赖
 *
 * 为什么要这个: B 档分发只想让作品目录里看起来就是一个原生游戏 exe, 不想
 * 多出 FVPLauncher.exe。把 fvpl_hook.dll 写进引擎自己的导入表后, Windows 在
 * 启动进程时就会把钩子带进来, 存档重定向/码页/换窗口图标全部照常生效。
 * 代价很明确: exe 本体被改了 (与「只换导入表、删掉即回滚」的原则不同), 所以
 * 工具就地改之前一定先留 .bak, 并且提供 -restore。
 *
 * 本模块是纯逻辑 (不 include windows.h), 直接在内存镜像上操作, 可在 host 侧单测。
 */
#ifndef FVPL_BINDPE_H
#define FVPL_BINDPE_H

#include "fvpl_types.h"

#define FVPL_BIND_FAIL     0   /* err 收到人类可读的原因, 缓冲区未被改动 */
#define FVPL_BIND_OK       1   /* 已写入新的导入表 */
#define FVPL_BIND_ALREADY  2   /* 目标已经绑过这条依赖, 未做任何改动 */

/* buf = 整个 exe 文件内容, *size = 当前长度, cap = buf 总容量。
 * 成功(OK)时把新节追加到 buf 并更新 *size; 容量不足一律判失败。
 * dll_name / func_name 只允许 ASCII 文件名安全字符 (会写进导入表被加载器当名字用)。 */
int pe_bind_apply(unsigned char *buf, unsigned int cap, unsigned int *size,
                  const char *dll_name, const char *func_name,
                  char *err, int errcch);

/* 只读体检, 供 -check 与单测使用。任一输出指针可为 NULL。
 * 返回 1 = 这个 exe 可以绑 (位数/导入表/头部空位都合格), 0 = 不能。 */
int pe_bind_inspect(const unsigned char *buf, unsigned int size, const char *dll_name,
                    int *is32, int *n_dlls, int *header_slack, int *already);

/* 这个 exe 是否已被 FVPBind 绑过 (看 .fvpl 节名, 与绑的 DLL 名无关)。
 * 换图标那类会重排节地址的改写必须先挡住它, 否则绑好的导入表会指向失效的 RVA。
 * 只读, 畸形文件返回 0。 */
int pe_bind_has_marker(const unsigned char *buf, unsigned int size);

#endif /* FVPL_BINDPE_H */
