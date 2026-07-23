/* Minimal ENGINE stub for building Linux 4.9 scripts against OpenSSL 3.x.
 * OpenSSL 3.x removed the ENGINE API. This shim provides enough structure
 * for extract-cert.c to compile. The ENGINE code paths are only reachable
 * when CONFIG_SYSTEM_TRUSTED_KEYS is non-empty; our target has it empty. */
#ifndef OPENSSL_ENGINE_COMPAT_H
#define OPENSSL_ENGINE_COMPAT_H
#include <openssl/ssl.h>
typedef struct engine_st ENGINE;
static inline void ENGINE_load_builtin_engines(void) {}
static inline ENGINE *ENGINE_by_id(const char *id) { return NULL; }
static inline int ENGINE_init(ENGINE *e) { return 0; }
static inline void ENGINE_finish(ENGINE *e) {}
static inline int ENGINE_ctrl_cmd_string(ENGINE *e, const char *cmd,
    const char *arg, int cmd_optional) { return 0; }
static inline int ENGINE_ctrl_cmd(ENGINE *e, const char *cmd, long i,
    void *p, void (*f)(void), int cmd_optional) { return 0; }
#endif
