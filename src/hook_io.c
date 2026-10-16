/* hook_io.c — 存档目录重定向 + 文件系统 A 系 API 的虚拟代码页化 */
#include "hooks.h"
#include "pathutil.h"
#include <shlobj.h>

typedef HRESULT (WINAPI *PFN_SHGetFolderPathA)(HWND, int, HANDLE, DWORD, LPSTR);
typedef HRESULT (WINAPI *PFN_SHGetFolderPathW)(HWND, int, HANDLE, DWORD, LPWSTR);
typedef HRESULT (WINAPI *PFN_SHGetKnownFolderPath)(const GUID *, DWORD, HANDLE, PWSTR *);
typedef HANDLE (WINAPI *PFN_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL (WINAPI *PFN_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *PFN_FindNextFileA)(HANDLE, LPWIN32_FIND_DATAA);
typedef DWORD (WINAPI *PFN_GetFileAttributesA)(LPCSTR);
typedef BOOL (WINAPI *PFN_GetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef BOOL (WINAPI *PFN_DeleteFileA)(LPCSTR);
typedef BOOL (WINAPI *PFN_CopyFileA)(LPCSTR, LPCSTR, BOOL);
typedef BOOL (WINAPI *PFN_MoveFileA)(LPCSTR, LPCSTR);
typedef BOOL (WINAPI *PFN_MoveFileExA)(LPCSTR, LPCSTR, DWORD);
typedef BOOL (WINAPI *PFN_CreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL (WINAPI *PFN_RemoveDirectoryA)(LPCSTR);
typedef BOOL (WINAPI *PFN_SetCurrentDirectoryA)(LPCSTR);
typedef HMODULE (WINAPI *PFN_LoadLibraryA)(LPCSTR);
typedef DWORD (WINAPI *PFN_GetModuleFileNameA)(HMODULE, LPSTR, DWORD);
typedef BOOL (WINAPI *PFN_FindClose)(HANDLE);

static PFN_SHGetFolderPathA   o_SHGetFolderPathA;
static PFN_SHGetFolderPathW   o_SHGetFolderPathW;
static PFN_SHGetKnownFolderPath o_SHGetKnownFolderPath;
static PFN_CreateFileA        o_CreateFileA;
static PFN_FindFirstFileA     o_FindFirstFileA;
static PFN_FindNextFileA      o_FindNextFileA;
static PFN_GetFileAttributesA o_GetFileAttributesA;
static PFN_GetFileAttributesExA o_GetFileAttributesExA;
static PFN_DeleteFileA        o_DeleteFileA;
static PFN_CopyFileA          o_CopyFileA;
static PFN_MoveFileA          o_MoveFileA;
static PFN_MoveFileExA        o_MoveFileExA;
static PFN_CreateDirectoryA   o_CreateDirectoryA;
static PFN_RemoveDirectoryA   o_RemoveDirectoryA;
static PFN_SetCurrentDirectoryA o_SetCurrentDirectoryA;
static PFN_LoadLibraryA       o_LoadLibraryA;
static PFN_GetModuleFileNameA o_GetModuleFileNameA;
static PFN_FindClose          o_FindClose;

static const GUID FOLDERID_Documents_local =
    { 0xf42f2270, 0x9d92, 0x4c16, { 0x8d, 0xb7, 0xbb, 0x4c, 0x01, 0xb6, 0x5f, 0x76 } };

/* FindNextFileA 只能换算我们自己开出的句柄, 否则会把别人的 A 句柄当 W 句柄用 */
#define FVPL_FINDTRACK 32
static HANDLE s_find_handles[FVPL_FINDTRACK];
static int s_find_count = 0;
static CRITICAL_SECTION s_find_lock;
static int s_find_lock_ok = 0;

static void find_track_add(HANDLE h) {
    if (!s_find_lock_ok) return;
    EnterCriticalSection(&s_find_lock);
    if (s_find_count < FVPL_FINDTRACK) s_find_handles[s_find_count++] = h;
    LeaveCriticalSection(&s_find_lock);
}
static int find_track_take(HANDLE h) {
    int i, found = -1;
    if (!s_find_lock_ok) return 0;
    EnterCriticalSection(&s_find_lock);
    for (i = 0; i < s_find_count; i++) if (s_find_handles[i] == h) { found = i; break; }
    if (found >= 0)
        s_find_handles[found] = s_find_handles[--s_find_count];
    LeaveCriticalSection(&s_find_lock);
    return found >= 0;
}

static int folder_redirects(int csidl) {
    int i;
    if (!g_cfg.save_redirect) return 0;
    for (i = 0; i < g_cfg.n_folder_ids; i++)
        if ((int)g_cfg.folder_ids[i] == csidl) return 1;
    return 0;
}

static HRESULT WINAPI Hook_SHGetFolderPathA(HWND owner, int csidl, HANDLE token,
                                            DWORD flags, LPSTR path) {
    const WCHAR *dir;
    if (!FvplReady() || !folder_redirects(csidl) ||
        (dir = FvplSaveDirW()) == NULL || path == NULL)
        return o_SHGetFolderPathA ? o_SHGetFolderPathA(owner, csidl, token, flags, path) : E_FAIL;
    if (FvplW2A(dir, path, FVPL_MAX_PATH) <= 0)
        return o_SHGetFolderPathA(owner, csidl, token, flags, path);
    FvplLog("SHGetFolderPathA redirected");
    return S_OK;
}

static HRESULT WINAPI Hook_SHGetFolderPathW(HWND owner, int csidl, HANDLE token,
                                            DWORD flags, LPWSTR path) {
    const WCHAR *dir;
    if (o_SHGetFolderPathW == NULL) return E_FAIL;
    if (!FvplReady() || !folder_redirects(csidl) ||
        (dir = FvplSaveDirW()) == NULL || path == NULL)
        return o_SHGetFolderPathW(owner, csidl, token, flags, path);
    if (pv_len(dir) >= FVPL_MAX_PATH) return E_INVALIDARG;
    lstrcpyW(path, dir);
    FvplLog("SHGetFolderPathW redirected");
    return S_OK;
}

static HRESULT WINAPI Hook_SHGetKnownFolderPath(const GUID *rguid, DWORD flags,
                                                HANDLE token, PWSTR *path) {
    const WCHAR *dir;
    if (o_SHGetKnownFolderPath == NULL || rguid == NULL || path == NULL ||
        !FvplReady() || !g_cfg.save_redirect ||
        memcmp(rguid, &FOLDERID_Documents_local, sizeof(GUID)) != 0 ||
        (dir = FvplSaveDirW()) == NULL)
        return o_SHGetKnownFolderPath ?
                   o_SHGetKnownFolderPath(rguid, flags, token, path) : E_NOTIMPL;
    {
        size_t cb = (pv_len(dir) + 1) * sizeof(WCHAR);
        PWSTR w = (PWSTR)CoTaskMemAlloc(cb);
        if (!w) return E_OUTOFMEMORY;
        memcpy(w, dir, cb);
        *path = w;
        FvplLog("SHGetKnownFolderPath redirected");
        return S_OK;
    }
}

/* A -> W (虚拟代码页) 失败就退回原 A 版, 绝不返回“文件不存在”这种假结果 */
static int io_ready(void) { return FvplReady() && g_cfg.virtualize_files; }

static HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                                      LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                      DWORD flags, HANDLE templ) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_CreateFileA(name, access, share, sa, disp, flags, templ);
    FvplMigratePathW(w);              /* 第一次读这份旧存档时先把这一份搬过来 */
    return CreateFileW(w, access, share, sa, disp, flags, templ);
}

static void find_data_to_a(const WIN32_FIND_DATAW *win, WIN32_FIND_DATAA *ans) {
    ans->dwFileAttributes = win->dwFileAttributes;
    ans->ftCreationTime = win->ftCreationTime;
    ans->ftLastAccessTime = win->ftLastAccessTime;
    ans->ftLastWriteTime = win->ftLastWriteTime;
    ans->nFileSizeHigh = win->nFileSizeHigh;
    ans->nFileSizeLow = win->nFileSizeLow;
    ans->dwReserved0 = win->dwReserved0;
    ans->dwReserved1 = win->dwReserved1;
    FvplW2A(win->cFileName, ans->cFileName, 260);
    {
        char alt[14];
        alt[0] = 0;
        FvplW2A(win->cAlternateFileName, alt, 14);
        lstrcpyA(ans->cAlternateFileName, alt);
    }
}

static BOOL WINAPI Hook_FindFirstFileA(LPCSTR pat, LPWIN32_FIND_DATAA data) {
    WCHAR w[FVPL_MAX_PATH];
    WIN32_FIND_DATAW d;
    HANDLE h;
    if (!io_ready() || pat == NULL || data == NULL || !FvplA2W(pat, w, FVPL_MAX_PATH))
        return o_FindFirstFileA(pat, data);
    FvplMigratePathW(w);              /* 枚举存档目录也算"要用这份旧档" */
    h = FindFirstFileW(w, &d);
    if (h == INVALID_HANDLE_VALUE) { SetLastError(GetLastError()); return FALSE; }
    find_track_add(h);
    find_data_to_a(&d, data);
    return TRUE;
}

static BOOL WINAPI Hook_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA data) {
    WIN32_FIND_DATAW d;
    if (!io_ready() || data == NULL || !find_track_take(h))
        return o_FindNextFileA(h, data);
    find_track_add(h);
    if (!FindNextFileW(h, &d)) return FALSE;
    find_data_to_a(&d, data);
    return TRUE;
}

