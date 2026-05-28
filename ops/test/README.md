# OSCAR Test Suite

Layer-by-layer + end-to-end verification scenarios. Each `layerN_*.md` documents:

1. **Scope** — which OSCAR component is exercised
2. **Preconditions** — what must already be running
3. **Procedure** — exact `curl` (or alternate) commands to run
4. **Expected result** — pass criteria, with sample output
5. **Failure modes** — common red flags

> Default test base: `http://localhost:8080`. Override via `TEST_BASE`.
> All shell helpers live in `ops/scripts/`. Run all via `make test` from `ops/`.

| # | Layer            | Doc                                              | Script                                 |
|---|------------------|--------------------------------------------------|----------------------------------------|
| 1 | Camera           | [layer1_camera.md](layer1_camera.md)             | `test_layer1_camera.sh`                |
| 2 | Inference        | [layer2_inference.md](layer2_inference.md)       | `test_layer2_inference.sh`             |
| 3 | Tracking         | [layer3_tracking.md](layer3_tracking.md)         | `test_layer3_tracking.sh`              |
| 4 | Localization     | [layer4_localization.md](layer4_localization.md) | `test_layer4_localization.sh`          |
| 5 | Storage          | [layer5_storage.md](layer5_storage.md)           | `test_layer5_storage.sh`               |
| 6 | Server (HTTP/SSE)| [layer6_server.md](layer6_server.md)             | `test_layer6_server.sh`                |
| – | End-to-End       | [e2e.md](e2e.md)                                 | `test_e2e.sh`                          |

Quick run:

```bash
cd ops
make up         # start oscar-core + oscar-web
make test       # run all layer tests
make logs       # tail container logs
make down
```
