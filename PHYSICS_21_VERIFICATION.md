# 2.1 Physics — verification checklist

How to check that the **2.1 Physics** toggle does what it claims, without needing
GD 2.1 installed and without judging by feel.

Pads, orbs and gravity portals are *instantaneous*: each one assigns exactly one
Y velocity. So each row below has one correct number. Read it off and compare.

## Setup

1. Enable the **Player Y Velocity** label (shows 3 decimals).
2. Enable frame stepping (`updater.advance_one` keybind) so you can pause and
   advance one physics tick at a time.
3. Assist tab → Physics → **2.1 Physics** (mobile: Assist page 2 → "2.1 physics").

Read the velocity on the tick *immediately after* contact.

> Every row lists the 2.2 value too. Toggle the setting off and re-read: if the
> number does not change, the toggle is not reaching that code path. That check
> is the whole point — it catches a "does nothing" regression immediately.

---

## 1. Gravity portals / blue orbs — THE change in this phase

This is currently the only interaction physics that differs, and it is the one
thing to test first.

Fall (or rise) to a known velocity `v`, hit a gravity portal, read the velocity
on the next tick.

| Toggle | Expected result |
| --- | --- |
| 2.1 Physics **ON** | `v × 1.75` |
| 2.1 Physics **OFF** | `v × 0.5` |

Example: enter at `-10.000` → 2.1 gives `-17.500`, 2.2 gives `-5.000`.

**Confidence: LOW — verify this one before trusting it.** The `1.75` comes from
a single line in `GD2.11-github2/.../PlayerObject/flipGravity.cpp:19`
(`m_yAccel *= 1.75`) and appears exactly once across *both* decompiled trees,
with no second decomp to corroborate it. If 1.75 sends you flying absurdly, the
constant is wrong (or means something else) — say so and it gets changed. It is
a one-line edit in `src/physics/gravity.cpp`.

---

## 2. Orbs — expected to be IDENTICAL in both modes

Checked line by line: the mod's orb code already matches the 2.1 decomp exactly.
**These should read the same with the toggle on and off.** If any row *does*
change, something is wrong.

Base jump power in the 2.1 decomp is `11.180032`. Multipliers:

| Orb | Cube | Ball | Robot | Spider | UFO | Ship |
| --- | --- | --- | --- | --- | --- | --- |
| Red | ×1.38 | ×1.34 | ×1.28 | ×1.34 | ×1.02 (mini ×1.36) | mini ×1.4 |
| Pink | ×0.72 | ×0.77 | ×0.72 | ×0.72 | ×0.42 | ×0.37 |
| Green | ×1.0 + flip | — | — | — | — | ×0.7 |
| Blue | ×0.8 + flip | — | — | — | — | — |
| Yellow | ×1.0 | ×1.0 | ×0.9 | ×1.0 | ×1.0 | ×1.0 |

Then, in order: `× (upside down ? -1 : 1)`, `× (mini ? 0.8 : 1.0)`, and finally
`× 0.7` if ball or spider.

Worked examples (normal gravity, normal size):

| Action | Expected Y velocity |
| --- | --- |
| Red orb, cube | 11.180032 × 1.38 = **15.428** |
| Red orb, ball | × 1.34 × 0.7 = **10.487** |
| Red orb, robot | × 1.28 = **14.310** |
| Down orb, ground modes | **−15.000** (spider ×1.10 → −16.500) |
| Down orb, flight | **−14.000** (UFO ×0.8 → −11.200) |

> One known discrepancy: the 2.1 decomp says pink-orb-on-ship is ×1.37 while the
> mod uses ×0.37. ×0.37 fits the pattern (pink is the weak orb everywhere else),
> so this is almost certainly a typo in the decomp and was left alone.

### Base jump power — MEASURED, and it did change

Measurements taken in-game (2.2 behaviour) vs what 2.1 should give:

| Test | Measured (2.2) | 2.1 expected |
| --- | --- | --- |
| Red orb, cube | 15.212 | 15.428 |
| Red orb, ball | 10.357 | 10.487 |
| Red orb, robot | 14.116 | 14.310 |
| Down orb, flight | −13.914 | −14.000 |

Dividing each by the per-orb multiplier this code already uses recovers the base:

- cube: 15.212 / 1.38 = **11.023**
- robot: 14.116 / 1.28 = **11.028**
- ball: 10.357 / (1.34 × 0.7) = **11.042**

Three gamemodes agree on ≈**11.03**, against 2.1's **11.180032** — 2.2's orbs are
about 1.3% weaker. Crucially the shortfall *scales with the orb multiplier*
(0.216 / 0.194 / 0.130) rather than being a constant offset, which is what a
smaller base looks like and rules out a fixed measurement error.

The down orb is the control: it uses a hardcoded −14 that is not multiplied by
the base, and it measured −13.914 — off by 0.086, which is roughly one physics
tick of flight gravity. So that path is already exact and was left alone.

**Fixed:** with 2.1 Physics on, the orb base is now 11.180032. Re-run the table
above; a red orb on a normal cube should now read **15.428** instead of 15.212.

---

## 3. Pads — expected to be IDENTICAL in both modes

The formula matches the 2.1 decomp exactly:

```
yVelocity = force × (mini ? 0.8 : 1.0) × (upsideDown ? -1 : 1) × 16.0
then × 0.6 if ball or spider
```

Blue pad uses `force = 0.8` in both 2.1 and the mod, then flips gravity.
So blue pad, normal cube → `0.8 × 1.0 × 1.0 × 16.0` = **12.800**.

**Caveat:** the per-pad force values for yellow/pink/red pads live in the
caller, and 2.1's version of that table is *not* present in the decompiled
sources — so it could not be compared statically. If yellow/pink/red pads feel
wrong in old levels, measure them and report the numbers; that is the only way
to recover those values.

---

## 4. What this phase does NOT change

Be aware so you're not testing for things that cannot work yet:

- **Continuous gravity** — ship curve, fall speed, terminal velocity, cube jump
  arc, slopes. That is Phase 2 (hooking `PlayerObject::updateJump`).
- **Hitboxes and collision/death.** The mod does not own collision — the game's
  own closed-binary `checkCollisions` decides it (`phys::checkPlayerCollisions`
  is an unused stub). If a level is impossible because of 2.2 *hitbox* changes,
  nothing here will fix it, and restoring it would be a much larger project.

---

## Reporting back

For anything that looks wrong, the useful report is:

```
gamemode + size (e.g. normal-size cube)
what was touched (e.g. red orb)
Y velocity with 2.1 ON:   <number>
Y velocity with 2.1 OFF:  <number>
```

Those two numbers are enough to pin down the constant.
