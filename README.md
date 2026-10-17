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
| `fvp-launcher.ini` | 配置, 与 `FVPLauncher.exe` 同目录; 没有也能跑 (全默认值) |

x86 / x64 各一套 (`dist/x86`, `dist/x64`), 位数必须和游戏 exe 一致, 启动器会校验并拒绝错配。

## 为什么用 IAT hook 而不是 inline detour

游戏 exe 一个字节都不改, 与汉化组 `filter.dll` 自带的 Detours 补丁不冲突, 删掉注入的 DLL 即完全回滚。
FVP 引擎的所有外部调用都走导入表, IAT 足够。

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

配置项见 `fvp-launcher.ini.example`。最常用两档:

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
| `[nls] virtualize_ui` | `0` | 默认不动界面文字 —— 汉化文本层要 0, 日文原版要 1, 无法两全, 所以默认不碰 |
| `[nls] lcid` | `0` | |

## 构建

```bash
./build.sh                      # 跑 host 单测 + 产出 dist/x86 和 dist/x64
./build.sh "/mnt/d/soft/Sakura moyu/Sakura.exe"
```

依赖 `gcc`、`mingw-w64` (`i686-w64-mingw32-gcc` / `x86_64-w64-mingw32-gcc`)。
纯逻辑模块 (`pathutil` `utf8w` `config` `peinfo`) 不依赖 `windows.h`, 直接在 WSL 上用 host gcc 单测,
测试里会拿真实 `Sakura.exe` 校验 PE 解析和导入指纹。

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

## 已知边界

- 不做跨位数注入 (32 位启动器不注 64 位游戏), 需要哪种就放哪套产物。
- `[save] path` 必须能用配置的代码页无损表示 (目录名里有日文假名而 `codepage=936` 时不行),
  否则启动器会弹窗提示并放弃重定向, 保持游戏原行为。
- `save path` 等于游戏目录时禁止开 `migrate`, 免得把 `文档\FAVORITE` 整棵灌进游戏根。
- 窗口标题类的纯外观字符串仍可能出现编码错位; 刻意没有把窗口过程换成 W 版 (理由见 `src/hook_nls.c` 头注释)。
