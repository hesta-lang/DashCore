# DashCore — Android rendering optimization

## 0.10.0-alpha — isolated GameObject geometry diagnostics

- Adds a separate AutoTest launch with Performance Diagnostics ON and static culling explicitly OFF.
- Reports the nearest eligible GameObject to the existing guard-band boundary, including object ID/type, raw rectangle, content size, anchor, position, transformed world bounds, viewport and parent transform chain.
- Adds an AutoTest diagnostics-mode override; ordinary FPS benchmark passes explicitly keep the expensive 600-frame diagnostics OFF so timing is not contaminated.
- Requires all three diagnostic hooks (CCNode::visit, PlayLayer::postUpdate and CCDirector::drawScene) before accepting a complete geometry scan.
- Keeps experimental culling OFF by default and preserves the 125% guard band. The diagnostic-only pass does not change rendering behavior.
- This is a diagnostic alpha, not a validated optimization. Cloud tests do not replace physical Huawei Y9s / Mali-G51 validation.
