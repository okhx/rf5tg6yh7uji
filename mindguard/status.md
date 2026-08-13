# Статус MindGuard

## Общий статус

**M0 завершён; M1 существенно реализован; M2 core существенно реализован;
обязательный C++ LLVM transformation tier реализован на local LLVM 18 и
cross-validated в приложении на LLVM 19.1.7
(2026-08-13):** есть проверяемые Dev frontends, зафиксированный Static Blob v1,
`mindguard-build` и независимые Rust/C++ decoder core. C++ narrow string/bytes и
bool/integer/basic-character/decimal-IEEE scalar generated Hardened subset реализован;
полный Rust/C++ API, Paranoid и release
readiness **не завершены**.

**Текущий shipping C++ protection path — одна mandatory Hardened static library;
protected fallback отсутствует. Проверены native Linux x86_64 и Windows x86_64
PE32+ DLL cross-build/artifact path, C++20, Clang/LLVM 18/19. Native Windows runtime
execution ещё не проверен.**

## Выполнено — C++ generated Hardened subset (частично)

- Добавлен `mindguard_protect_cpp_target(...)` для CMake: target source обязан быть
  явно зарегистрирован, находиться под `CMAKE_SOURCE_DIR`, а profile/tool paths,
  `PROJECT_ID`, `RELEASE_ID` и ephemeral `MG_BUILD_SEED_FILE` обязательны.
- `mindguard-scan prepare-cpp` извлекает narrow direct `MG_WITH_STRING`/
  `MG_WITH_BYTES` и scoped `MG_WITH_VALUE` для `bool`, integer, basic narrow
  character и decimal IEEE `float`/`double`, декодирует либо
  строго канонизирует literal, вычисляет site identity и через
  `mindguard-build` атомарно публикует private `0700` directory с `0600` generated
  header/audit. Plaintext staging удаляется; generated output содержит только
  объединённый encoded material и hashes.
- Blob и packed share объединяются в единый site-dependent permuted/rolling-masked
  поток, поэтому protected sites не имеют открытого `MGSTV1` marker, builder-вида
  share или видимой границы между ними; material восстанавливается в одном scoped
  buffer и wipe-ится. Сам
  byte-level Blob v1 и Rust/C++ core parity не изменены.
- Четыре варианта reconstruction выбираются старшими битами BLAKE3 `site_id`:
  отличаются направление обхода и rolling state transition. Release E2E требует
  несколько фактически разных вариантов среди шести sites, чтобы diversification
  не оставалась декларативной.
- Combined-material reconstruction перенесён из LTO-visible frontend header внутрь
  `-fno-lto`, `noinline` static core. Release E2E проверяет отдельный call boundary
  до strip и отсутствие имени decoder после `--strip-all`.
- Четыре variants имеют отдельные compile-time-specialized core entrypoints; E2E
  подтверждает минимум два разных call targets в реальном sample до удаления имён.
- Generated entrypoints стали success-only: detailed decode errors не выходят в
  frontend, любое нарушение вызывает abort внутри opaque core до callback.
- Linux Hardened target/core получают hidden visibility, function/data sections +
  linker GC, PIE, full RELRO/NOW, non-exec stack и отключённые build-id/compiler
  ident. E2E проверяет свойства фактического ELF, а не только наличие flags.
- Добавлен обязательный LLVM 18/19 new-pass-manager plugin `static-sdk/llvm-pass`: он после
  optimizer внедряет volatile keyed CFG dispatcher и несколько safe `abort` paths
  в используемые opaque materialization entrypoints. Hardened E2E реально собирает
  и загружает plugin через `-fpass-plugin`, затем проверяет несколько независимых
  read-only guards и минимум шесть guard references в ELF до strip. До шести
  non-EH basic blocks каждого entrypoint защищаются разными state salts.
- Hardened static library получает отдельный public 256-bit per-build seed формы
  кода, сгенерированный CMake и записанный в private
  `mindguard/obfuscation-manifest.txt`. Seed влияет на CFG guard values, число
  защищённых blocks и decoy branches; явный seed воспроизводит static core object.
