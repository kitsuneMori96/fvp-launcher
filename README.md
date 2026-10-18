# FVP 通用启动器 (FVP Launcher)

启动 FAVORITE 引擎 (FVP) 游戏的主程序, 并把存档根目录重定向到游戏目录或任意指定目录;
同时用配置里的代码页虚拟化整个进程的 ANSI 语义, 让同一个 exe 能以简体/繁体/拉丁等编码运行,
不再被 Locale Emulator 或汉化组启动器绑死在 SJIS 上。

## 组成

| 文件 | 作用 |
| --- | --- |
| `FVPLauncher.exe` | 找到游戏主程序 → `CREATE_SUSPENDED` 起进程 → 注入 `fvpl_hook.dll` → 放行 |
| `fvpl_hook.dll` | 只改游戏主模块的**导入表** (IAT hook), 实现存档重定向 + 代码页虚拟化 |
| `FVPIcon.exe` | 独立的换图标小工具: 把 `.ico` 写进任意 exe 的图标资源 (见 [换图标](#换图标)) |
| `FVPBind.exe` | 把钩子 DLL 写进游戏主程序自己的导入表, 从此不需要 `FVPLauncher.exe` (见 [B 档](#b-档目录里只留一个-exe--fvpbindexe)) |
| `fvp-launcher.ini` | 配置, 与 `FVPLauncher.exe` 同目录; 没有也能跑 (全默认值) |

x86 / x64 各一套 (`dist/x86`, `dist/x64`), 位数必须和游戏 exe 一致, 启动器会校验并拒绝错配。

## 为什么用 IAT hook 而不是 inline detour

游戏 exe 一个字节都不改, 与汉化组 `filter.dll` 自带的 Detours 补丁不冲突, 删掉注入的 DLL 即完全回滚。
FVP 引擎的所有外部调用都走导入表, IAT 足够。

(用 `FVPBind.exe` 的 [B 档](#b-档目录里只留一个-exe--fvpbindexe) 是这套"零修改"唯一的例外:
它把同样的 IAT 思路做在文件上, 换来的是"目录里只有一个 exe"。撤销用 `-restore`, 字节级还原。)

## 汉化版的链式启动 → 延迟注入

汉化版的最外层 `SakuraChs.exe` 自己也是个启动器: 它 `CREATE_SUSPENDED` 起 `Sakura.exe`, **在挂起状态
下**往子进程导入表里塞 `filter.dll` (连同 `ass.dll` / `MSVCP140` / `VCRUNTIME140`), 然后才放行。

我们的 `CreateProcessA` hook 会把这个子进程接过去, 但**不能当场注入**: 远线程注入会立刻触发子进程的
加载器初始化, 而加载器只处理初始化那一刻已存在的导入项 —— 汉化组随后才塞进去的那条永远轮不到被处理。
实测后果就是 `filter.dll` 整条链进不了游戏, 汉化失效、对话回落成原版日文 (标题也变回日文)。

所以 hook 侧一发现"调用方自己带着 `CREATE_SUSPENDED`", 就不自己动手, 而是拉起同目录的
`FVPLauncher.exe --inject-ready <pid> <dll>` —— 独立进程, 不会随宿主 (`SakuraChs.exe` 注入完就退出)
一起消失 —— 由它等到子进程模块数超过 `exe + ntdll` (即加载器已处理完导入表) 再注入。

`fvpl_hook.log` / `fvpl_launcher.log` 里能看到这条链:

```
CreateProcessA intercepted
  caller passed CREATE_SUSPENDED
child injection deferred to FVPLauncher.exe
延迟注入: pid=24412 模块数=22, 注入 D:\soft\Sakura moyu\fvpl_hook.dll
延迟注入完成
```

结果: 汉化的 `filter.dll` / `ass.dll` / `MSVCP140` 与我们的 `fvpl_hook.dll` 同时存在于游戏进程里
(实测模块表 75 项, 两者齐全), 标题栏与画面都是汉化后的中文。

## 存档路径是怎么来的 (逆向结论)

`Sakura.exe` (PE32, 0x014c) 的存档根 = `shell32!SHGetFolderPathA(CSIDL_PERSONAL)` 拼上
`user32!LoadStringA` 取出的资源串 (RT_STRING, lang 1041, 每个游戏各自编译了一份厂商名 + 作品名),
再接 HCB 剧本给的 `save/s%03d.bin`。剧本层 (`Sakura.hcb`) 不含任何路径逻辑。
所以只要改写 `SHGetFolderPath*` 的返回值, 就同时管住了存档、配置、崩溃转储的落点。

## 用法

把 `dist/x86/FVPLauncher.exe` 和 `dist/x86/fvpl_hook.dll` 放进游戏目录, 双击启动器即可 ——
`fvp-launcher.ini` 可以没有, 此时按本机环境自动推断 (见下)。要精细适配再放一份 ini。
汉化版 (带 `SakuraChs.exe` 的) 把 `[launcher] exe` 写成链的最外层并开 `inject_children = 1`。

配置项见 `fvp-launcher.ini.example`。不想让玩家碰启动器, 就用 `FVPBind.exe` 把钩子绑进游戏本体,
目录里只留一个 exe (见 [B 档](#b-档目录里只留一个-exe--fvpbindexe))。存档最常用两档:

```ini
[save]
path = .            ; 存档就落在游戏目录
migrate = 0
```

```ini
[save]
path = D:\saves\さくら    ; 独立存档盘, 首启时把文档里的老档搬过来
migrate = 1
```

## 不写 ini 时的默认值

启动器不依赖 ini: 目录里没有 `fvp-launcher.ini` 时按下面的自动推断跑, 原则是"至少不比直接
双击游戏更差"。`codepage` / `font_charset` 也可以显式写 `auto`。

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `[launcher] exe` | 自动探测 | 扫本目录, 挑导入 `shell32!SHGetFolderPathA` 的主程序; 多个才弹框让你写死 |
| `[launcher] inject_children` | `0` | 汉化链式启动 (`SakuraChs.exe` → `Sakura.exe`) 必须写 `1` |
| `[save] redirect` / `path` | `1` / `userdata` | 存档根落到游戏目录下的 `userdata`, 不是系统"文档" |
| `[save] migrate` | `1` | 首启把"文档"里的老档**复制**过来 (源不删, 已存在的目标文件跳过) |
| `[nls] codepage` | **本机真实 ACP** | 中文 Windows = 936。谎报别的值会破坏非 ASCII 路径的 A↔W 往返 (见实测样例) |
| `[nls] font_charset` | 按 codepage 推导 | 932→128 936→134 950→136 949→129 1252→0 |
| `[nls] virtualize_files` | `1` | |
| `[nls] text_codepage` | `0` (文本层关) | **文字层单独一档**: 界面/剧本字节的解释码页。留 0 = 完全不碰字形与字体, 汉化包安全; 填 936/932/950/949/65001 = 打开 `CreateFontA`/`GetGlyphOutlineA`/`LoadStringA` 这层 |
| `[nls] virtualize_ui` | 跟随 `text_codepage` | 显式写 0/1 可单独覆盖; 默认就是"文本层开了才开", 不破坏旧 ini |
| `[nls] lcid` | `0` | |

## 构建

```bash
./build.sh                      # 跑 host 单测 + 产出 dist/x86 和 dist/x64
./build.sh "/mnt/d/soft/Sakura moyu/Sakura.exe"
```

依赖 `gcc`、`mingw-w64` (`i686-w64-mingw32-gcc` / `x86_64-w64-mingw32-gcc`)。
纯逻辑模块 (`pathutil` `utf8w` `config` `peinfo` `bindpe`) 不依赖 `windows.h`, 直接在 WSL 上用 host gcc 单测,
测试里会拿真实 `Sakura.exe` 校验 PE 解析和导入指纹, 并在内存里完整跑一遍 B 档的绑定/回读。

## 按游戏单独适配 (每游戏一份 ini)

`fvp-launcher.ini` 就是适配点: 同一个 `FVPLauncher.exe` 扔进不同游戏目录, 各自读自己那份,
可以随汉化包一起分发, 不需要改代码、也不需要装 Locale Emulator。

实测样例 —— **さくら、もゆ。 简体汉化版** (`SakuraChs.exe` 链式启动 `Sakura.exe`, 汉化自带
`filter.dll` + `ass.dll`/libass 文本层):

```ini
[launcher]
exe = SakuraChs.exe
inject_children = 1

[nls]
codepage = 936
font_charset = 134
virtualize_files = 1
virtualize_ui = 0        ; 关键: 汉化自带文本层时必须关
```

- **`virtualize_ui = 0` 是关键**。这类汉化自己就 hook 了取字形 / 窗口标题的路径 (它自带
  libass 文本层), 再叠我们的 `CreateFontA` / `GetGlyphOutlineA` / `MessageBoxA` /
  `CreateWindowExA` 标题 hook 会让对话文本与标题栏一起错乱 (实测: 标题回落成"原版日文
  字节被当 GBK 读"的乱码形态)。纯日文原版游戏没有自带文本层, 才需要开 1。
- `virtualize_files = 1` 可以和这类汉化共存 (实测): 文件 API 的码页解释层不干扰链式启动。
- 宿主进程自己就是 936 时, `codepage = 936` 那批转换 hook 等价于空操作 —— 此时启动器提供的
  是"子进程注入 + 存档重定向"; 到了日文区 Windows 上, 同一个 ini 才真正靠这批 hook 把 ACP
  假扮成 936, 顶掉外挂 Locale Emulator 的作用。

实测样例 —— **紅い瞳に映るセカイ (日文原版, 装在中文 Windows 上)**:

```ini
[launcher]
exe = D:\soft\君が朱眸に映す五彩の世界\Shinku.exe

[save]
redirect = 1
path = .
migrate = 0

[nls]
codepage = 936           ; 关键: 必须等于宿主真实 ACP (这台机器是 936)
virtualize_files = 0
virtualize_ui = 0
```

- **日文原版 + 中文 Windows 上 `codepage` 不要填 932** (实测踩过): 引擎是先用宿主 ACP(936)
  的字节拼资源路径 (如 `graph/main_window`), 再交给 ANSI API 的; 一旦把 ACP 谎报成 932,
  非 ASCII 路径的 A↔W 往返就不再恒等, 玩家会看到游戏自己弹的
  「graph/main_windowのオープンに失敗しました」。改成宿主真实值 (936) 即恢复正常。
  对照实验: 同一个 ini 其它都不动, 只把 `codepage` 从 932 改成 936 —— 932 弹框, 936 正常。
- 所以 `[nls] codepage` 表达的不是"游戏是什么语言", 而是"要让系统 API 假装成哪个区域":
  日文区 Windows 上跑汉化版填 936 (假扮中文); 中文 Windows 上跑日文原版填 936 (保持真实);
  只有"非日文区 Windows + 纯日文引擎"才填 932。
- 这个游戏没有 `SakuraChs.exe` 那样的 `CREATE_SUSPENDED` 链, `inject_children` 保持默认 1 即可
  (只有链式启动的汉化版才走 `--inject-ready` 延迟注入)。

实测样例 —— **同人作品: 日文引擎本体 + 中文剧本** (祈愿与仲夏日之梦, 主程序 `Sakura_cn.exe`):

```ini
[launcher]
exe = Sakura_cn.exe
title = 祈愿与仲夏日之梦      ; 盖掉引擎那句日文原标题 (见下)

[nls]
codepage = 936
text_codepage = 0            ; 关键: 这台机器上必须关 (踩过, 见下)

[save]
redirect = 1
path = .save
migrate = 1
```

- **宿主 ACP 已经等于剧本文字的码页时, `text_codepage` 要留 0**。中文 Windows(936) 上跑中文
  GBK 剧本本来就是自洽的, 开文本层没有任何收益; 而这个作品的引擎标题串是它内部按 932 转过
  一道再交给 Windows 的, 开 936 会把这句连同"メッセージフォント"预览样张一起打成问号。
  实测: `text_codepage = 0` 时窗口标题读到的是 `祈愿与仲夏日之梦` 8 个干净码点,
  填 932 时变成 `?橳????????????`。
- 剩下的"标题栏花屏"是**引擎自己那句日文原标题** (`さくら、もゆ。` 的 SJIS 字节被 936 系统
  读成 GBK), 与码页配置无关 —— 用 `[launcher] title` 直接写死要显示的那句。
- 只有"非日文区 Windows + 纯日文引擎"这种真需要转一层的情况才填 `text_codepage = 932`。

## B 档: 目录里只留一个 exe —— `FVPBind.exe`

A 档 (默认) 目录里是「游戏.exe + `FVPLauncher.exe` + `fvpl_hook.dll` + ini」, 玩家要双击启动器。
B 档把钩子 DLL 直接写进**游戏主程序自己的导入表** (加一条 `.fvpl` 节 + 一个导入描述符),
于是目录里只剩「游戏.exe + `fvpl_hook.dll` + ini」, 双击游戏本体就等于走了启动器。
分发出去的包和原版长得一样。

```bat
FVPBind.exe Sakura.exe -check          :: 只体检: 位数 / 导入几座 DLL / 头部空位 / 绑过没有
FVPBind.exe Sakura.exe                 :: 就地绑 (先自动留 Sakura.exe.bak)
FVPBind.exe Sakura.exe -o 我的游戏.exe  :: 另存一份, 原文件不动
FVPBind.exe Sakura.exe -restore        :: 撤销, 字节级还原成 .bak
```

- **这一步真的改了 exe 本体**, 和启动器"只改内存里的导入表"不同, 所以备份是强制的:
  `.bak` 留不下来就一个字都不写。体检不过 (节表后面空位不足 / 非 PE / 没有导入表) 会明确
  告诉你退回 A 档。
- 绑完必须把 `fvpl_hook.dll` 放到 exe 同目录, 否则 Windows 会报"找不到 xxx.dll"启动不了
  (工具绑成功后会检查并提醒)。
- 加了导入项会让 exe 的**数字签名和自校验失效**。FVP 引擎没有自校验 (实测绑完照常跑),
  但带完整性检查的外壳可能拒绝启动 —— 遇到就退回 A 档。
- 绑过的 exe 仍然可以照常用 `FVPLauncher.exe` 启动 (加载器对同一个 DLL 只加载一次),
  所以先绑后测、随时回退都不冲突。
- **顺序只能是「先换图标, 再绑定」**。`FVPIcon.exe` 走的是 Windows 资源重写, 它会把 `.rsrc`
  之后每一节的地址重排一遍, 而 `.fvpl` 里那张导入表的地址全按绑定当时的地址写死 —— 绑完再
  换图标, exe 会当场打不开。为此 `FVPIcon.exe` 查到 `.fvpl` 节就动手前先拒绝, 并把三步
  (`-restore` → 换图标 → 重新绑定) 打在屏幕上照着抄就行。
- 钩子 DLL 的 `DllMain` 里不能开文件 (会死锁加载器锁), 因此进程初始化阶段的日志先在内存里
  攒着, 初始化结束再一次性写进 `fvpl_hook.log`。B 档全靠这条约定才拿得到日志。

## 换图标

两种图标互不相干, 可以只用其中一种。

**1) 游戏窗口图标** —— 在游戏目录的 `fvp-launcher.ini` 的 `[launcher]` 里写一行:

```ini
[launcher]
icon = D:\icons\xxx.ico
```

`fvpl_hook.dll` 会在游戏窗口出现后把它换成这个图标 (进程运行期间一直有效)。也可以填另一个
`.exe` 的路径, 会取它里面的第 0 号图标; 相对路径按游戏目录解析。留空 = 不动。

**2) exe 文件本身的图标** —— 用 `FVPIcon.exe` (随发布包一起分发) 往 exe 里写图标资源:

```bat
FVPIcon.exe FVPLauncher.exe my.ico              :: 就地改, 改前留一份 FVPLauncher.exe.bak
FVPIcon.exe FVPLauncher.exe my.ico -o 我的启动器.exe   :: 另存为, 不动原文件
```

`my.ico` 里的多张尺寸会全部写进目标 (用于资源管理器在不同视图下选合适的尺寸)。

旧图标是**枚举**出来删的: 任意 ID、任意语言的 `RT_GROUP_ICON` 和 `RT_ICON` 一律清掉, 新图标
再按目标原有图标组的那个子语言写回去。日文引擎常把图标组放在 ID 104 / 语言 1041, 只盯着
ID 1 / 语言 0 的话旧的删不掉, 换完 exe 里会躺着两组图标 (资源管理器挑到哪组看运气)。
目标一个图标都没有也能写 (这时用语言 0)。做过 B 档绑定的 exe 会被拒绝, 见上一节。

**3) 窗口标题栏的文字** —— ini 里 `[launcher] title` 写死一句即可 (只作用于顶层窗口,
游戏内对话框不动)。用于引擎自带标题是另一种码页、任何 `text_codepage` 都救不了的混排作品。

## 已知边界

- 不做跨位数注入 (32 位启动器不注 64 位游戏), 需要哪种就放哪套产物。
- `[save] path` 必须能用配置的代码页无损表示 (目录名里有日文假名而 `codepage=936` 时不行),
  否则启动器会弹窗提示并放弃重定向, 保持游戏原行为。
- `save path` 等于游戏目录时禁止开 `migrate`, 免得把 `文档\FAVORITE` 整棵灌进游戏根。
- 窗口标题类的纯外观字符串仍可能出现编码错位; 刻意没有把窗口过程换成 W 版 (理由见 `src/hook_nls.c` 头注释)。
