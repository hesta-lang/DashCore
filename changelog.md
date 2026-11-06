# DashCore changelog

## 0.7.1 — Render-visit diagnostics

- Count actual `GameObject` visits through `CCNode::visit` while experimental culling is disabled.
- Sample visit counts once every 30 draw frames to keep diagnostic overhead low.
- Clarify that `dontCreateObjects` is logged only as a raw Geometry Dash initialization argument; it is not used to classify a session as a preview or a normal level.
- Harden the GLES compute probe by checking the SSBO readback result after unmapping.
- Keep GPU visibility testing diagnostic-only. No live GPU culling, object hiding, trigger changes, or renderer replacement is enabled.

## 0.7.0 — GLES compute visibility probe

- Add an Android GLES 3.1+ compute-shader and SSBO capability test.
- Collect real scene GameObjects and compare a GPU visibility mask against a CPU reference.
- Preserve the original Geometry Dash renderer; the GPU test does not modify visibility or draw calls.
- Keep experimental CPU render culling opt-in and disabled by default.
