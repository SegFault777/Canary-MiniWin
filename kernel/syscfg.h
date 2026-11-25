/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_SYSCFG_H
#define MW_SYSCFG_H

/* syscfg.h -- the settings that outlive a restart, and who the user is (1.1-alpha-14).
 *
 * Until now only the screen size and the system font survived a restart (bits of the boot sector's byte 440); the language, the input methods, the desktop
 * colour and the time zone went back to their defaults every boot. They now live in /etc/SYSTEM.CFG, a few lines of text on the root volume:
 *
 *     oobe=1                 the machine has been set up (its presence keeps the first-run setup from showing)
 *     user=Name              the user's name (the logon prompt shows it, `whoami` says it)
 *     lang=ko                the language of the menus: en or ko
 *     ime_en=1 ime_ko=1 ime_cur=0     the input methods Right Alt cycles through, and the one in use
 *     desk=2                 the desktop colour (an index of setting_desk[], apps/setting.h)
 *     tz=NN dst=1            the time zone (an index of tz_zones[], tz.h) and "adjust for daylight saving"
 *
 * Every key is optional; a missing one keeps its default. The file is read once, at the first frame (oobe_tick(), apps/oobe.h), and written by the
 * first-run setup and -- through syscfg_touch() -- whenever Settings changes one of these values on a machine that has been set up.
 *
 * THE USER'S PASSWORD is /etc/USER.PWD, in the format of the root password (terminal.h): 16 bytes of salt and a SHA-256 hash stretched over 1000 rounds,
 * both in hex, never the password itself. No file = no password = no logon prompt. It is separate from the root password, which only MiniShell's
 * `root` command asks for.
 *
 * A LIVE DISC OR A FLOPPY is not an installation: it can not keep anything (the disc is read-only, RAM is gone at the next boot), so nothing is read
 * or written there, there is no first-run setup, and the user is simply "LIVE". Installing to a disk (the installer formats a fresh root volume, without
 * these files) is what makes the first boot of the installed system run the setup.
 * Needs vfs.h, tz.h, lang.h, terminal.h (the password hashing), setting.h (the desktop colours), blk.h, live.h. */

#define SYSCFG_FILE   "/etc/SYSTEM.CFG"
#define USERPWD_FILE  "/etc/USER.PWD"

static int  syscfg_found = 0;                          /* a SYSTEM.CFG was read (or written by the setup): this machine is set up */

/* 1 on a live disc or a floppy: nothing is kept, there is no setup, the user is LIVE. */
static int sys_is_live(void) { return live_boot || (blk_boot_dev >= BLK_FD_FIRST && blk_boot_dev < BLK_CD_FIRST); }

static void syscfg_put(char *b, u32 *n, u32 cap, const char *s) { append_str(b, n, cap, s); }
static void syscfg_putnum(char *b, u32 *n, u32 cap, int v) {
    char d[8]; int k = 0;
    if (v <= 0) d[k++] = '0';
    while (v > 0 && k < 7) { d[k++] = (char)('0' + v % 10); v /= 10; }
    while (k) { char c[2] = { d[--k], 0 }; append_str(b, n, cap, c); }
}

/* Writes the file from the values in use now. 0 on a live disc (nothing to keep it on) or when the write fails. */
static int syscfg_save(void) {
    if (sys_is_live()) return 0;
    char b[256]; u32 n = 0; b[0] = 0;
    syscfg_put(b, &n, sizeof b, "oobe=1\nuser="); syscfg_put(b, &n, sizeof b, sys_user);
    syscfg_put(b, &n, sizeof b, sys_language == LANG_KOREAN ? "\nlang=ko\nime_en=" : "\nlang=en\nime_en=");
    syscfg_putnum(b, &n, sizeof b, ime_enabled[IME_ENGLISH]); syscfg_put(b, &n, sizeof b, "\nime_ko=");
    syscfg_putnum(b, &n, sizeof b, ime_enabled[IME_KOREAN]);  syscfg_put(b, &n, sizeof b, "\nime_cur=");
    syscfg_putnum(b, &n, sizeof b, current_ime);              syscfg_put(b, &n, sizeof b, "\ndesk=");
    syscfg_putnum(b, &n, sizeof b, setting_desk_idx);         syscfg_put(b, &n, sizeof b, "\ntz=");
    syscfg_putnum(b, &n, sizeof b, tz_zone);                  syscfg_put(b, &n, sizeof b, "\ndst=");
    syscfg_putnum(b, &n, sizeof b, tz_dst_auto);              syscfg_put(b, &n, sizeof b, "\n");
    int was = vfs_priv; vfs_priv = 1;                      /* /etc is a system directory */
    int e = vfs_mkdir("/etc");
    e = (e == MWFS_OK || e == MWFS_E_EXIST) ? vfs_write(SYSCFG_FILE, (const u8 *)b, n, 0) : e;
    vfs_priv = was;
    if (e == MWFS_OK) syscfg_found = 1;
    return e == MWFS_OK;
}
/* Called by Settings after it changed the language, an input method, the desktop colour or the time zone: keeps the new value if the machine is set up. */
static void syscfg_touch(void) { if (syscfg_found) syscfg_save(); }

