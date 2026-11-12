# DashCore — Android rendering optimization

## 0.10.3-alpha — complete Android duration portability fix

- Replaces the remaining `std::chrono::microseconds` use in the GPU-cull input collection timer with the same explicit microsecond duration type used by the other Android timing sites.
- Updates all runtime diagnostic labels and the mod version to `0.10.3-alpha`.
- Preserves the read-only GameObject geometry scan scheduled from `PlayLayer::postUpdate` after 600 gameplay updates.
- Keeps `CCDirector::drawScene` optional and experimental static culling OFF by default.
- Diagnostic alpha only. No rendering optimization or performance improvement is claimed; physical Huawei Y9s / Mali-G51 validation is still required.
