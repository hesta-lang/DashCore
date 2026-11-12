# DashCore — Android rendering optimization

## 0.10.1-alpha — Android geometry-scan scheduling fix

- Moves the isolated, read-only scene-geometry scan to the confirmed `PlayLayer::postUpdate` hook after 600 gameplay updates.
- Keeps `CCDirector::drawScene` as optional timing telemetry; Android builds no longer depend on that hook being invoked to produce geometry diagnostics.
- AutoTest now waits for the actual `DIAGNOSTIC-COST` completion marker, then validates candidate, bounds and parent-chain records.
- The separate diagnostic launch keeps static culling OFF. Normal FPS benchmark passes keep expensive diagnostics OFF.
- Experimental culling remains OFF by default; no rendering behavior is changed by the diagnostic pass.
- Diagnostic alpha only. Cloud emulator results do not replace physical Huawei Y9s / Mali-G51 validation.
