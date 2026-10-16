# FVP 通用启动器 (FVP Launcher)

启动 FAVORITE 引擎 (FVP) 游戏的主程序, 并把存档根目录重定向到游戏目录或任意指定目录;
同时用配置里的代码页虚拟化整个进程的 ANSI 语义, 让同一个 exe 能以简体/繁体/拉丁等编码运行,
不再被 Locale Emulator 或汉化组启动器绑死在 SJIS 上。

## 组成

| 文件 | 作用 |
| --- | --- |
| `FVPLauncher.exe` | 找到游戏主程序 → `CREATE_SUSPENDED` 起进程 → 注入 `fvpl_hook.dll` → 放行 |
| `fvpl_hook.dll` | 只改游戏主模块的**导入表** (IAT hook), 实现存档重定向 + 代码页虚拟化 |
| `fvp-launcher.ini` | 配置, 与 `FVPLauncher.exe` 同目录; 没有也能跑 (全默认值) |

x86 / x64 各一套 (`dist/x86`, `dist/x64`), 位数必须和游戏 exe 一致, 启动器会校验并拒绝错配。

## 为什么用 IAT hook 而不是 inline detour

游戏 exe 一个字节都不改, 与汉化组 `filter.dll` 自带的 Detours 补丁不冲突, 删掉注入的 DLL 即完全回滚。
FVP 引擎的所有外部调用都走导入表, IAT 足够。

## 存档路径是怎么来的 (逆向结论)

`Sakura.exe` (PE32, 0x014c) 的存档根 = `shell32!SHGetFolderPathA(CSIDL_PERSONAL)` 拼上
`user32!LoadStringA` 取出的资源串 (RT_STRING, lang 1041, 每个游戏各自编译了一份厂商名 + 作品名),
再接 HCB 剧本给的 `save/s%03d.bin`。剧本层 (`Sakura.hcb`) 不含任何路径逻辑。
所以只要改写 `SHGetFolderPath*` 的返回值, 就同时管住了存档、配置、崩溃转储的落点。

## 用法

把 `dist/x86/FVPLauncher.exe`、`dist/x86/fvpl_hook.dll` 和一份 `fvp-launcher.ini` 放进游戏目录, 双击启动器。
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

## 构建

```bash
./build.sh                      # 跑 host 单测 + 产出 dist/x86 和 dist/x64
./build.sh "/mnt/d/soft/Sakura moyu/Sakura.exe"
```

依赖 `gcc`、`mingw-w64` (`i686-w64-mingw32-gcc` / `x86_64-w64-mingw32-gcc`)。
纯逻辑模块 (`pathutil` `utf8w` `config` `peinfo`) 不依赖 `windows.h`, 直接在 WSL 上用 host gcc 单测,
测试里会拿真实 `Sakura.exe` 校验 PE 解析和导入指纹。

## 已知边界

- 不做跨位数注入 (32 位启动器不注 64 位游戏), 需要哪种就放哪套产物。
- `[save] path` 必须能用配置的代码页无损表示 (目录名里有日文假名而 `codepage=936` 时不行),
  否则启动器会弹窗提示并放弃重定向, 保持游戏原行为。
- `save path` 等于游戏目录时禁止开 `migrate`, 免得把 `文档\FAVORITE` 整棵灌进游戏根。
- 窗口标题类的纯外观字符串仍可能出现编码错位; 刻意没有把窗口过程换成 W 版 (理由见 `src/hook_nls.c` 头注释)。