static int syscfg_int(const char *v) { int r = 0; while (*v >= '0' && *v <= '9') r = r * 10 + (*v++ - '0'); return r; }
/* A user name must be printable ASCII or Hangul; anything else (a damaged file) is not used. */
static int syscfg_name_ok(const char *s) {
    u32 l = tf_len(s);
    if (l == 0 || l > SYS_USER_MAX) return 0;
    for (u32 i = 0; i < l; ) {
        if ((unsigned char)s[i] >= 32 && (unsigned char)s[i] < 127) i++;
        else if (tf_hangul3(s + i, l - i)) i += 3;
        else return 0;
    }
    return 1;
}
/* Reads the file and applies it. 0 = there is none (the machine has not been set up). */
static int syscfg_load(void) {
    u8 buf[300]; u32 len = 0, aux = 0;
    if (vfs_read(SYSCFG_FILE, buf, sizeof buf - 1, &len, &aux) != MWFS_OK || len == 0) return 0;
    buf[len] = 0;
    for (u32 p = 0; p < len; ) {
        u32 e = p; while (e < len && buf[e] != '\n') e++;
        buf[e] = 0;
        char *k = (char *)buf + p, *v = k;
        while (*v && *v != '=') v++;
        if (*v == '=') {
            *v++ = 0;
            if (!tf_len(k)) { p = e + 1; continue; }
            if (mwfs_name_eq(k, "lang")) sys_language = (v[0] == 'k' && v[1] == 'o') ? LANG_KOREAN : LANG_ENGLISH;
            else if (mwfs_name_eq(k, "ime_en")) ime_enabled[IME_ENGLISH] = syscfg_int(v) != 0;
            else if (mwfs_name_eq(k, "ime_ko")) ime_enabled[IME_KOREAN] = syscfg_int(v) != 0;
            else if (mwfs_name_eq(k, "ime_cur")) current_ime = syscfg_int(v) == IME_KOREAN ? IME_KOREAN : IME_ENGLISH;
            else if (mwfs_name_eq(k, "desk")) { int d = syscfg_int(v); if (d >= 0 && d < SETTING_DESK_N) { setting_desk_idx = d; setting_desk_apply(); } }
            else if (mwfs_name_eq(k, "tz")) { int z = syscfg_int(v); if (z >= 0 && z < TZ_N) tz_zone = z; }
            else if (mwfs_name_eq(k, "dst")) tz_dst_auto = syscfg_int(v) != 0;
            else if (mwfs_name_eq(k, "user")) { if (syscfg_name_ok(v)) mwfs_strcpy(sys_user, v, sizeof sys_user); }
        }
        p = e + 1;
    }
    ime_ensure_current_enabled();
    syscfg_found = 1;
    return 1;
}

/* ---- the user's password ---- */
static int upw_load(u8 salt[16], u8 hash[32]) {
    u8 buf[128]; u32 len = 0;
    if (vfs_read(USERPWD_FILE, buf, sizeof buf, &len, 0) != MWFS_OK || len < 96) return 0;
    return term_pw_unhex((const char *)buf, salt, 16) && term_pw_unhex((const char *)buf + 32, hash, 32);
}
static int upw_is_set(void) { u8 s[16], h[32]; return upw_load(s, h); }
static int upw_check(const char *pw) {
    u8 salt[16], want[32], got[32];
    if (!upw_load(salt, want)) return 0;
    term_pw_hash(salt, pw, got);
    u8 diff = 0; for (int i = 0; i < 32; i++) diff |= (u8)(want[i] ^ got[i]);           /* no early exit */
    return diff == 0;
}
/* Stores the password; an empty one removes the file (no password). 1 = done. */
static int upw_store(const char *pw) {
    int was = vfs_priv, ok;
    vfs_priv = 1;
    if (!pw[0]) {
        vfs_node_t n;
        ok = vfs_stat(USERPWD_FILE, &n) != MWFS_OK || vfs_rm(USERPWD_FILE) == MWFS_OK;
    } else {
        u8 salt[16], h[32]; char file[96];
        tls_rng_mix_hw(); tls_rng_bytes(salt, 16);
        term_pw_hash(salt, pw, h);
        term_pw_hex(file, salt, 16); term_pw_hex(file + 32, h, 32);
        ok = vfs_write(USERPWD_FILE, (const u8 *)file, 96, 0) == MWFS_OK;
    }
    vfs_priv = was;
    return ok;
}

#endif
