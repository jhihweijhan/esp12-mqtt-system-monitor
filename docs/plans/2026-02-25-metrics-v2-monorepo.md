# Metrics v2 Monorepo Rewrite Plan

> Execution branch: `feat/metrics-v2-rewrite`

## Scope

- Move firmware into `apps/firmware`.
- Add sender implementations in the same repo under `apps/sender`.
- Introduce `metrics/v2` topic + compact payload protocol.
- Rework firmware data plane for lower redraw and parse overhead.

## Deliverables

- Firmware v2 transport/parser/store/display pipeline.
- Web API v2 endpoints.
- Python sender v2 (cross-platform).
- Go sender v2 (single binary path).
- Protocol documentation and baseline tests.