- Mutation read-only CFG guard в копии ELF проверена отдельно: execution abort до
  callback/stdout, то есть guards участвуют в success path, а не являются dead noise.
- CMake source/seed dependency выполняет безопасную регенерацию exact generated
  directory; unregistered source, missing/31-byte seed, unsupported compiler,
  `long double`/hex-float/non-basic character/enum/wide literal, два sites на одной
  expansion line и callback с non-void
  result завершаются fail-closed.
- Hardened compile фиксирует input/execution encoding UTF-8 (`-finput-charset`/
  `-fexec-charset`, `/utf-8`), чтобы generator bytes совпадали с compiler semantics.
- Hardened callback вызывается только после blob/site/tag checks, обязан вернуть
  `void` и использует scoped plaintext с wipe. Integer/bool реконструируются typed
  только после проверки; decode/parse failure вызывает `abort` до callback.
  Wide/enum/RAII и ABI-зависимые scalar forms остаются fail-closed без Dev fallback.
- Generated site identity переведён с FNV на domain-separated BLAKE3 с
  length-prefixed target/source и зафиксированным golden vector; неоднозначная
  конкатенация target/source больше не создаёт структурную коллизию.
- Consumer проверен с CMake IPO/LTO `ON`; core object остаётся native ELF relocatable
  благодаря отдельной static library и `-fno-lto`, decoder остаётся `noinline`.
- Добавлен source-aware `mindguard-scan artifact-cpp`: final stripped artifact
  проверяется по всем зарегистрированным sites, для strings — exact/UTF-16/UTF-32
  representations; site count и фактическое наличие каждого encoded material
  сверяются с generated header. Audit содержит только counts/hashes.

## Проверки — C++ generated Hardened subset

- Per-build uniqueness/replay — PASS: две fresh build directories получили разные
  manifest seed и разные `core.cpp.o`; fresh rebuild с seed первого manifest дал
  byte-identical static core object. Некорректный seed отклоняется configure-time.
- После per-build seed полный `e2e_hardened_cpp.sh`, every-byte material mutation,
  CFG-guard tamper и negative fixtures — PASS. C++ `decode + allocation + wipe`
  4 KiB: **17.986 µs**, бюджет `<100 µs` соблюдён на local Linux x86_64.
- Добавлен обязательный `static-sdk/tests/e2e_rizin_cpp.sh`: он строит три fresh
  stripped ELF одного site set, проверяет уникальные manifest seed/artifact hash,
  запускает все попарные graph diff и полную directed cross-signature matrix.
  Для актуального Rizin 0.9.1 прежний `rz-diff -g` заменён его текущим эквивалентом
  `rz-diff -B -t graphs`; thresholds встроены в скрипт и завершают CI ошибкой.
- Rizin N-build gate — PASS: exact-basic-block graph similarity по 9 сравнениям
  `min=0.041667`, `median=0.157895`, `max=0.250000` при gate `<=0.50`;
  protected-entrypoint signature cross-hit по 6 направлениям стабильно `0.333333`
  при gate `<=0.70`. Это измеряет текущую полиморфность, но не доказывает стойкость
  к ручному семантическому сопоставлению.
- После Rizin automation полный C++ Hardened E2E/tamper/mutation — PASS; повторный
  C++ 4 KiB `decode + allocation + wipe`: **14.447 µs**, бюджет `<100 µs` соблюдён.
- LLVM tier расширен на decoder/callback/thunk path. Seed-dependent threaded
  dispatch использует `indirectbr` и 4/8 block-address targets вместо LLVM
  `switch`; `decode_share`, byte/string/scalar callback paths имеют отдельные
  runtime-keyed flattened state loops. Protect pass рекурсивно обрабатывает
  materializers, decoder, callback thunks и output-junk helper.
- Каждый protected entrypoint теперь проверяется через noinline guard thunk;
  guard thunk сам получает независимый read-only guard и threaded CFG. Tamper
  выбранного E2E guard прекращает выполнение до callback/output.
- Добавлены seed-depth MBA замены integer `add` (`xor/and/shift` либо `or/and`,
  depth 1–2), вставляемые optimizer-last. Runtime opaque predicate сравнивает два
  volatile чтения адреса локального stack slot через отдельный protected identity
  thunk, поэтому значение недоступно build-time constant folding.
