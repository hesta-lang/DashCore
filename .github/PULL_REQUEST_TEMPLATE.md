## Change type
- [ ] Diagnosis only
- [ ] Bug fix
- [ ] Performance optimization
- [ ] Build / CI / documentation maintenance

## Root cause and evidence
**Observed problem or goal:**

**Root cause (not just the symptom):**

**Relevant log, issue, or workflow run:**

## Implementation
**Files and code paths changed:**

**Why this is the smallest safe change:**

**Android64 API/binding verification:**

## Validation
- [ ] Cppcheck passed
- [ ] Python syntax checks passed
- [ ] Relevant unit/regression tests passed
- [ ] Android64 build passed for this exact commit
- [ ] .geode package validation passed
- [ ] Gameplay-only AutoTest passed with the required final diagnostics
- [ ] Physical Huawei Y9s test completed
- [ ] No new gameplay, physics, draw-order, input, or UI behavior change
- [ ] Static culling remains OFF by default
- [ ] Performance diagnostics remain OFF by default

**Build run URL and source SHA:**

**AutoTest run URL and source SHA:**

**Physical device/version/SHA and log:**

**Measured outcome (include frame pacing/hitches where available):**

## Release classification
- [ ] Diagnostic/validation only — keep prerelease \`alpha\`
- [ ] Optimization physically validated — attach the evidence supporting a non-alpha release

**Unverified assumptions or remaining limitations:**

> Cloud-emulator results do not prove performance improvements on the Huawei Y9s. CPU callback timings are not GPU timings, and nested inclusive timings must not be added together.
