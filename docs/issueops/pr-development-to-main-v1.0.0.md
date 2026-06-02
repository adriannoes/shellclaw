# PR body — `development` → `main` (v1.0.0)

**Title:** `release: v1.0.0 edge Jetson foundation`

**Create draft PR:**

```bash
gh pr create --repo adriannoes/shellclaw \
  --base main \
  --head development \
  --title "release: v1.0.0 edge Jetson foundation" \
  --body-file docs/issueops/pr-development-to-main-v1.0.0.md \
  --draft
```

---

## Summary

ShellClaw **v1.0.0** — Phase 5 edge release: Jetson Orin Nano Super primary target, GPIO/I2C tools, CUDA local inference (`llama-server`), signed ASAP manifest, gateway `/hardware` UI (sensor/camera panels deferred to v1.2).

**Merge gate:** Jetson sign-off issue closed with all boxes checked — see linked issue below.

## Evidence

| Artifact | Link |
|----------|------|
| Security self-audit | [`docs/SECURITY.md`](../SECURITY.md) |
| Benchmarks (Jetson rows filled) | [`docs/BENCHMARKS.md`](../BENCHMARKS.md) |
| Changelog | [`CHANGELOG.md`](../../CHANGELOG.md) § [1.0.0] |
| Jetson operator checklist | [`docs/JETSON_SIGNOFF.md`](../JETSON_SIGNOFF.md) |
| Release runbook | [`docs/RELEASE_V1.0.md`](../RELEASE_V1.0.md) |

## Jetson sign-off

- **Tracking issue:** _(paste URL — e.g. #NNN "v1.0.0 Jetson sign-off")_
- **On-device runner:** `SHELLCLAW_HW_TEST=1 make test_hardware_on_device` — **PASS** on commit: `________`
- **Manual checklist:** B1–B6 complete per [`JETSON_SIGNOFF.md`](../JETSON_SIGNOFF.md)

## Pre-merge verification (x86 / CI)

- [ ] `CI=true GATEWAY=1 make clean && CI=true GATEWAY=1 make test`
- [ ] `make static` — zero cppcheck findings
- [ ] `make test-sanitize` — ASan + UBSan green
- [ ] `gpio-mockup` ritual per [`CONTRIBUTING.md`](../../CONTRIBUTING.md)
- [ ] `make release` binary < 600 KB

## Post-merge (maintainer — not in this PR)

1. Tag and push — see [`RELEASE_V1.0.md`](../RELEASE_V1.0.md) Phase C
2. Wait for [`publish-manifest.yml`](../../.github/workflows/publish-manifest.yml) → GitHub Pages manifest
3. ASAP marketplace IssueOps — [`register-agent-prefill.md`](register-agent-prefill.md)
4. Verify listing — [`VERIFY_MARKETPLACE.md`](VERIFY_MARKETPLACE.md); update [`MARKETPLACE_STATUS.md`](../MARKETPLACE_STATUS.md)

## Test plan

- [ ] Reviewer confirms Jetson issue closed with benchmark table updates in `BENCHMARKS.md`
- [ ] CI green on `development` at merge SHA
- [ ] No v1.2-deferred features claimed in release notes (sensors, camera E2E, deferred skills)