- Перед публикацией plaintext добавлен noinline output-dependent junk round:
  два одинаковых involutive прохода реально маскируют и восстанавливают каждый
  output byte; E2E требует две сохранившиеся call boundaries. Это повышает цену
  статического анализа, но динамически снятый plaintext по-прежнему не скрывает.
- После threaded/flatten/recursive/MBA/opaque/junk изменений C++ Dev CTest и полный
  `e2e_hardened_cpp.sh` с every-byte material mutation, CFG guard tamper,
  fail-closed fixtures, LLVM IR structural checks и ELF checks — PASS. C++ 4 KiB
  `decode + allocation + wipe`: **39.748 µs**, бюджет `<100 µs` соблюдён.
- Повторный Rizin N-build gate после усиления — PASS: все 9 protected graph pairs
  восстановлены, exact-basic-block similarity `min/median/max=0.000000`;
  cross-build protected signature hit ratio по 6 направлениям `0.333333`.
- Добавлен обязательный C++ runtime guard layer: first-use `PTRACE_TRACEME`/tracer
  probe, calibrated `RDTSCP` decode+callback windows, entry `INT3`/JMP checks,
  preload/Frida-Gum maps scan, RELRO relocation anchor и отдельный CPUID/MAC/syscall
  VM score. Container policy, запрещающая self-ptrace при `TracerPid=0`, честно
  считается недоступным ptrace signal; остальные mandatory checks не отключаются.
- Добавлен post-link `mindguard_seal_elf` и `.mindguard.seal`: loaded `.text`
  хешируется лениво непосредственно перед каждым callback. Mutation последнего байта
  `.text` либо seal завершает процесс до callback; entry `INT3` проверен отдельно
  после повторного sealing изменённой `.text`.
- Full RELRO/NOW дополнен relocation anchor в RELRO, который при callback обязан
  указывать на ожидаемый hidden runtime entry внутри executable mapping. Для static
  executable это проверяемый эквивалент защиты собственного relocation target;
  публичного экспортируемого SDK PLT в текущем static-only контракте нет.
- `LD_PRELOAD` signal, 150 ms callback pause, CFG guard, site watermark, каждый байт
  combined material, `.text` и seal tamper fixtures — PASS; во всех случаях нет
  callback/stdout. Maps scan rate-limited до 1 ms активного use, environment и
  `.text` проверяются у callback.
- Per-site 64-bit watermark детерминирован из `site_id` и reconstruction variant,
  реально читается и валидируется protected thunk path; mutation watermark — PASS
  fail-closed. Маркер помогает трассировке lineage, но может быть удалён attacker,
  полностью пересобирающим/патчащим собственную копию.
- Full protected C++ callback benchmark с 4096-byte generated site, mandatory pass,
  runtime checks, decoder, output-dependent junk, callback и wipe: **81.812 µs**
  в финальном полном E2E, budget `<100 µs` соблюдён на local Linux x86_64.
  `nm -S` gate также подтвердил размер каждого materializer не более **4 KiB**.
- Финальный Rizin 0.9.1 gate — PASS: 3 fresh builds, 12 attempted/9 recovered
  protected graph pairs, exact basic-block similarity `min/median/max=0.000000`;
  directed signature cross-hit `0.250000`; stripped `aaa+afl` нашёл 105 функций,
  combined material не стал function boundary и имел один data xref.
- Bounded `aae` на stripped ELF не извлёк plaintext (`plaintext_hits=0`) и сообщил
  6 unsupported relocation/memory diagnostic events. Это результат конкретного
  автоматического прогона, не доказательство невозможности ручной эмуляции.
- Финальная чистая проверка — PASS: C++ Dev CMake/CTest, standalone LLVM pass build,
  полный Hardened regression/tamper/every-byte mutation E2E и Rizin N-build gate.
- Честный предел: защита повышает цену static/user-space анализа и обнаруживает
  перечисленные fixtures, но не предотвращает runtime memory dump в окне callback,
  hardware/kernel debugger, physical access, переименованный reflective injector,
  подменённый loader/kernel или полную attacker-controlled перепаковку binary.

