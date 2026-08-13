# Static Protection SDK — дорожная карта v1-alpha

Roadmap отделяет документированный contract от фактически выполненного кода.
Текущий Rust ELF Protector/runtime slice не считается выполнением Static SDK этапов.

## M0 — contract и architecture freeze

**Результат:** `plan.md`, `docs/static-protection-v1.md` и
`docs/static-threat-model.md` согласованы; старые protocol/threat docs явно
помечены Binary Integrity Bridge.

**Gate:** нет конфликтующих public names, profile semantics, seed lifecycle или
границ Bridge; baseline фиксируется отдельно для native runtime и cross artifact
gates без переноса assurance между платформами.

## M1 — independent core skeleton и Dev/Hardened frontend

**Результат:** независимые C++20 и Rust 1.97+ static implementations, CMake/Cargo
integration, explicit `Dev` и `Hardened` configuration, direct-literal parser,
64 KiB limit и automatic site identity.

**Gate:** unsupported runtime expressions, pointers/references/aggregates, C++ header
sites и oversized payloads дают build error; нет общего runtime C ABI; C++/Rust
negative fixtures согласованы.

## M2 — material, blob и decoder boundary

**Результат:** `mindguard-build`, ephemeral 32-byte seed-file lifecycle,
`project_id`/`release_id`, pinned/audited BLAKE3 derivation, versioned static blob,
domain-separated diversified ARX decoder, integrity tag и opaque static-core boundary.

**Gate:** seed не встречается в env/CLI/cache/source/artifact/log; temporary directory
`0700` очищается; одинаковые входы дают одинаковый material; tag/site/version/bounds
mutation не доходит до callback; no-cross-module-LTO/inlining policy fail-closed.

## M3 — API ergonomics и lifetime safety

**Результат:** C++ `MG_WITH_STRING`, `MG_WITH_BYTES`, `MG_WITH_VALUE`, `MG_WITH_ENUM`,
`MG_VALUE_AS`, RAII escape hatch; Rust `mg_with_str!`, `mg_with_bytes!`,
`mg_with_value!`, `mg_str!`, `mg_bytes!`, `mg_value!`; exact-length views, `c_str()`
NUL rule, wipe/drop semantics.

**Gate:** callback forms — primary; RAII objects — move-only; нет implicit owning
conversion/logging/serialization; scalar register exposure явно покрыта документацией.

## M4 — scanner, stripped artifact и performance

**Результат:** source scanner и post-link scanner, split/strip debug symbols, статусы
`non-auditable-short` и numeric reconstruction assurance, audit report без secrets,
site-size/latency measurements.

**Gate:** direct `MG_*`/`mg_*` calls извлекаются из target sources; финальный stripped
artifact проверяется после signing/package mutation; Hardened size <=4 KiB/site;
latency <=100 microseconds/4 KiB hard-gated только на pinned Linux x86_64 runner.

## M5 — compatibility и negative/tamper corpus

**Результат:** cross-language golden/negative corpus для blob, tag, bounds, identities,
encodings, profiles, malformed inputs и build failure modes; независимые Rust/C++
реализации принимают одинаковые valid cases и отвергают одинаковые mutations.

**Gate:** ни один fixture не содержит seed/private key/plaintext; parity test не
использует общий runtime core/C ABI; report содержит непроверенные targets честно.

## M6 — Optional Binary Integrity Bridge integration

**Результат:** explicit CMake/Cargo opt-in, последний pipeline step после compile/link/
strip/platform signing/package mutation, передача Protector только path exact raw
32-byte public-key file и fail на unsupported target.

**Gate:** существующий фиксированный 512-byte `.mindguard` Protocol v1 не изменён;
Bridge не включается молча; фактический coverage остаётся честно ограничен
Linux ELF64LE x86_64 до завершения PE/Mach-O/arm64 работы.

## M7 — platform Hardened expansion

**Результат:** обязательные safe-only runtime guards, platform sealer и Clang C++
transformation tier расширяются по одному target без protected fallback. `Paranoid`
остаётся зарезервированным именем, а не отдельным shipping profile.

**Gate:** missing capability вызывает configure/build failure; нет UB, stack
corruption, illegal instructions, unsafe anti-disassembler или runtime env bypass.

## Platform and release policy

- Target design: Linux/Windows/macOS x86_64 и arm64; native CI где доступно,
  compile/link/artifact audit иначе.
- Обязательные toolchains: GCC 14+, Clang 18+, MSVC 17.10+, Rust 1.97+.
- Нельзя объявлять Static SDK release-ready до прохождения всех applicable gates,
  публикации reproducible compiler/linker/strip configuration и документирования
  непроверенных платформ.
- Native Linux C++ Hardened runtime и Windows x86_64 PE32+ DLL cross artifact gate
  подтверждаются раздельно; Windows native execution, Mach-O и arm64 остаются pending.
