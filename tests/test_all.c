/* test_all.c — host 侧纯逻辑单测 (gcc 直接跑, 不进 Windows 产物) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/pathutil.h"
#include "../src/utf8w.h"
#include "../src/config.h"
#include "../src/peinfo.h"
#include "../src/bindpe.h"

static int fails = 0, checks = 0;

#define CHECK(cond, name) do { \
    checks++; \
    if (!(cond)) { fails++; printf("FAIL %s\n", name); } \
} while (0)

static void t_path(void) {
    fvpl_wchar d[FVPL_MAX_PATH], j[FVPL_MAX_PATH];
    CHECK(pv_is_abs(L"D:\\a\\b") == 1, "is_abs drive");
    CHECK(pv_is_abs(L"\\\\srv\\share") == 1, "is_abs unc");
    CHECK(pv_is_abs(L"userdata") == 0, "is_abs rel");
    CHECK(pv_is_abs(L"D:a\\b") == 0, "is_abs drive-relative");
    CHECK(pv_dirname(d, 260, L"C:\\games\\Sakura\\Sakura.exe") && pv_ci_equal(d, L"C:\\games\\Sakura"), "dirname");
    CHECK(pv_dirname(d, 260, L"C:\\a.exe") && pv_ci_equal(d, L"C:\\"), "dirname keeps root");
    CHECK(pv_join(j, 260, L"C:\\g\\Sakura", L"userdata") && pv_ci_equal(j, L"C:\\g\\Sakura\\userdata"), "join rel");
    CHECK(pv_join(j, 260, L"C:\\g\\Sakura\\", L"userdata") && pv_ci_equal(j, L"C:\\g\\Sakura\\userdata"), "join trailing sep");
    CHECK(pv_join(j, 260, L"C:\\g", L"D:\\x\\save") && pv_ci_equal(j, L"D:\\x\\save"), "join abs wins");
    CHECK(pv_join(j, 260, L"C:\\g", L"\\save") && pv_ci_equal(j, L"C:\\g\\save"), "join leading sep");
    CHECK(pv_join(j, 8, L"C:\\longdir", L"sub") == 0, "join overflow rejected");
    {
        fvpl_wchar big[FVPL_MAX_PATH];
        memset(big, 'a', sizeof(big));
        big[FVPL_MAX_PATH - 1] = 0;
        CHECK(pv_copy(d, 8, big) == 0, "copy overflow rejected");
    }
    CHECK(pv_ends_with_ci(L"Sakura.EXE", L".exe") == 1, "ends_with_ci");
    {
        fvpl_wchar p[FVPL_MAX_PATH];
        pv_copy(p, 260, L"C:\\g\\Sakura\\.");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"C:\\g\\Sakura"), "norm drops dot");
        pv_copy(p, 260, L"C:/g//Sakura/");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"C:\\g\\Sakura"), "norm slashes and dupes");
        pv_copy(p, 260, L"\\\\srv\\share\\save");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"\\\\srv\\share\\save"), "norm keeps unc");
        pv_copy(p, 260, L"\\\\srv\\share\\");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"\\\\srv\\share"), "norm unc trailing");
        pv_copy(p, 260, L"D:\\saves\\..\\x");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"D:\\saves\\..\\x"), "norm leaves dotdot alone");
        pv_copy(p, 260, L"C:");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"C:\\"), "norm drive root");
        pv_copy(p, 260, L".");
        CHECK(pv_norm(p, 260) && pv_ci_equal(p, L"."), "norm bare dot");
    }
}

static void t_utf8(void) {
    const unsigned char sjisish[] = "userdata\\FAVORITE";
    const unsigned char jp[] = "\xE3\x81\x95\xE3\x81\x8F\xE3\x82\x89";  /* さくら */
    const unsigned char zh[] = "\xE6\xA7\x8B\xE8\x8A\xB1";              /* 桜花 */
    fvpl_wchar w[64];
    unsigned char back[64];
    CHECK(uw_decode(sjisish, 17, w, 64) == 17 && w[0] == L'u' && w[16] == L'E' && w[17] == 0, "ascii decode");
    CHECK(uw_decode(jp, 9, w, 64) == 3 && w[0] == 0x3055 && w[1] == 0x304F && w[2] == 0x3089, "3-byte decode");
    CHECK(uw_encode(w, 3, back, 64) == 9 && memcmp(back, jp, 9) == 0, "roundtrip jp");
    CHECK(uw_decode(zh, 6, w, 64) == 2 && w[0] == 0x69CB && w[1] == 0x82B1, "cjk decode");
    {
        const unsigned char bad[] = { 0xE3, 0x81, 0x00 };  /* 截断的三字节 */
        CHECK(uw_decode(bad, 3, w, 64) == -1, "truncated utf8 rejected");
    }
    {
        const unsigned char sur[] = { 0xED, 0xA0, 0x80 };  /* lone surrogate */
        CHECK(uw_decode(sur, 3, w, 64) == -1, "surrogate rejected");
    }
    CHECK(uw_decode(zh, 6, w, 3) == 2, "fits tight cap");
    CHECK(uw_decode(zh, 6, w, 2) == -1, "too small cap rejected");
}

