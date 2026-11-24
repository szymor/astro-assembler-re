/* Small platform-neutral helpers. */
#include "astro.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

bool astro_no_user_files;

void astro_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

int astro_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d || !*a) return d;
    }
}

uint64_t astro_hash(uint64_t h, const void *data, size_t size)
{
    const uint8_t *p = data;
    if (!h) h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

/* fopen with a UTF-8 path; on Windows the narrow fopen would use the ANSI code
 * page, which breaks non-ASCII folder names (e.g. in the user's profile). */
FILE *astro_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wpath[1024], wmode[16];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) ||
        !MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 16))
        return NULL;
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

void *astro_read_file(const char *path, size_t *size)
{
    FILE *f = astro_fopen(path, "rb");
    if (!f) return NULL;
    char *data = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 1 << 20;
            char *grown = realloc(data, cap);
            if (!grown) { free(data); fclose(f); return NULL; }
            data = grown;
        }
        size_t n = fread(data + len, 1, cap - len, f);
        len += n;
        if (n == 0) break;
    }
    fclose(f);
    if (size) *size = len;
    return data;
}

static void make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        char c = *p;
        *p = 0;
#ifdef _WIN32
        wchar_t w[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) _wmkdir(w);
#else
        mkdir(path, 0755);
#endif
        *p = c;
    }
}

/*
 * Per-user folder for ASTRO_CC.CFG and astro-port.ini, the same place SDL3's
 * SDL_GetPrefPath("", "Astro Assembler") uses, so every frontend shares it:
 *   Linux:   $XDG_DATA_HOME/Astro Assembler/ or ~/.local/share/Astro Assembler/
 *   Windows: %APPDATA%\Astro Assembler\
 *   macOS:   ~/Library/Application Support/Astro Assembler/
 * ASTRO_CONFIG_DIR overrides it.
 */
const char *astro_pref_dir(void)
{
    static char dir[1024];
    if (dir[0]) return dir;
    const char *over = getenv("ASTRO_CONFIG_DIR");
    if (over && *over) {
        snprintf(dir, sizeof dir, "%s/", over);
    } else {
#if defined(_WIN32)
        wchar_t *appdata = _wgetenv(L"APPDATA");
        char base[1024] = ".";
        if (appdata) WideCharToMultiByte(CP_UTF8, 0, appdata, -1, base, sizeof base, NULL, NULL);
        snprintf(dir, sizeof dir, "%s\\Astro Assembler\\", base);
#elif defined(__APPLE__)
        const char *home = getenv("HOME");
        snprintf(dir, sizeof dir, "%s/Library/Application Support/Astro Assembler/", home ? home : ".");
#else
        const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
        if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/Astro Assembler/", xdg);
        else snprintf(dir, sizeof dir, "%s/.local/share/Astro Assembler/", home ? home : ".");
#endif
    }
    make_dirs(dir);
    return dir;
}

bool astro_write_bmp(const char *path, const uint32_t *xrgb, int w, int h)
{
    FILE *f = astro_fopen(path, "wb");
    if (!f) return false;
    uint32_t data = (uint32_t)(w * h * 4);
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t v[] = {54 + data, 0, 54, 40, (uint32_t)w, (uint32_t)-h, 1 | (32u << 16), 0, data, 2835, 2835, 0, 0};
    for (int i = 0; i < 13; i++) memcpy(hdr + 2 + 4 * i, &v[i], 4);
    bool ok = fwrite(hdr, 1, 54, f) == 54 && fwrite(xrgb, 4, (size_t)(w * h), f) == (size_t)(w * h);
    return fclose(f) == 0 && ok;
}
