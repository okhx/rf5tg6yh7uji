## Target

GPL-preserving quality pass across first-party `src/`, starting with Android frame stepping.

## Dependents

- `FrameEngine::stepOnce` / `consumeStep`: desktop UI, mobile touch overlay, editor playback, keybinds, scheduler.
- `portableFrameUpdate`: Android `processCommands`, Apple/macOS `postUpdate`, replay frame count, checkpoints, trails, autoclicker, pathfinder.
- Persistence and replay modules are shared by desktop and mobile UI.
- Renderer/audio state crosses the game and encoder threads.

## Affected Stories

No release-plan or story files exist in this repository.

## Test Coverage

- Compile-time check: `consumePortableFrames` fractional accumulation.
- Build gate: private Win64 package through `pc/`.
- CI gates: Android32, Android64, macOS, and iOS.
- Gap: no automated game-runtime or mobile touch test harness.

## Risk: High

The requested broad rewrite touches shared timing, replay, persistence, rendering, and platform hooks without runtime tests.

## Recommended action

Use serial behavior-preserving milestones. Build after each milestone. Require physical Android/iOS validation for frame timing, and do not combine renderer/audio ownership changes with unrelated cleanup.
