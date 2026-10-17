/* test_all.c — host 侧纯逻辑单测 (gcc 直接跑, 不进 Windows 产物) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/pathutil.h"
#include "../src/utf8w.h"
#include "../src/config.h"
#include "../src/peinfo.h"

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

int main(int argc, char **argv) {
    t_path();
    t_utf8();
    t_config();
    if (argc > 1) t_pe(argv[1]);
    printf("checks=%d fails=%d\n", checks, fails);
    return fails ? 1 : 0;
}