- `static-sdk/tests/e2e_hardened_cpp.sh` — PASS: шесть автоматически generated sites,
  rebuild после seed timestamp change, valid execution, `strip --strip-all`, absence
  plaintext/checkout path/debug sections, source-aware final audit и mutation
  начала и конца встроенного combined material с abort без callback/output.
- Final scanner отдельно отклонил deliberate appended plaintext и изменённый/missing
  masked site material, не опубликовав audit для failed artifact.
- Каждый байт первого combined material по очереди изменён уже в stripped ELF:
  все варианты завершились внутри opaque core без callback/stdout.
- Negative fixtures — PASS: missing/31-byte seed, `long double`, wide literal, ambiguous
  same-line sites и borrowed `std::string_view` return отклонены без partial output.
- Полная regression matrix после combined-material/per-site reconstruction hardening:
  build/scanner unit, Rust core, Rust Dev release/doctests, C++ Dev CMake/CTest,
  Blob v1 E2E и C++ Hardened E2E — PASS. Последний 4 KiB informational run:
  Rust 24.142 µs, C++ 25.898 µs на local Linux x86_64; это ниже target 100 µs,
  но остаётся informational до pinned runner.
- Полная regression matrix после opaque success-only boundary, ELF hardening и
  multi-point LLVM pass: build/scanner unit, Rust core, Rust Dev release/doctests,
  C++ Dev CMake/CTest, Blob v1 E2E и plugin-enabled C++ Hardened E2E — PASS.
  Последний independent Blob 4 KiB run: Rust 17.192 µs, C++ 14.541 µs.

## Выполнено — C++ Windows x86_64 PE32+ DLL (cross-validated)

- CMake `Hardened` принимает Windows только как `SHARED_LIBRARY`/`MODULE_LIBRARY`,
  сохраняет одну static core library, обязательный Clang/LLVM 18/19 pass и запрещает
  EXE fallback. Cross-build требует явный host `MINDGUARD_SEAL_TOOL`; native Windows
  собирает `mindguard_seal_pe` как host tool.
- Windows runtime guards реализуют debugger probes, calibrated `RDTSCP`, critical
  prologue/import checks, Toolhelp Frida/Gum module scan, read-only relocation anchor,
  loaded `.text` hash и отдельный CPUID/MAC/timing VM score.
- PE sealer принимает только AMD64 PE32+ DLL с ASLR/NX/high-entropy VA, read-only
  `.mgseal` и без base relocations в `.text`; уже Authenticode-signed DLL не
  изменяется. `--verify` сверяет `.text` и seal после strip/package steps.
- `e2e_hardened_cpp_pe.sh` — PASS: реальная PE32+ DLL собрана Clang 18 с mandatory
  pass, export/COFF flags/indirect dispatch проверены, post-link seal и scanner
  прошли; `.text`, seal, repeated seal, signed-directory и truncated-input fixtures
  отклонены. Native Windows runtime execution/debugger/hook fixtures не запускались,
  поэтому Windows runtime readiness не заявляется.
- Интеграция Grape/Geode 5.7.1 — PASS на local Linux cross-build с
  Clang/LLVM 19.1.7: `build.sh` собрал matching host pass и PE sealer, сгенерировал
  material для 6 sites, слинковал Windows runtime, запечатал PE32+ DLL и создал
  `.geode`. `mindguard_seal_pe --verify` и `mindguard-scan artifact-cpp` прошли;
  6/6 encoded material присутствуют, 5 auditable plaintext sites отсутствуют,
  DLL внутри пакета byte-identical итоговой DLL. Native Windows execution не
  запускался.

## Выполнено — M2 blob/build/decoder core (частично)

- Зафиксирован отдельный byte-level contract `docs/static-blob-v1.md`: 96-byte
  header, version/profile/kind/site/bounds, split `blob_share`/`code_share`,
  diversified ARX stream и 128-bit ARX tamper tag. Bridge block не изменялся.
