/**
 * @file allowlist.c
 * @brief Shell-command allowlist: built-in blocklist + workspace realpath checks.
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "sandbox/allowlist.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Built-in blocklist patterns                                          */
/* ------------------------------------------------------------------ */

static const char *const BLOCK_SUBSTRINGS[] = {
    /* Filesystem destroyers */
    "rm -rf /",
    "rm -rf /*",
    "rm -rf / ",
    "rm -fr /",
    "mkfs",
    "fdisk",
    "parted",
    "> /dev/sd",
    "dd if=",
    "dd of=/dev",
    /* System lifecycle */
    "shutdown",
    "reboot",
    "halt",
    "poweroff",
    "init 0",
    "init 6",
    "systemctl poweroff",
    "systemctl reboot",
    "systemctl halt",
    /* Fork bombs */
    ":(){ :|:& };:",
    "fork()",
    ":(){:|:&};:",
    /* Privilege escalation */
    "chmod 777 /",
    "chmod -R 777 /",
    "chown root",
    "sudo rm -rf",
    /* Credential / secret file access */
    "/etc/shadow",
    "/etc/gshadow",
    "~/.ssh/id_",
    "id_rsa",
    "id_ed25519",
    NULL
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void set_reason(char *buf, size_t cap, const char *prefix, const char *detail)
{
    if (!buf || cap == 0) return;
    if (detail && detail[0])
        snprintf(buf, cap, "%s%s", prefix, detail);
    else
        snprintf(buf, cap, "%s", prefix);
    buf[cap - 1] = '\0';
}

/** Return 1 if @p s begins with a path-like character. */
static int has_path_chars(const char *tok)
{
    if (!tok) return 0;
    return tok[0] == '/' || tok[0] == '~' || tok[0] == '.';
}

/**
 * Strip one layer of matching surrounding quotes from @p tok in place.
 * Returns the (possibly advanced) start of the unquoted token.
 */
static char *strip_surrounding_quotes(char *tok)
{
    size_t n;
    if (!tok || !tok[0]) return tok;
    n = strlen(tok);
    if (n >= 2 && ((tok[0] == '\'' && tok[n - 1] == '\'') ||
                   (tok[0] == '"' && tok[n - 1] == '"'))) {
        tok[n - 1] = '\0';
        return tok + 1;
    }
    return tok;
}

/**
 * Return 1 if @p c is allowed inside an absolute path fragment we extract
 * from command text (conservative; stops before shell metacharacters).
 */
static int is_path_body_char(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '/' || c == '.' || c == '_' ||
           c == '-' || c == '+' || c == '%' || c == '@';
}

/**
 * Return 1 when @p p in @p text starts a filesystem absolute path (`/` or `~`),
 * not a slash inside `src/foo`, `3/4`, or a URL scheme `://`.
 */
static int is_fs_absolute_path_start(const char *text, const char *p)
{
    unsigned char prev;
    if (!text || !p || (*p != '/' && *p != '~'))
        return 0;
    if (p == text)
        return 1;
    prev = (unsigned char)p[-1];
    /* URL scheme slashes in http:// and file:// — do not treat them as FS paths. */
    if (*p == '/' && prev == ':')
        return 0;
    if (*p == '/' && p >= text + 2 && p[-1] == '/' && p[-2] == ':')
        return 0;
    /* Relative "src/foo" or "3/4": slash continues an existing token. */
    if (is_path_body_char(prev) && prev != '/')
        return 0;
    return 1;
}

/**
 * Copy a `~` fragment into @p dest, expanding `$HOME` the same way token checks do.
 * @return 0 on success, -1 if the expanded path does not fit.
 */
static int expand_tilde_fragment(const char *fragment, char *dest, size_t dest_cap)
{
    const char *home;
    int n;
    if (!fragment || !dest || dest_cap == 0)
        return -1;
    if (fragment[0] != '~') {
        if (strlen(fragment) >= dest_cap)
            return -1;
        memcpy(dest, fragment, strlen(fragment) + 1);
        return 0;
    }
    home = getenv("HOME");
    if (!home)
        home = "";
    n = snprintf(dest, dest_cap, "%s%s", home, fragment + 1);
    if (n < 0 || (size_t)n >= dest_cap)
        return -1;
    return 0;
}

/**
 * Scan @p text for absolute (~ or /) path fragments and reject any that escape
 * @p workspace_root. Catches quoted / embedded paths the whitespace tokenizer misses
 * (e.g. python3 -c "open('/etc/passwd')").
 * @return 1 if blocked, 0 if all fragments are under the workspace.
 */
static int block_if_embedded_paths_escape(const char *text, const char *workspace_root,
                                          char *reason_buf, size_t reason_cap)
{
    const char *p;
    if (!text || !workspace_root) return 0;
    for (p = text; *p; p++) {
        char fragment[PATH_MAX];
        char expanded[PATH_MAX];
        size_t n = 0;
        const char *start;
        if (!is_fs_absolute_path_start(text, p))
            continue;
        start = p;
        fragment[n++] = *p++;
        while (*p && is_path_body_char((unsigned char)*p) && n + 1 < sizeof(fragment))
            fragment[n++] = *p++;
        fragment[n] = '\0';
        if (expand_tilde_fragment(fragment, expanded, sizeof(expanded)) != 0) {
            set_reason(reason_buf, reason_cap,
                       "command blocked: path escapes workspace: ", fragment);
            return 1;
        }
        if (!allowlist_path_is_under_workspace(expanded, workspace_root)) {
            set_reason(reason_buf, reason_cap,
                       "command blocked: path escapes workspace: ", expanded);
            fprintf(stderr, "allowlist: blocked path outside workspace: %s\n", expanded);
            return 1;
        }
        if (p > start)
            p--;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public: path-under-workspace check (5.4)                             */
/* ------------------------------------------------------------------ */

int allowlist_path_is_under_workspace(const char *path, const char *workspace_root)
{
    char resolved_path[PATH_MAX];
    char resolved_ws[PATH_MAX];
    const char *actual_ws;
    size_t wlen;
    if (!path || !workspace_root || !workspace_root[0]) return 0;
    /* Resolve the workspace root (handles symlinks like macOS /tmp -> /private/tmp). */
    if (realpath(workspace_root, resolved_ws))
        actual_ws = resolved_ws;
    else
        actual_ws = workspace_root;
    wlen = strlen(actual_ws);
    if (realpath(path, resolved_path)) {
        /* Exact match or resolved path starts with resolved workspace + '/' */
        if (strncmp(resolved_path, actual_ws, wlen) == 0) {
            if (resolved_path[wlen] == '\0' || resolved_path[wlen] == '/') return 1;
        }
        return 0;
    }
    /* Path does not exist on disk: check the lexical prefix against resolved workspace. */
    if (strncmp(path, actual_ws, wlen) == 0) {
        if (path[wlen] == '\0' || path[wlen] == '/') return 1;
    }
    /* Also try against the original (unresolved) workspace root. */
    wlen = strlen(workspace_root);
    if (strncmp(path, workspace_root, wlen) == 0) {
        if (path[wlen] == '\0' || path[wlen] == '/') return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public: combined check                                               */
/* ------------------------------------------------------------------ */

int allowlist_check_shell_command(const char *cmd, const allowlist_config_t *cfg,
                                  char *reason_buf, size_t reason_cap)
{
    const char *const *p;
    char ws_resolved[PATH_MAX];
    const char *workspace_root = NULL;
    int workspace_only = 0;
    char *cmd_copy = NULL;
    char *tok;
    char *saveptr;
    if (!cmd) {
        set_reason(reason_buf, reason_cap, "null command", "");
        return 1;
    }
    /* Phase 1: built-in substring blocklist */
    for (p = BLOCK_SUBSTRINGS; *p; p++) {
        if (strstr(cmd, *p) != NULL) {
            set_reason(reason_buf, reason_cap,
                       "command blocked: contains forbidden pattern '", *p);
            if (reason_buf && reason_cap > 0) {
                size_t used = strlen(reason_buf);
                if (used + 2 < reason_cap) {
                    reason_buf[used] = '\'';
                    reason_buf[used + 1] = '\0';
                }
            }
            fprintf(stderr, "allowlist: blocked command containing '%s'\n", *p);
            return 1;
        }
    }
    /* Phase 2: workspace path containment */
    if (!cfg || !cfg->workspace_only || !cfg->workspace_path || !cfg->workspace_path[0])
        return 0;
    workspace_only = cfg->workspace_only;
    (void)workspace_only;
    /* Resolve workspace root once */
    if (!realpath(cfg->workspace_path, ws_resolved)) {
        /* Workspace path does not exist; use as-is. */
        size_t n = strlen(cfg->workspace_path);
        if (n >= PATH_MAX) n = PATH_MAX - 1;
        memcpy(ws_resolved, cfg->workspace_path, n);
        ws_resolved[n] = '\0';
    }
    workspace_root = ws_resolved;
    /*
     * Scan the full command for embedded absolute paths first. Whitespace
     * tokenization alone misses quoted paths (cat '/etc/passwd') and paths
     * inside -c / eval strings. Sandbox namespaces do not chroot, so this
     * scan is the primary workspace FS gate when workspace_only is set.
     */
    if (block_if_embedded_paths_escape(cmd, workspace_root, reason_buf, reason_cap))
        return 1;
    /* Tokenize the command and check each path-like token (incl. relative ./). */
    cmd_copy = strdup(cmd);
    if (!cmd_copy) {
        set_reason(reason_buf, reason_cap, "command blocked: out of memory", "");
        return 1; /* fail-closed on OOM */
    }
    tok = strtok_r(cmd_copy, " \t\n;|&><", &saveptr);
    while (tok) {
        tok = strip_surrounding_quotes(tok);
        if (has_path_chars(tok)) {
            /* Expand a leading tilde naively */
            char expanded[PATH_MAX];
            if (tok[0] == '~') {
                const char *home = getenv("HOME");
                if (home)
                    snprintf(expanded, sizeof(expanded), "%s%s", home, tok + 1);
                else
                    snprintf(expanded, sizeof(expanded), "%s", tok);
                tok = expanded;
            }
            if (!allowlist_path_is_under_workspace(tok, workspace_root)) {
                set_reason(reason_buf, reason_cap,
                           "command blocked: path escapes workspace: ", tok);
                fprintf(stderr, "allowlist: blocked path outside workspace: %s\n", tok);
                free(cmd_copy);
                return 1;
            }
        }
        tok = strtok_r(NULL, " \t\n;|&><", &saveptr);
    }
    free(cmd_copy);
    return 0;
}
