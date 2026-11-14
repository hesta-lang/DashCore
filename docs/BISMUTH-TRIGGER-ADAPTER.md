# Bismuth-style trigger-state adapter for Android64

## Purpose

This milestone adds a CPU-side shadow model of the group state used by Bismuth. It assigns a stable index to each unique combination of up to ten group IDs and keeps transforms and opacity per combination rather than writing a new transform to every object.

The adapter is optional and disabled by default. It validates the trigger-state model without replacing Geometry Dash gameplay or rendering.

## What is mirrored

- Move: observes the native move-command deltas after the original GJBaseGameLayer::processMoveActions has run.
- Rotate: observes native rotation commands before the original method consumes their transient flags, including center and extra movement values.
- Follow: observes the native follow-command delta calculated from the followed object's current and last position.
- Toggle: mirrors the group-enabled state after the native toggleGroup call.
- Alpha: samples GJEffectManager::opacityModForGroup after the vanilla PlayLayer::postUpdate callback and multiplies values for each group combination.
- Color: copies current color-action sprite channel values into a bounded 1101-entry CPU snapshot.
- Pulse: copies active HSV/Color pulse action metadata into a bounded CPU list and records per-combination pulse offsets.
- Checkpoints/reset: mirrors group state into checkpoint snapshots and restores/resets its shadow state without changing the game's checkpoint.

## Safety boundary

All original Geometry Dash trigger functions are called. The adapter does not change GameObject positions, rotations, opacity, visibility, sections, collision, object batching, z-order or Cocos nodes. It does not replace processMoveActions, processRotationActions, processFollowActions, toggleGroup, postUpdate, CCNode::visit or sprite draw calls.

The CPU group state is a shadow/reference representation only. The existing Geometry Dash renderer remains the only visible renderer. No GPU buffer is uploaded and no Compute Shader is dispatched in this milestone.

## GLES compatibility and next stage

The current launcher branch deliberately restores Geode's original GLES 2 context creation. GLES 2 cannot execute Bismuth's Compute Shader/SSBO path. The adapter therefore remains CPU-only and does not create another EGL context or request a different GLES version.

The model can serialize its state into a 64-byte, 16-byte-aligned GpuGroupStateRecord layout. This is only a data-layout contract for a future backend. A future compute path must first verify the actual current EGL/GLES context, require GLES 3.1+ plus necessary SSBO/compute support, and fall back cleanly without touching the vanilla renderer. A capable GPU does not mean the currently active GLES 2 context supports those features.

## Validation status

This is not yet a Bismuth renderer, does not dispatch Compute Shaders, and does not claim pixel parity or performance gains. First validate trigger counters and CPU state against vanilla gameplay and checkpoints. Only after this reference model is stable should a separate, opt-in GPU experiment be built. Keep diagnostic/validation builds on an -alpha prerelease version.
