# DashCore — Android rendering optimization

## 0.10.5-alpha — read-only render-pipeline instrumentation

- Adds a separate, OFF-by-default **Render Pipeline Diagnostics** setting.
- During a bounded 180-frame gameplay window, counts calls to `CCNode::visit`, `CCSprite::draw`, `CCSpriteBatchNode::draw`, `CCTextureAtlas::drawQuads` and `CCTextureAtlas::drawNumberOfQuads`.
- Measures callback durations only every 8th frame to limit diagnostic overhead; callback totals are inclusive and nested, so they must not be summed.
- Does not replace, skip, reorder or modify Geometry Dash rendering. It is CPU-side instrumentation, not GPU execution timing.
- Performance Diagnostics and experimental static culling remain separately controlled; culling remains OFF by default.
- Cloud build/test results do not represent physical Kirin 710F / Mali-G51 behavior. Physical Huawei Y9s validation remains required.
