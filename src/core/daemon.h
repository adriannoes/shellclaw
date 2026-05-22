/**
 * @file daemon.h
 * @brief Double-fork daemon mode, PID file, and log redirection (0600).
 */

#ifndef SHELLCLAW_DAEMON_H
#define SHELLCLAW_DAEMON_H

#ifdef __cplusplus
extern "C" {
#endif

/** Enable or disable daemon mode before enter_daemon_mode(). */
void daemon_set_want(int want);

/**
 * Classic double-fork daemon with session detach. No-op when want was not set.
 * @return 0 on success, -1 on error.
 */
int enter_daemon_mode(void);

/**
 * Redirect stdio to log (0600) and write PID file with advisory lock.
 * Called internally after the second fork; exposed for tests if needed.
 * @return 0 on success, -1 on error.
 */
int finish_daemon_stdio_and_pid(void);

/** Close PID fd and remove PID file on shutdown. */
void daemon_pid_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* SHELLCLAW_DAEMON_H */