static const fvpl_wchar INI_TEXT[] =
L"; comment line\n"
"[launcher]\n"
"exe = Sakura.exe\n"
"inject_children=1\n"
"[save]\n"
"path = D:\\saves\\sakura\n"
"folder_ids = 5, 0x0c\n"
"migrate = 0\n"
"[nls]\n"
"codepage = 936 ; 汉化剧本\n"
"font_charset = 134\n";

static void t_config(void) {
    FvplIni ini;
    FvplConfig c;
    fvpl_wchar err[128];
    CHECK(cfg_ini_parse(INI_TEXT, &ini) == 1, "ini parse");
    CHECK(cfg_ini_get(&ini, L"launcher", L"exe") && pv_ci_equal(cfg_ini_get(&ini, L"launcher", L"exe"), L"Sakura.exe"), "ini value trimmed");
    CHECK(cfg_ini_int(&ini, L"save", L"migrate", 1) == 0, "ini zero wins over default");
    CHECK(cfg_build(&ini, L"C:\\g\\Sakura", &c, err, 128) == 1, "cfg_build ok");
    CHECK(pv_ci_equal(c.save_path, L"D:\\saves\\sakura"), "abs save path kept");
    CHECK(c.n_folder_ids == 2 && c.folder_ids[0] == 5 && c.folder_ids[1] == 12, "folder_ids hex list");
    CHECK(c.codepage == 936 && c.font_charset == 134, "nls knobs");
    CHECK(c.inject_children == 1, "inject children");
    CHECK(c.save_migrate == 0, "migrate off");
    {
        FvplIni e;
        cfg_ini_parse(L"[save]\npath=userdata\n", &e);
        CHECK(cfg_build(&e, L"C:\\g\\Sakura", &c, err, 128) == 1 && pv_ci_equal(c.save_path, L"C:\\g\\Sakura\\userdata"), "relative save resolves to game dir");
        CHECK(c.n_folder_ids == 1 && c.folder_ids[0] == 5, "default folder id");
        CHECK(pv_ci_equal(c.migrate_names, L"FAVORITE"), "default migrate name");
    }
    {
        FvplIni e;
        cfg_ini_parse(L"[launcher]\nexe = Sakura.exe ; 主程序\nparams = \"a;b\" ; 引号内算内容\nworkdir =\n", &e);
        CHECK(pv_ci_equal(cfg_ini_get(&e, L"launcher", L"exe"), L"Sakura.exe"), "inline comment stripped");
        CHECK(pv_ci_equal(cfg_ini_get(&e, L"launcher", L"params"), L"a;b"), "quoted value keeps semicolon");
        CHECK(cfg_ini_get(&e, L"launcher", L"workdir") && cfg_ini_get(&e, L"launcher", L"workdir")[0] == 0, "empty value stays empty");
    }
    {
        /* [launcher] icon: 绝对路径原样保留, 相对路径对 basedir 解析, 缺省为空 */
        FvplIni e;
        cfg_ini_parse(L"[launcher]\nicon = D:\\icons\\my.ico\n", &e);
        CHECK(cfg_build(&e, L"C:\\g\\Sakura", &c, err, 128) == 1 && pv_ci_equal(c.icon, L"D:\\icons\\my.ico"), "abs icon kept");
        cfg_ini_parse(L"[launcher]\nicon = my.ico\n", &e);
        CHECK(cfg_build(&e, L"C:\\g\\Sakura", &c, err, 128) == 1 && pv_ci_equal(c.icon, L"C:\\g\\Sakura\\my.ico"), "relative icon resolves to game dir");
        cfg_ini_parse(L"[launcher]\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 && c.icon[0] == 0, "icon default empty");
    }
    {
        /* [launcher] title: 原样存下, 不做路径解析; 缺省为空 = 不盖标题 */
        FvplIni e;
        cfg_ini_parse(L"[launcher]\ntitle = 祈愿与仲夏日之梦 ver1.1\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 &&
              pv_ci_equal(c.title, L"祈愿与仲夏日之梦 ver1.1"), "title kept verbatim");
        cfg_ini_parse(L"[launcher]\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 && c.title[0] == 0, "title default empty");
    }
    {
        FvplIni e;
        cfg_ini_parse(L"[nls]\ncodepage=0\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 0, "codepage 0 refused");
        cfg_ini_parse(L"[save]\npath=.\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 && pv_ci_equal(c.save_path, L"C:\\g"),
              "save==basedir allowed (lazy migrate only takes this game's files)");
        cfg_ini_parse(L"[save]\npath=.\nmigrate=0\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1, "save==basedir allowed when migrate off");
    }
    {
        /* 没有任何 ini 也要能跑: codepage 跟随宿主, charset 按 codepage 推导, 界面文字不动 */
        FvplIni e;
        cfg_set_host_acp(936);
        cfg_ini_parse(L"", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1, "empty ini builds");
        CHECK(c.codepage == 936 && c.font_charset == 134, "host acp becomes default codepage");
        CHECK(c.virtualize_ui == 0, "ui virtualization off by default");
        cfg_ini_parse(L"[nls]\ncodepage = auto\nfont_charset = auto\n", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 && c.codepage == 936 && c.font_charset == 134, "auto keyword");
        cfg_set_host_acp(932);
        cfg_ini_parse(L"", &e);
        CHECK(cfg_build(&e, L"C:\\g", &c, err, 128) == 1 && c.codepage == 932 && c.font_charset == 128, "jp host derives shiftjis charset");
    }
}

static void t_pe(const char *path) {
    FILE *f = fopen(path, "rb");
    static unsigned char buf[4 * 1024 * 1024];
    size_t n;
    int m;
    if (!f) { printf("SKIP pe tests (%s missing)\n", path); return; }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    m = pe_machine(buf, (unsigned)n);
    CHECK(m == FVPL_MACHINE_I386, "Sakura.exe is x86");
    CHECK(pe_has_import(buf, (unsigned)n, "shell32.dll", "SHGetFolderPathA") == 1, "imports SHGetFolderPathA");
    CHECK(pe_has_import(buf, (unsigned)n, "SHELL32.DLL", "SHGetFolderPathA") == 1, "dll name case-insensitive");
    CHECK(pe_has_import(buf, (unsigned)n, "user32.dll", "LoadStringA") == 1, "imports LoadStringA");
    CHECK(pe_has_import(buf, (unsigned)n, "gdi32.dll", "GetGlyphOutlineA") == 1, "imports GetGlyphOutlineA");
    CHECK(pe_has_import(buf, (unsigned)n, "kernel32.dll", "MultiByteToWideChar") == 1, "imports MultiByteToWideChar");
    CHECK(pe_has_import(buf, (unsigned)n, "kernel32.dll", "NoSuchFunctionHere") == 0, "absent import not matched");
    CHECK(pe_has_import(buf, (unsigned)n, "uxtheme.dll", "FlushMenuThemes") == 0, "absent dll not matched");
    CHECK(pe_has_import(buf, (unsigned)n, "shell32.dll", "SHGetFolderPath") == 0, "prefix is not a match");
    {
        unsigned int save = buf[0x3C];
        buf[0x3C] = (unsigned char)(n - 1);
        CHECK(pe_machine(buf, (unsigned)n) == -1, "corrupt e_lfanew refused");
        buf[0x3C] = (unsigned char)save;
    }
    CHECK(pe_machine(buf, 10) == -1, "truncated header refused");
}

static unsigned int b32(const unsigned char *b) {
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8) |
           ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}

static void t_bind(const char *path) {
    FILE *f = fopen(path, "rb");
    static unsigned char buf[4 * 1024 * 1024];
    static unsigned char junk[0x2000];
    unsigned int cap, n, sz;
    size_t r;
    char err[256];
    int is32 = -1, ndll = -1, ndll2 = -1, slack = -1, already = -1;
    unsigned int irva = 0, ilen = 0, io = 0, off = 0, last;
    if (!f) { printf("SKIP bind tests (%s missing)\n", path); return; }
    r = fread(buf, 1, sizeof(buf) - 0x4000, f);
    fclose(f);
    n = (unsigned)r;
    cap = n + 0x4000;

    CHECK(pe_bind_inspect(buf, n, "fvpl_hook.dll", &is32, &ndll, &slack, &already) == 1,
          "inspect 认得这个 exe");
    CHECK(already == 0, "干净 exe 报未绑定");
    CHECK(slack >= 40, "头部留得下再加一节");
    CHECK(pe_bind_has_marker(buf, n) == 0, "未绑定的 exe 没有 .fvpl 节标记");
    memset(junk, 'x', sizeof(junk));
    CHECK(pe_bind_has_marker(junk, sizeof(junk)) == 0, "畸形输入不会误报节标记");

    CHECK(pe_bind_apply(buf, cap, &n, "a\\b.dll", "FvplAttach", err, 256) == FVPL_BIND_FAIL,
          "带路径的 DLL 名被拒绝");
    CHECK(pe_bind_apply(buf, n, &n, "fvpl_hook.dll", "FvplAttach", err, 256) == FVPL_BIND_FAIL,
          "容量不够时拒绝写入");
    memset(junk, 0, sizeof(junk));
    {
        unsigned int jn = 0x1000;
        CHECK(pe_bind_apply(junk, sizeof(junk), &jn, "fvpl_hook.dll", "FvplAttach",
                            err, 256) == FVPL_BIND_FAIL, "全零垃圾镜像被拒绝");
    }

    sz = n;
    CHECK(pe_bind_apply(buf, cap, &sz, "fvpl_hook.dll", "FvplAttach", err, 256) == FVPL_BIND_OK,
          "绑定成功");
    CHECK(pe_has_import(buf, sz, "fvpl_hook.dll", "FvplAttach") == 1, "新依赖可被查到");
    CHECK(pe_bind_has_marker(buf, sz) == 1, "绑定后查得到 .fvpl 节标记");
    CHECK(pe_has_import(buf, sz, "shell32.dll", "SHGetFolderPathA") == 1,
          "搬表后原有导入一条不少");
    CHECK(pe_bind_inspect(buf, sz, "fvpl_hook.dll", &is32, &ndll2, &slack, &already) == 1 &&
          already == 1 && ndll2 == ndll + 1, "绑定后描述符表多一项且标记已绑");
    CHECK(pe_bind_apply(buf, cap, &sz, "fvpl_hook.dll", "FvplAttach", err, 256)
              == FVPL_BIND_ALREADY, "重复绑定是幂等的");

    /* 导入目录现在必须指向新节里的表, 且最后一项的 DLL 名就是 fvpl_hook.dll */
    CHECK(pe_data_dir(buf, sz, 1, &irva, &ilen) == 1 && irva != 0, "导入目录仍可读");
    CHECK(pe_rva_to_off(buf, sz, irva, &io) == 1, "导入目录对得到文件偏移");
    last = io + (unsigned)ndll * 20u;
    CHECK(b32(buf + last + 12) != 0 && pe_rva_to_off(buf, sz, b32(buf + last + 12), &off) == 1 &&
          strcmp((const char *)buf + off, "fvpl_hook.dll") == 0, "新导入项写着 fvpl_hook.dll");
    CHECK(b32(buf + last + 16) != 0 && pe_rva_to_off(buf, sz, b32(buf + last + 16), &off) == 1,
          "新 IAT 落在镜像内 (加载器要写它)");
    /* 按加载器的走法逐级解引用: 描述符 -> INT 表 -> thunk -> IMAGE_IMPORT_BY_NAME */
    {
        unsigned int io2 = 0, thunk = 0;
        CHECK(pe_rva_to_off(buf, sz, b32(buf + last), &io2) == 1, "INT 表能定位到文件");
        thunk = b32(buf + io2);
        CHECK(thunk != 0 && pe_rva_to_off(buf, sz, thunk, &off) == 1 &&
              strcmp((const char *)buf + off + 2, "FvplAttach") == 0, "按名导入 FvplAttach");
        CHECK(b32(buf + io2 + (is32 ? 4 : 8)) == 0, "INT 表以 0 收尾");
    }
    CHECK(b32(buf + io + (unsigned)(ndll + 1) * 20u + 12) == 0, "表尾仍有空描述符收尾");
    CHECK(pe_data_dir(buf, sz, 11, &irva, &ilen) == 1 && irva == 0 && ilen == 0,
          "绑定导入表已清空 (否则会按陈旧信息比对)");
}

int main(int argc, char **argv) {
    t_path();
    t_utf8();
    t_config();
    if (argc > 1) { t_pe(argv[1]); t_bind(argv[1]); }
    printf("checks=%d fails=%d\n", checks, fails);
    return fails ? 1 : 0;
}