static BOOL WINAPI Hook_FindClose(HANDLE h);

static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR name) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_GetFileAttributesA(name);
    FvplMigratePathW(w);
    return GetFileAttributesW(w);
}

static BOOL WINAPI Hook_GetFileAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS lvl, LPVOID info) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_GetFileAttributesExA(name, lvl, info);
    FvplMigratePathW(w);
    return GetFileAttributesExW(w, lvl, info);
}

static BOOL WINAPI Hook_DeleteFileA(LPCSTR name) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_DeleteFileA(name);
    return DeleteFileW(w);
}

static BOOL WINAPI Hook_CopyFileA(LPCSTR src, LPCSTR dst, BOOL fail_if_exists) {
    WCHAR a[FVPL_MAX_PATH], b[FVPL_MAX_PATH];
    if (!io_ready() || !src || !dst || !FvplA2W(src, a, FVPL_MAX_PATH) ||
        !FvplA2W(dst, b, FVPL_MAX_PATH))
        return o_CopyFileA(src, dst, fail_if_exists);
    return CopyFileW(a, b, fail_if_exists);
}

static BOOL WINAPI Hook_MoveFileA(LPCSTR src, LPCSTR dst) {
    WCHAR a[FVPL_MAX_PATH], b[FVPL_MAX_PATH];
    if (!io_ready() || !src || !dst || !FvplA2W(src, a, FVPL_MAX_PATH) ||
        !FvplA2W(dst, b, FVPL_MAX_PATH))
        return o_MoveFileA(src, dst);
    return MoveFileW(a, b);
}

