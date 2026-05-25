# ASAP marketplace registration status

| Field | Value |
|-------|--------|
| **Status** | `pending` |
| **Slice task** | [6.3 IssueOps submission](../.cursor/dev-planning/tasks/phase5/03-inference-trust-marketplace.md) |
| **Manifest URL** | https://adriannoes.github.io/shellclaw/manifest.json |
| **Submitted issue** | _(operator: paste GitHub issue URL after filing)_ |

## Operator checklist (6.3)

Artifacts in-repo are complete; **live marketplace verification** is an operator post-condition.

- [ ] Pre-submit: manifest URL live + `verify_manifest` OK — see [`docs/issueops/register-agent-prefill.md`](issueops/register-agent-prefill.md)
- [ ] File [Register Agent](https://github.com/adriannoes/asap-protocol/issues/new?template=register_agent.yml) using prefill block
- [ ] Record submitted issue URL in **Submitted issue** row above
- [ ] Bot merge complete
- [ ] Post-submit verify — [`docs/issueops/VERIFY_MARKETPLACE.md`](issueops/VERIFY_MARKETPLACE.md)
- [ ] Set **Status** to `listed` when Browse UI + Demo badge + filters pass

## Related docs

- [`docs/ASAP.md`](ASAP.md) — IssueOps field table and human checklist
- [`docs/fixtures/shellclaw-v1.0-registry-entry.json`](fixtures/shellclaw-v1.0-registry-entry.json) — expected post-bot registry shape