- Добавлен `mindguard-build` на Rust 1.97.1 с pinned BLAKE3 `=1.8.5`, domain
  separation, exact 32-byte seed-file read, private `0700` staging, `0600` outputs,
  deterministic generation и atomic publish без overwrite.
- Добавлен backward-compatible share format: `raw-v1` сохраняет прежние 32 bytes,
  рекомендуемый shipping `packed-v1` рассеивает conceptual code share по 256-byte
  site/diversifier-dependent permutation. Все 256 bytes участвуют в reconstruction;
  exact raw share не хранится contiguous.
- Seed, plaintext и derived arrays имеют scoped wipe; seed читается сразу в fixed
  array без промежуточной heap-копии. Audit содержит только contract/tool/profile,
  kind, site id и hashes project/release IDs.
- Добавлены независимые runtime core: `static-sdk/rust-core` без runtime crypto
  dependency и C++ static core без OpenSSL/system crypto. Оба проверяют все header
  fields/bounds/site/tag до plaintext и wipe material/plaintext.
- Material теперь обёрнут scoped RAII/Drop wipe в обоих core, включая exception/
  unwind path; также best-effort стираются keystream blocks, material word copies и
  computed tag. C++ очищает ранее выданный output до каждой новой decode-попытки,
  поэтому ошибка не оставляет доступным stale plaintext.
- C++ internal span boundary самостоятельно проверяет exact 32/256-byte share size;
  regression fixture подтверждает `Bounds` и пустой output вместо out-of-bounds.
- Добавлены scoped `with_decoded`/`with_decoded_packed`: callback вызывается только
  после полной проверки, а plaintext wipe выполняется сразу после callback, включая
  Rust panic/C++ exception unwind.
- C++ decoder помечен `noinline` и core компилируется с `-fno-lto` (`/GL-` для
  MSVC); C++ consuming IPO/LTO boundary проверен локально, Rust/Cargo gate pending.

## Проверки — M2 core

- `mindguard-build` unit ARX round-trip/tag-tamper test — PASS offline/locked.
- Rust core malformed-input test — PASS.
- Self-contained `static-sdk/tests/e2e_blob_v1.sh` — PASS: deterministic output,
  permissions, plaintext absence, отказ от overwrite, seed 31/33-byte и payload
  >64 KiB без публикации partial output, независимые Rust/C++ accept и parity rejection
  при mutation version/profile/kind/site/bounds/tag/payload/code share. Дополнительно
  каждый байт полного blob по очереди изменён: обе реализации отклонили все варианты
  с одинаковым классом ошибки; C++ stale-output regression check — PASS.
- Raw/packed compatibility — PASS в Rust и C++; mutation каждого из 256 packed
  share bytes — одинаковый `Tag` rejection до plaintext. Scanner подтвердил, что
  exact raw share отсутствует в packed table.
- Scalar kind=3 compatibility — PASS: Rust/C++ независимо восстановили canonical
  signed integer payload, а payload mutation одинаково отклонили как `Tag`.
- Собраны реальные embedded PIE artifacts Rust/C++, затем `strip --strip-all`:
  plaintext, raw share, absolute checkout path, `.debug_*`, `.symtab` и MindGuard
  symbols не обнаружены. Оба artifacts восстановили valid payload; заранее tampered
  embedded blob завершился ошибкой без callback/output. C++ artifact linked с
  non-executable stack.
- Информационный Release benchmark на текущем local Linux x86_64 для полного
  `decode + allocation + wipe` 4 KiB: Rust 16–19 µs, C++ 15–19 µs в финальных
  прогонах. Это ниже target
  100 µs, но не считается hard gate до объявления pinned benchmark runner.

## Выполнено — M4 scanner core (частично)

- Добавлен `mindguard-scan source` для C++/Rust direct callback macros: lexer
  пропускает comments/quoted/raw literals, извлекает sites и отвергает runtime либо
  indirect C++ forms. Audit содержит hashes/counts, но не path или literal.
- Literal grammar требует exact end token: выражения вида `"a" + value + "b"`,
  `"a".to_owned()`, составные character expressions и identifiers с цифрой больше
  не могут пройти эвристику как direct literal.
