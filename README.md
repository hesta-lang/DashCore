# DashCore — Android rendering optimization

## 0.10.4-alpha — PlayLayer object-array coverage diagnostics

- Adds a bounded census of `PlayLayer::m_objects` alongside the Cocos child-tree census, because the prior AutoTest found only two GameObjects in the child tree.
- Builds read-only GPU visibility probe inputs and an independent bounds comparison from the PlayLayer object array.
- Reports camera position and zoom as metadata without applying an unverified camera-space transform.
- Explicitly excludes `PlayerObject` instances from experimental culling and reports protected-player visits.
- Diagnostics remain read-only and experimental static culling remains OFF by default. No rendering or FPS improvement is claimed.
- Cloud AutoTest is not a physical Kirin 710F/Mali-G51 test. Physical Huawei Y9s visual and performance validation remains required.
