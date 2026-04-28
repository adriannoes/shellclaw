/**
 * @file asap_version.h
 * @brief ASAP Protocol version string for envelopes and discovery (PRD Phase 3).
 *
 * Use this constant for the envelope @c asap_version field and wherever the
 * wire protocol revision must be asserted (e.g. compliance harnesses).
 */
#ifndef SHELLCLAW_ASAP_PROTOCOL_VERSION_H
#define SHELLCLAW_ASAP_PROTOCOL_VERSION_H

/** ASAP Protocol revision implemented by this build (JSON-RPC envelope field). */
#define ASAP_PROTOCOL_VERSION "2.1"

#endif /* SHELLCLAW_ASAP_PROTOCOL_VERSION_H */