- Source scanner покрывает также все контрактные RAII/value macros (`MG_STRING`,
  `MG_BYTES`, `MG_VALUE`, `MG_ENUM`, `mg_str!`, `mg_bytes!`, `mg_value!`); их нельзя
  обойти мимо site audit.
- Добавлен `mindguard-scan artifact`: final artifact проверяется на exact bytes и
  UTF-16/UTF-32 LE/BE representations; обнаружение plaintext — build failure,
  короткие значения получают `non-auditable-short`, scalars —
  `runtime-reconstruction`.
- Scanner использует ту же pinned BLAKE3 build dependency только для audit hashes;
  runtime SDK по-прежнему не зависит от system/runtime crypto.

## Проверки — M4 scanner core

- Scanner unit grammar/encoding search test — PASS.
- Source scan на текущих C++/Rust Dev frontends — PASS (10/6 sites); existing
  indirect/runtime negative fixtures — expected failure.
- Artifact scan на decoder runner — `absent`; deliberate Dev literal leak — expected
  failure; 2-byte absent secret — `non-auditable-short`.
- Source и artifact scan теперь входят в проходящий Static Blob v1 E2E.
- Полная локальная матрица после scoped-wipe изменения: build/scanner unit tests,
  Rust core tests, Rust Dev release tests/doctests, Clang 18 CMake build/CTest и E2E —
  PASS.

## Последнее обновление

2026-08-13

## Выполнено — M1 bootstrap (частично)

- Добавлены независимые `static-sdk/cpp` и `static-sdk/rust`; общий runtime/C ABI не
  вводился, существующий Bridge не изменялся.
- Добавлены контрактные C++ macro names и Rust macro names для explicit Dev:
  callback forms, explicit move-only/non-`Copy` objects, exact-length string/bytes и
  embedded NUL semantics.
- Добавлены compile-time 64 KiB guards и scalar boundary: Rust принимает только
  sealed primitive scalar set, C++ отвергает pointer/aggregate и `long double`.
- C++ `consteval` token parser отвергает runtime expressions, pointers и indirect
  macros до генерации кода; string/raw-string, numeric/character и qualified enum
  forms имеют отдельные grammar guards.
- Site identity автоматически хеширует target/source/line/column/ordinal; C++
  использует per-TU `__COUNTER__`, Rust bootstrap пока использует span с ordinal 0.
  Raw source path не хранится в API object.
- C++ `source_location` gate отклоняет protected sites из headers; CMake добавляет
  GCC/Clang `-ffile-prefix-map` либо MSVC `/pathmap` для canonical relative path.
- Отсутствующий, неизвестный или конфликтующий profile завершается ошибкой;
  Hardened блокируется до M2 material/blob backend, Paranoid не понижается до Dev.

## Проверки — M1 bootstrap

- Rust 1.97.1: release `cargo test --manifest-path static-sdk/rust/Cargo.toml --features dev`
  — PASS, 1 unit test и 2 `compile_fail` doctests.
- Clang 18.1.3/CMake 3.28.3: configure/build/CTest с `MINDGUARD_PROFILE=Dev` — PASS;
  шесть `try_compile` negative fixtures отклонили runtime value, pointer,
  `long double`, indirect macro, payload >64 KiB и header site.
- `strings` audit Release test binaries C++/Rust не обнаружил absolute checkout
  path или raw static-sdk source path после compile-time site hashing.
- Rust `.rlib` как промежуточный compiler metadata всё ещё содержит absolute source
  path; финальный Release test executable его не содержит. Поэтому `.rlib` нельзя
  считать shipping artifact или сохранять в release CI cache до отдельного
  Cargo/rustc remap integration gate.
- Rust missing profile, mixed `dev,hardened` и Hardened — ожидаемый build failure;
  CMake missing profile, Hardened и Paranoid — ожидаемый configure failure.
- Поиск в `static-sdk` не обнаружил private key, `build_seed` или signing seed.

## Завершено — M0 documentation/reframe

- Переписан `plan.md`: primary Static SDK, optional Bridge, профили Dev/Hardened/
  Paranoid, target design, seed/key separation, build/scanner порядок и roadmap.
