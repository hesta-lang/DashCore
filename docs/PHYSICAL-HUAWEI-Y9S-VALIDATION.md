# Physical Huawei Y9s A/B validation

This procedure is the required device-side check before DashCore can be described as a verified optimization. GitHub-hosted Pixel/SwiftShader measurements are regression checks only; they do not reproduce the Huawei Y9s.

## Target and build

- Phone: Huawei Y9s STK-LX3, Kirin 710F, Mali-G51 MP4, 6 GB RAM.
- Android: 10 / API 29; display: 2340×1080, 60 Hz.
- Geometry Dash: 2.2081; Geode: 5.11.2; Android64.
- Install one successful `.geode` artifact and use that exact file for every run.
- Keep DashCore at the `alpha` prerelease stage until this test is reviewed.

## Controlled A/B procedure

1. Restart the phone before the session. Close background apps and keep battery-saver, display brightness, sound, network state, game settings, and charging state unchanged throughout.
2. Choose one repeatable level and a fixed route lasting about 60 seconds. Avoid changing level, start position, graphics settings, or attempt style between runs.
3. In Geode settings, set **Performance Diagnostics** OFF. This avoids measuring the diagnostic instrumentation itself.
4. Keep the same DashCore package installed. For the baseline, set **General Enable** OFF and fully restart Geometry Dash.
5. Play the fixed route for 60 seconds. Record the run number, completion/attempt outcome, obvious stutters, freezes, and any visual/gameplay anomaly. Do not use screen recording during the timed run because recording can affect performance.
6. Set **General Enable** ON, fully restart Geometry Dash, and repeat the same route for 60 seconds.
7. Repeat for five pairs. Alternate order to reduce warm-up and thermal bias:
   - Pair 1: OFF, then ON
   - Pair 2: ON, then OFF
   - Pair 3: OFF, then ON
   - Pair 4: ON, then OFF
   - Pair 5: OFF, then ON
8. Allow the phone to return to a similar temperature between pairs. If the phone becomes hot, pause the session and note it; do not compare a hot run with a cool run as if conditions were equal.
9. If a crash, missing object, flicker, late appearance, or gameplay difference occurs, stop and preserve the Geode log for that run.

## Results sheet

Fill one row per run. Use `none` when no obvious stutter occurs; describe approximate duration and location when one does.

| Pair | Order | General Enable | Stutters / freezes | Anomaly or notes |
|---|---:|---|---|---|
| 1 | 1 | OFF | | |
| 1 | 2 | ON | | |
| 2 | 1 | ON | | |
| 2 | 2 | OFF | | |
| 3 | 1 | OFF | | |
| 3 | 2 | ON | | |
| 4 | 1 | ON | | |
| 4 | 2 | OFF | | |
| 5 | 1 | OFF | | |
| 5 | 2 | ON | | |

## Separate diagnostic pass

Only after the A/B performance session, enable **Performance Diagnostics** for a separate diagnostic run. Play a normal level long enough for the sampling window to finish (about 10 seconds at 60 FPS), then export/share the complete Geode log. Turn diagnostics OFF again afterward. Never mix diagnostic-enabled runs into the performance A/B results.

## Interpretation

- A visual/manual run is useful for identifying repeatable stutters and regressions, but it is **not an exact FPS measurement**.
- Exact engine frame-time/FPS comparison requires instrumentation that can read the running phone's frame data (for example, a host connected through ADB). Do not infer FPS from the 60 Hz display, screen recording playback, or SurfaceFlinger presentation alone.
- Treat an improvement as credible only when it repeats across pairs without new visual/gameplay anomalies and without a consistent increase in stutters.
- A cloud benchmark, a single good run, or a subjective impression is not sufficient to remove the `alpha` label.
