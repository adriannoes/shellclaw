/**
 * @file sandbox.h
 * @brief Process sandbox API: isolated execution with namespaces, timeout, and cgroups v2.
 *
 * On Linux, sandbox_exec uses clone(2) with PID/mount/network namespace isolation,
 * optional cgroups v2 resource limits, and a hard timeout with SIGKILL.
 * On other platforms (macOS, BSDs) it falls back to a plain fork+exec and logs a warning.
 */
#ifndef SHELLCLAW_SANDBOX_H
#define SHELLCLAW_SANDBOX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of bytes that sandbox_exec writes into the output buffer. */
#define SANDBOX_OUTPUT_MAX (256 * 1024)

/**
 * Configuration for sandbox_exec. Zero-initialise and set only the fields you need;
 * unset numeric fields fall back to safe defaults.
 */
typedef struct sandbox_config {
    /** Absolute path to the workspace directory (chdir target inside sandbox). NULL = skip chdir. */
    const char *workspace_path;
    /** Memory ceiling in bytes written to cgroup memory.max. 0 = use default (64 MiB). */
    size_t memory_max_bytes;
    /**
     * CPU quota as a string for cgroups v2 "cpu.max" (e.g. "50000 100000" = 50 % on one CPU).
     * NULL = no CPU limit.
     */
    const char *cpu_max;
    /**
     * Base path for cgroup hierarchy (default: "/sys/fs/cgroup").
     * NULL = use default.
     */
    const char *cgroup_base;
} sandbox_config_t;

/**
 * Execute @p cmd inside an isolated child process and capture output.
 *
 * On Linux, enters a user namespace when needed, then unshares
 * CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWNET. Isolation failure is fail-closed
 * (returns -1). Applies cgroups v2 limits when available; degrades if not.
 * Kills the child with SIGKILL if @p timeout_ms elapses before exit.
 *
 * On non-Linux platforms the function executes the command via fork()+exec()
 * without namespace isolation and emits a warning to stderr.
 *
 * @param cmd        Shell command string passed to "/bin/sh -c".
 * @param out        Buffer for captured stdout+stderr. Always NUL-terminated on success.
 * @param out_cap    Capacity of @p out (including NUL byte). Must be > 0.
 * @param timeout_ms Maximum wall-clock milliseconds before SIGKILL. 0 = default (10 000 ms).
 * @param cfg        Optional sandbox configuration. NULL = use built-in defaults.
 * @return           0 on success (command ran; check output for exit status text),
 *                   -1 on system error (pipe/fork/clone failure).
 */
int sandbox_exec(const char *cmd, char *out, size_t out_cap,
                 int timeout_ms, const sandbox_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_SANDBOX_H */
