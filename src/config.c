/*
 * Configuration file, in the original binary format (Config_Load 0x402900,
 * Config_Save 0x402b30). The original kept it as %WINDIR%\ASTRO_CC.CFG; the
 * port keeps it in the user's settings folder (astro_pref_dir), under the same
 * name, so a file from the original can be copied over.
 *
 * Layout (little-endian, read and written in this order):
 *   'RHO!' magic, run counter, players, control P1, control P2,
 *   0x20 + 0x1a + 0x40 bytes (game settings / high-score data),
 *   display mode index, wave volume, CD volume, difficulty.
 */
#include "astro.h"

#include <string.h>

#define CFG_MAGIC 0x214f4852u   /* "RHO!" */

static const struct { uint32_t va, size; } k_fields[] = {
    {0x55cbe0, 4}, {0x46f440, 4}, {0x46f444, 4}, {0x46f448, 4},
    {0x46f470, 0x20}, {0x46f450, 0x1a}, {0x46f498, 0x40},
    {0x46ec04, 4}, {0x46ec08, 4}, {0x46ec0c, 4}, {0x556abc, 4},
};

static bool g_save_enabled;   /* DAT_00556830: false if an existing file was not ours */

static void config_path(char *out, size_t size)
{
    snprintf(out, size, "%sASTRO_CC.CFG", astro_pref_dir());
}

void config_load(void)
{
    g_save_enabled = !astro_no_user_files;
    if (astro_no_user_files) return;
    char path[1200];
    config_path(path, sizeof path);
    FILE *f = astro_fopen(path, "rb");
    if (!f) return;                        /* no file yet: defaults, like a first run */
    uint32_t magic = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != CFG_MAGIC) {
        /* the original refused to use (or overwrite) a foreign file */
        astro_log("%s is not a configuration file; settings will not be saved", path);
        g_save_enabled = false;
        fclose(f);
        return;
    }
    for (size_t i = 0; i < sizeof k_fields / sizeof k_fields[0]; i++) {
        uint8_t buf[0x40];
        if (fread(buf, 1, k_fields[i].size, f) != k_fields[i].size) break;   /* partial file: stop */
        memcpy(&mem[k_fields[i].va], buf, k_fields[i].size);
    }
    fclose(f);
}

void config_save(void)
{
    if (!g_save_enabled) return;
    char path[1200];
    config_path(path, sizeof path);
    FILE *f = astro_fopen(path, "wb");
    if (!f) { astro_log("cannot write %s", path); return; }
    uint32_t magic = CFG_MAGIC;
    bool ok = fwrite(&magic, 4, 1, f) == 1;
    for (size_t i = 0; ok && i < sizeof k_fields / sizeof k_fields[0]; i++)
        ok = fwrite(&mem[k_fields[i].va], 1, k_fields[i].size, f) == k_fields[i].size;
    if (fclose(f) != 0 || !ok) astro_log("writing %s failed", path);
}