static BOOL WINAPI Hook_MoveFileExA(LPCSTR src, LPCSTR dst, DWORD flags) {
    WCHAR a[FVPL_MAX_PATH], b[FVPL_MAX_PATH];
    if (!io_ready() || !src || !dst || !FvplA2W(src, a, FVPL_MAX_PATH) ||
        !FvplA2W(dst, b, FVPL_MAX_PATH))
        return o_MoveFileExA(src, dst, flags);
    return MoveFileExW(a, b, flags);
}

static BOOL WINAPI Hook_CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES sa) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_CreateDirectoryA(name, sa);
    return CreateDirectoryW(w, sa);
}

static BOOL WINAPI Hook_RemoveDirectoryA(LPCSTR name) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_RemoveDirectoryA(name);
    return RemoveDirectoryW(w);
}

static BOOL WINAPI Hook_SetCurrentDirectoryA(LPCSTR name) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_SetCurrentDirectoryA(name);
    return SetCurrentDirectoryW(w);
}

static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR name) {
    WCHAR w[FVPL_MAX_PATH];
    if (!io_ready() || name == NULL || !FvplA2W(name, w, FVPL_MAX_PATH))
        return o_LoadLibraryA(name);
    return LoadLibraryExW(w, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

static DWORD WINAPI Hook_GetModuleFileNameA(HMODULE mod, LPSTR buf, DWORD cch) {
    WCHAR w[FVPL_MAX_PATH];
    DWORD n;
    if (!io_ready() || buf == NULL || cch == 0)
        return o_GetModuleFileNameA(mod, buf, cch);
    n = GetModuleFileNameW(mod, w, FVPL_MAX_PATH);
    if (n == 0 || n >= FVPL_MAX_PATH) return o_GetModuleFileNameA(mod, buf, cch);
    if (FvplW2A(w, buf, (int)cch) <= 0) return o_GetModuleFileNameA(mod, buf, cch);
    return (DWORD)lstrlenA(buf);
}

static const FvplPatch s_tbl[] = {
    { "shell32.dll", "SHGetFolderPathA",       (void *)Hook_SHGetFolderPathA,       (void **)&o_SHGetFolderPathA },
    { "shell32.dll", "SHGetFolderPathW",       (void *)Hook_SHGetFolderPathW,       (void **)&o_SHGetFolderPathW },
    { "shell32.dll", "SHGetKnownFolderPath",   (void *)Hook_SHGetKnownFolderPath,   (void **)&o_SHGetKnownFolderPath },
    { "kernel32.dll", "FindClose",             (void *)Hook_FindClose,              (void **)&o_FindClose },
    { "kernel32.dll", "CreateFileA",           (void *)Hook_CreateFileA,            (void **)&o_CreateFileA },
    { "kernel32.dll", "FindFirstFileA",        (void *)Hook_FindFirstFileA,         (void **)&o_FindFirstFileA },
    { "kernel32.dll", "FindNextFileA",         (void *)Hook_FindNextFileA,          (void **)&o_FindNextFileA },
    { "kernel32.dll", "GetFileAttributesA",    (void *)Hook_GetFileAttributesA,     (void **)&o_GetFileAttributesA },
    { "kernel32.dll", "GetFileAttributesExA",  (void *)Hook_GetFileAttributesExA,   (void **)&o_GetFileAttributesExA },
    { "kernel32.dll", "DeleteFileA",           (void *)Hook_DeleteFileA,            (void **)&o_DeleteFileA },
    { "kernel32.dll", "CopyFileA",             (void *)Hook_CopyFileA,              (void **)&o_CopyFileA },
    { "kernel32.dll", "MoveFileA",             (void *)Hook_MoveFileA,              (void **)&o_MoveFileA },
    { "kernel32.dll", "MoveFileExA",           (void *)Hook_MoveFileExA,            (void **)&o_MoveFileExA },
    { "kernel32.dll", "CreateDirectoryA",      (void *)Hook_CreateDirectoryA,       (void **)&o_CreateDirectoryA },
    { "kernel32.dll", "RemoveDirectoryA",      (void *)Hook_RemoveDirectoryA,       (void **)&o_RemoveDirectoryA },
    { "kernel32.dll", "SetCurrentDirectoryA",  (void *)Hook_SetCurrentDirectoryA,   (void **)&o_SetCurrentDirectoryA },
    { "kernel32.dll", "LoadLibraryA",          (void *)Hook_LoadLibraryA,           (void **)&o_LoadLibraryA },
    { "kernel32.dll", "GetModuleFileNameA",    (void *)Hook_GetModuleFileNameA,     (void **)&o_GetModuleFileNameA },
};

const FvplPatch *FvplTableIo(int *count) {
    static int inited = 0;
    if (!inited) { InitializeCriticalSectionAndSpinCount(&s_find_lock, 0x80000000);
                   s_find_lock_ok = 1; inited = 1; }
    *count = (int)(sizeof(s_tbl) / sizeof(s_tbl[0]));
    return s_tbl;
}

/* FindClose: 摘掉追踪后必须原样透传, 否则句柄泄漏 */
static BOOL WINAPI Hook_FindClose(HANDLE h) {
    find_track_take(h);
    if (o_FindClose == NULL) return FALSE;
    return o_FindClose(h);
}