- Добавлен нормативный v1-alpha контракт `docs/static-protection-v1.md` с public API,
  literal boundary, 64 KiB limit, blob/decoder semantics, failure rules, scanner и
  non-goals.
- Добавлена `docs/static-threat-model.md` с build/cache/debug-symbol рисками,
  локальным reverse/debug attacker, residual runtime exposure и связью с Bridge.
- Добавлена `docs/static-sdk-roadmap.md` с упорядоченными implementation milestones.
- Старые `docs/protocol-v1.md` и `docs/threat-model.md` сохранены как документы
  Binary Integrity Bridge; их fixed metadata rules не менялись.

## Проверки M0

Выполнены только non-destructive Markdown checks:

- `find`/`grep` по ожидаемым файлам, заголовкам, public API names, profile names,
  seed/key separation, Bridge ordering и запретам.
- Изменения исходников Rust/C++/CMake/Cargo/tests/.gitignore не выполнялись.

## Pending — Static SDK

- Завершение M1: Rust procedural span ordinal, согласованные encoding/oversized
  fixtures и явная регистрация target sources в CMake/Cargo flow.
- Завершение M2: frontend/build-tool wiring, generated blob/share embedding,
  Rust opaque-core LTO gate и Rust generated frontend. C++ narrow scoped subset уже
  official; wide/enum/RAII, ABI-зависимые scalar forms и multi-source native
  platform coverage pending.
- CMake/Cargo integrations, opaque-core no-cross-module-LTO/inlining gate и
  explicit Dev/Hardened/Paranoid configuration.
- Полная public API/Hardened macro wiring, `c_str()` contract, generated report
  aggregation и strip/debug-symbol enforcement.
- Negative/tamper/golden cross-language corpus, performance gates и native CI.
- Optional Bridge post-link integration; текущий Bridge фактически остаётся только
  Linux ELF64LE x86_64 slice.

## Непроверенные платформы и исторические ограничения

- Проверены Dev и C++ narrow generated Hardened на local Linux x86_64/Clang 18.
  Rust public Hardened остаётся заблокирован: текущая Cargo integration не может
  доказуемо enforce dependency-core без cross-crate LTO. Cross-platform readiness
  заявлять нельзя.
- Rust 1.97.1 proof отдельно собрал core `.rlib` с `lto=off`,
  `embed-bitcode=no`, затем попытался связать consumer с fat LTO: rustc ожидаемо
  отказал `failed to get bitcode ... Can't find section .llvmbc`. Cargo package
  overrides официально не разрешают `lto`; безопасный следующий вариант требует
  отдельного Cargo/rustc wrapper либо нового private ABI и архитектурного решения.
- Design targets Linux/Windows/macOS x86_64 и arm64; текущий local baseline — Linux
  x86_64.
- Bridge PE32+, Mach-O, arm64, Windows/macOS CI, full golden corpus и release
  baseline остаются незавершёнными.
- Исторические локальные Linux ELF integrity tests были зафиксированы как PASS в
  предыдущем milestone. Повторный C++ Bridge configure не начался: в текущем
  окружении CMake не нашёл OpenSSL development files.
- Корневой Cargo workspace сейчас не загружается: отсутствуют каталоги
  `crates/mindguard-protector` и `crates/mindguard-runtime-rust`, на которые
  ссылается `Cargo.toml`. Новый Rust Static SDK проверен как отдельный workspace;
  исторический Rust Bridge/protector не восстановлен и не проверен.
- Static SDK не даёт абсолютной/невзламываемой secrecy; plaintext может быть снят в
  момент использования, а privileged attacker находится за границей модели.

## Следующие шаги

1. Rust generated Hardened frontend, stable site binding и доказуемый Cargo opaque
   core/no-cross-crate-LTO gate.
2. C++ wide/enum/RAII and ABI-dependent scalar coverage; Windows/macOS/arm64 native gates.
3. Performance gate, расширенный cross-language corpus и native target CI.
4. M6: explicit last-step Optional Bridge integration без изменения Protocol v1.
5. Расширение optional LLVM tier и capability-gated Paranoid — только после полного
   Rust/C++ core; текущий Clang 18 CFG tier не объявляется Paranoid.
