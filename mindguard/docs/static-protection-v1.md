# Static Protection SDK v1-alpha — нормативный контракт

**Статус:** v1-alpha, contract-first. В этом документе слово **MUST** означает
обязательное требование будущей реализации; **MUST NOT** — запрет; **SHOULD** —
рекомендацию, отступление от которой требует явного audit note. Документ описывает
первый Static Protection SDK milestone и не объявляет, что этот код уже реализован.

Существующий ELF-протектор и integrity runtimes описаны отдельно как **Optional
Binary Integrity Bridge**. Их фиксированный metadata contract не расширяется этим
документом: см. [protocol-v1.md](protocol-v1.md) и его модель угроз
[threat-model.md](threat-model.md). Этот static blob не является секцией `.mindguard`
и не является новым полем в старом 512-byte блоке.

## 1. Назначение и границы

Static Protection SDK — local-only библиотека и build tooling для повышения стоимости
статического анализа, реверс-инжиниринга и отладки compile-time literals. Она
восстанавливает значение только на коротком scope callback либо в явно созданном
move-only объекте. Она не обещает абсолютную, идеальную или «невзламываемую»
секретность: когда приложение использует plaintext, его можно снять из памяти,
регистров, дампа или через debugger.

В v1-alpha нет web UI, dashboard, обязательного backend, сети, HSM, private signing
key в репозитории, kernel/driver компонента, VM-based virtualization или намеренного
undefined behavior. Static SDK не является лицензированием, entitlement-системой или
средством удалённой отзыва.

### 1.1 Публичные реализации и target design

- Поддерживаются два независимых public SDK: C++20 и Rust 1.97+. Они обязаны иметь
  общий security/API contract и negative/tamper semantics, но **не** общий Rust
  runtime и **не** общий runtime C ABI.
- Target design: Linux, Windows и macOS, каждый на x86_64 и arm64. Это design target,
  а не заявление о готовности: до native CI/реализации или эквивалентного
  compile/link/artifact-аудита конкретный target считается непроверенным. Текущий
  baseline — native Linux x86_64 и Windows x86_64 PE32+ DLL cross-build/artifact
  gate; native Windows runtime execution ещё не подтверждён.
- Официальные build integrations — только CMake (C++) и Cargo (Rust).
- Distribution — hybrid static SDK: C++ headers/templates/macros и статически
  связанный core; Rust macro crate и статически связанный runtime crate.
- Client static SDK не использует system crypto dependency и не делает сетевых
  запросов.

## 2. Profiles

| Profile | Обязательная семантика | Включение и статус |
|---|---|---|
| `Dev` | Явно **unprotected**: обычное значение доступно обычным способом. Нет обещания скрытия, decoder hardening или release scanner. | Разрешён только явным compile-time выбором. Это не fallback после ошибки Hardened. |
| `Hardened` | Единый C++ protected profile: strings/bytes/scalars, diversified reconstruction, обязательные LLVM transformations и runtime guards, scoped plaintext, wipe, platform sealing и post-link scanner. | Linux executable и Windows PE32+ DLL, x86_64, C++20, Clang/LLVM 18; любая отсутствующая capability — build error. |
| `Paranoid` | Отдельный shipping profile не вводится. | Имя зарезервировано; configure завершается ошибкой. |

Для protected C++ build нет упрощённого fallback: build/configure MUST fail при
отсутствии требуемого compiler pass, runtime guard, sealer или toolchain capability.
`Dev` остаётся только явно unprotected режимом и не выбирается автоматически.

## 3. Literal boundary

### 3.1 Общее правило

Защищаемый аргумент MUST быть **прямым compile-time literal**, видимым scanner и
парсером соответствующего frontend. Запрещены runtime values и expressions,
переменные, функции, вызовы, указатели, ссылки/borrowed references, агрегаты,
конкатенации и indirect macro forms. Нельзя передавать массив байтов через array
macro: bytes задаются только string/byte-literal syntax. Неподдержанный случай MUST
вызывать ошибку сборки, а не обычную незашифрованную вставку.

Один site имеет максимум **64 KiB plaintext**. Payload больше лимита MUST быть явно
отклонён до публикации артефакта. Лимит относится к точным байтам/кодовым единицам,
которые должны быть восстановлены, а не к размеру ciphertext/blob.

Numeric grammar — прямой numeric literal плюс, опционально, один unary sign `+` или
`-`. Вычисления (`1 + 2`, casts, ternary, `sizeof`, `const` variable и т. п.) не
являются частью v1-alpha boundary.

### 3.2 C++

Поддерживаются:

- string/byte literal forms в native encodings `char`, `char8_t`, `wchar_t`,
  `char16_t`, `char32_t`, включая raw strings;
- scalar `bool`, character и signed/unsigned integer literals;
- C++ enum через отдельный `MG_WITH_ENUM` (только непосредственный qualified
  enumerator/type contract, не переменная и не вычисляемое выражение);
- `float`/`double` только как IEEE-754 32/64 representation (`f32`/`f64` semantics).

`long double`, non-IEEE floating representation, pointer/reference/aggregate и
неоднозначные implementation-defined формы MUST быть отвергнуты. Сuffix, задающий
поддержанный scalar type, является частью direct literal; implicit narrowing или
user-defined conversion не разрешены.

C++ source protection v1 ограничена target `.cpp`, `.cc`, `.cxx` sources, явно
перечисленными в CMake. Вызовы в public headers не защищаются в `Hardened`/`Paranoid`
v1; scanner должен сообщить ошибку либо site вне contract. Это ограничение не
запрещает приложению вручную включать header API, но такой вызов не считается
protected site.

### 3.3 Rust

Поддерживаются UTF-8 `str` literals, byte strings и scalar `bool`, `char`, signed /
unsigned integer, `f32`/`f64` literals при IEEE-754 32/64 representation. Macro
frontend MUST отклонять runtime expressions, references, aggregates, casts и формы,
которые нельзя однозначно свести к direct literal. Rust macro span и per-site
ordinal образуют site identity; source path не записывается в shipping artifact.

## 4. Public API contract

Scoped callback forms — primary API: plaintext lifetime заканчивается при выходе из
callback. RAII forms — явный move-only escape hatch, а не implicit conversion.
Ниже приведены contract-level examples; конкретные callback/view type names могут быть
уточнены реализацией только без изменения семантики и публичных macro names.

### 4.1 C++ names

Обязательные имена:

- `MG_WITH_STRING(literal, callback)` — protected string literal;
- `MG_WITH_BYTES(literal, callback)` — protected byte/string literal;
- `MG_WITH_VALUE(literal, callback)` — protected scalar value;
- `MG_WITH_ENUM(enumerator, callback)` — protected enum value;
- `MG_VALUE_AS(type, value)` — единственный typed scalar access operation;
- `MG_STRING`, `MG_BYTES`, `MG_VALUE`, `MG_ENUM` — explicit move-only RAII objects.

Иллюстрация scoped semantics (не требование конкретного lambda spelling):

```cpp
MG_WITH_STRING("license text", [](mg::string_view value) {
    consume(value.data(), value.size());
});

MG_WITH_BYTES(R"(raw\nbytes)", [](mg::bytes_view value) {
    consume_bytes(value.data(), value.size());
});

MG_WITH_VALUE(-42, [](auto encoded_value) {
    auto value = MG_VALUE_AS(std::int32_t, encoded_value);
    consume_number(value);
});

MG_WITH_ENUM(MyMode::Safe, [](MyMode value) { consume_mode(value); });
```

RAII объекты MUST быть move-only, не копироваться и уничтожать/замораживать
plaintext после выхода из своего explicit scope. Они не должны неявно превращаться в
`std::string`, `std::vector`, C string, поток вывода или сериализуемый объект.

### 4.2 Rust names

Обязательные macro names:

- `mg_with_str!` — protected UTF-8 `str` literal;
- `mg_with_bytes!` — protected byte string;
- `mg_with_value!` — protected scalar literal;
- `mg_str!`, `mg_bytes!`, `mg_value!` — explicit value/move escape hatch.

Пример scoped semantics:

```rust
mg_with_str!("license text", |value| {
    consume(value.as_bytes());
});

mg_with_bytes!(b"raw\\0bytes", |value| {
    consume_bytes(value); // exact length is retained
});

mg_with_value!(-42i32, |value| {
    consume_number(value);
});
```

`mg_str!`, `mg_bytes!` и `mg_value!` MUST возвращать явно владеющий или move-only
contract object, определённый crate API; implicit conversion в `String`, `Vec`,
logging или serialization не допускается. Callback forms остаются предпочтительным
путём для минимизации lifetime.

### 4.3 Views, NUL и scalar exposure

String/bytes view — пара `pointer + exact length`; нулевой терминатор не является
частью длины автоматически. `c_str()` разрешён только если exact plaintext не
содержит embedded NUL; при embedded NUL вызов должен быть недоступен или завершаться
ошибкой contract, а не молча обрезать значение.

Нет implicit conversion в owning string/vector, logging или serialization. Приложение
само отвечает за копии, форматирование и журналы, которые оно создаёт внутри
callback. Scalar direct values могут существовать в регистрах или spill-слотах; SDK
обещает runtime reconstruction, а не невозможность увидеть raw value во всех местах.

## 5. Site identity и отсутствие source path в артефакте

Для C++ автоматически вычисляется tuple:

```text
target + canonical relative source path + line + column + per-TU ordinal
```

Для Rust используется эквивалентный macro-span site identity и per-site ordinal.
Canonical relative path нужен для детерминированного связывания site, но raw path,
имя пользователя и абсолютный checkout path MUST NOT попадать в generated source,
blob, symbol name или shipping artifact. В audit report допускается hash/metadata
identity, но не plaintext path, если это раскрывает локальную структуру без явной
необходимости.

Generated C++ Hardened frontend вычисляет 64-bit `site_id` как первые 8 байт
(little-endian) BLAKE3 derive-key context `mindguard static site identity v1` над
length-prefixed UTF-8 `target`, length-prefixed canonical relative source path и
little-endian `line`, `column`, `ordinal`. Length prefix — `u64` little-endian.
Golden vector: `target`, `src/main.cpp`, `7`, `3`, `0` даёт
`site_id = 0x8520e2f55ec30036`.

Generated C++ frontend объединяет blob и packed share в один маскированный,
site-зависимо переставленный поток с rolling byte dependency. Граница материалов
в shipping artifact отсутствует; единый scoped buffer восстанавливается и стирается
после callback. Это ломает линейное извлечение и служит anti-beacon слоем, а не
cryptographic secrecy.
Направление обхода и rolling transition выбираются из четырёх вариантов двумя
старшими битами `site_id`, поэтому разные sites не обязаны иметь одинаковый reconstruction
control/data flow.
Reconstruction combined material выполняется внутри того же отдельного static core,
который собирается с `-fno-lto` и имеет `noinline` decoder boundary. Release gate
проверяет реальный call через эту границу до strip и отсутствие её имени после strip.
Каждый из четырёх вариантов имеет отдельный `noinline` core entrypoint с фиксированным
на этапе компиляции направлением/state transition; release sample обязан вызывать
как минимум два разных entrypoints.
Generated entrypoints не возвращают подробный error oracle: любая ошибка embedding,
header/site/tag/bounds завершает процесс внутри opaque core; возврат означает только
успешную materialization перед callback. Независимые raw/packed compatibility API
сохраняют typed errors для тестового corpus.
На проверенном Linux Hardened CMake flow дополнительно включает hidden visibility,
function/data sections с linker GC, PIE, full RELRO/NOW, non-executable stack и
отключает build-id/compiler ident. Windows PE32+ DLL CMake flow включает function/
data sections с linker GC, ASLR (`DYNAMIC_BASE` + `HIGH_ENTROPY_VA`), NX и нулевой
COFF timestamp; PE artifact gate проверяет эти flags, export и sections. Native
Windows runtime execution этим cross gate не доказан.

Обязательный Clang 18 transformation tier загружается явным
`MINDGUARD_LLVM_PASS_PLUGIN` path. New-pass-manager plugin после optimizer применяет
seed-driven MBA depth 1–2, runtime stack-address opaque predicates, threaded
`indirectbr` dispatch и независимые fail-closed guards к materializer/decoder/
callback/thunk path. Guard thunks защищаются вторым guard/dispatcher уровнем;
output-dependent reversible byte graph сохраняет data dependency с plaintext.
Отсутствующий plugin, не-Clang compiler или несовместимый target — configure/build
error; упрощённого protected режима нет.

CMake создаёт public 256-bit `MINDGUARD_OBFUSCATION_SEED` формы кода и private
`mindguard/obfuscation-manifest.txt`. Fresh build directory получает новый seed;
явно повторённый seed воспроизводит static core object. Этот seed не является
секретом и не заменяет derivation seed.

Каждый C++ protected site содержит проверяемый 64-bit watermark, детерминированный
из `site_id` и reconstruction variant. Watermark предназначен для трассировки
утечки site/build lineage и tamper detection; он не предотвращает копирование
артефакта и не является доказательством владельца.

Повторяющаяся или неоднозначная identity, невозможность вычислить canonical
relative path или collision после domain separation MUST быть build error.

## 6. Build material lifecycle

### 6.1 `mindguard-build`

`mindguard-build` — Rust build tool, а не runtime library. Он принимает явные
`project_id` и `release_id` и использует exact raw 32-byte `build_seed` только из
**ephemeral file path** (эквивалент `MG_BUILD_SEED_FILE`). Seed value MUST NOT
появляться в environment, command-line argument, CMake cache, Rust flags/config,
source, generated source, logs, fixtures или artifacts.

Tool MUST:

1. создать private temporary build directory с правами `0700`;
2. открыть seed path без печати содержимого и отклонить размер, отличный от 32 байт;
3. доменно разделённо вывести material из contract version, profile, `project_id`,
   `release_id`, site identity и literal kind;
4. не сохранять raw seed в generated source/blob/shipping artifact;
5. удалить ephemeral directory, plaintext и промежуточный derived material после
   завершения настолько, насколько позволяет ОС/toolchain;
6. создать audit report только из hashes, non-secret metadata и tool versions.

Одинаковые входы (`build_seed`, IDs, contract/profile/site inputs) MUST давать
одинаковый MindGuard material. Это не обещает byte-for-byte одинаковый весь binary:
для этого дополнительно фиксируются reproducible compiler/linker/strip flags,
SDK/tool versions, platform signing и packaging.

`build_seed` — материал derivation static SDK. Он **не** является Ed25519 signing
seed, не должен переиспользоваться для Bridge и не передаётся Protector. Private
signing seed Protector остаётся отдельным внешним секретом release CI.

### 6.2 Derivation и зависимости

Build tool использует pinned и audited BLAKE3 dependency с явными domain separators.
Static client не должен тянуть system crypto dependency только ради derivation или
decoder. Client decoder применяет custom diversified ARX как обфускацию и
разнообразие путей, но это **не** криптографическая confidentiality claim и не
замена AEAD/подписи для угроз, где нужна криптографическая тайна.

## 7. Static blob и decoder semantic boundary

### 7.1 Отдельная версия и обязательные проверки

Protected blob/layout — новый, отдельно versioned static protection contract. Он
**никогда не является расширением** fixed 512-byte `.mindguard` metadata Bridge.
Точная byte layout и golden corpus фиксируются до реализации core; неизвестные версии
и неподдержанные варианты отвергаются.

Каждый blob MUST семантически содержать (в конкретной layout-версии):

- version и profile marker;
- site binding, достаточный для сопоставления вызова и blob;
- bounds/length для всех variable regions;
- derived payload/ciphertext, не содержащий raw `build_seed`;
- integrity tag, проверяемый до вызова callback.

Decoder обязан проверить version, profile, site binding, длины/переполнения и tag до
materialization plaintext. Любая модификация blob, tag или site binding MUST
завершиться fail-closed и **не должна вызвать callback**. Tag — обязательный
integrity check; отсутствие tag не может трактоваться как Dev.

### 7.2 Opaque core и plaintext lifetime

Decoder path и materialization helper проходят через opaque static-core boundary.
Приложение может включать LTO, но MindGuard core MUST собираться без cross-module
LTO/inlining. Если CMake, Cargo или compiler не могут enforce это свойство,
configure/build завершается ошибкой.

Plaintext создаётся непосредственно перед callback/explicit object use, живёт
минимально необходимое время и очищается после callback/drop посредством
реализационно проверяемого wipe. SDK не обещает, что compiler/OS исключит все копии,
регистры, swap или crash dump; эти ограничения раскрыты в
[static-threat-model.md](static-threat-model.md).

Запрещены intentional UB, stack corruption, illegal instructions, нестабильные
stack tricks и unsafe anti-disassembler tricks. Такие техники не являются частью
contract и не должны использоваться для вида «paranoic» защиты.

## 8. Build integration и scanner

### 8.1 Детерминированная последовательность

Официальный flow CMake/Cargo MUST быть эквивалентен следующему:

1. target и его source list регистрируются; C++ source list содержит только target
   `.cpp`/`.cc`/`.cxx`, Rust frontend получает macro spans;
2. scanner извлекает прямые `MG_*`/`mg_*` calls, проверяет literal boundary, тип,
   размер и site identity;
3. `mindguard-build` создаёт material/blob во временном private directory;
4. target compile/link выполняется с opaque-core policy;
5. C++ platform sealer записывает hash финальной `.text`; PE sealer выполняется до
   Authenticode и отказывает для уже подписанной DLL либо base relocation в `.text`;
6. выполняются strip/debug-symbol split и package mutation, не меняющие sealed
   `.text`;
7. Hardened post-link scanner сканирует **финальный stripped shipping
   artifact**;
8. только при явном Bridge opt-in в самом последнем шаге запускается Bridge
   Protector.

Текущий проверенный C++20 Hardened subset регистрируется через CMake:

```cmake
add_subdirectory(path/to/static-sdk/cpp mindguard-static)
add_executable(app src/main.cpp)
mindguard_protect_cpp_target(app
  PROJECT_ID my-product
  RELEASE_ID 2026.08
  SOURCES src/main.cpp)
```

На Windows target обязан быть `SHARED_LIBRARY`/`MODULE_LIBRARY` PE32+ DLL. При
cross-compilation CMake требует `MINDGUARD_SEAL_TOOL` с host-native PE sealer;
target executable под Windows отклоняется configure-time.

Configure выполняется только с `MINDGUARD_PROFILE=Hardened`, существующими paths
`MINDGUARD_BUILD_TOOL`/`MINDGUARD_SCAN_TOOL` и ephemeral path в
`MG_BUILD_SEED_FILE`. Содержимое seed не является CMake value. Для каждого
зарегистрированного `.cpp` создаются private generated header/audit; исходный literal
не используется macro expansion после parsing. Canonical blob хранится в generated
header в site-dependent masked embedding без открытого per-site `MGSTV1` marker и
восстанавливается только в scoped wiped buffer перед decoder. Это дополнительная
обфускация embedding, а не новая криптографическая гарантия. После внешних strip/sign/package
шагов final artifact обязательно проверяется `mindguard-scan artifact-cpp` вместе с
тем же target source и generated header; scanner сопоставляет site count, наличие
каждого masked blob/packed share и отсутствие plaintext. На 2026-08-13 этот
generated subset поддерживает только scoped
narrow `MG_WITH_STRING`/`MG_WITH_BYTES` и scoped `MG_WITH_VALUE` для `bool`, basic
execution-set narrow character, signed/unsigned integer и decimal IEEE `float`/
`double` literals. Wide character/string, hex-float, `long double`, enum/RAII и незарегистрированные
sources завершаются fail-closed и не считаются реализованными.

Нельзя подписать/защитить Bridge раньше strip, platform signing или package mutation:
любое изменение после Bridge invalidates его digest/signature.

### 8.2 Source scanner

Source scanner MUST находить direct `MG_*`/`mg_*` invocations именно в target sources
и отвергать unsupported indirect forms, runtime expressions, aggregate/array
macros, header sites (для C++ v1), слишком большие payloads и site collisions. Он
должен отличать явный `Dev` от пропущенной/непонятной конфигурации; неизвестная
конфигурация — ошибка, не silent unprotected build.

### 8.3 Post-link scanner

Post-link scanner просматривает весь stripped artifact и проверяет protected
strings/bytes в relevant encodings. Shipping debug symbols MUST быть split/stripped;
в shipping artifact не должно быть debug data, раскрывающих source path, macro site
или plaintext.

Literal длиной менее 8 code units/bytes остаётся защищённым, но получает статус
`non-auditable-short`. Такой статус означает, что scanner не может дать надёжную
глобальную гарантию отсутствия короткой последовательности; это не означает
`unprotected` и не является false absence claim.

Для numeric values scanner даёт только **runtime reconstruction assurance**: он
проверяет наличие ожидаемого protected site/decoder metadata, но не обещает
глобальный raw-bit scan по всему binary. Число может легально появиться в коде,
таблицах, ABI или регистрах.

Audit report MUST содержать результат каждого site, profile, contract/tool versions,
identity hashes и scanner findings; он MUST NOT содержать seed, plaintext, derived
material или private signing key.

## 9. Optional Binary Integrity Bridge

Bridge — explicit opt-in, последний этап после compile/link/strip/platform signing/
package mutation. Он получает от интеграции **только путь** к exact raw 32-byte
Ed25519 public-key file. Public-key bytes не передаются как environment value,
command-line value или CMake cache value; их путь не заменяет private signing seed.

Rust Protector Bridge использует отдельный private Ed25519 signing seed из внешнего
release-CI secret. Этот seed никогда не попадает в repository, static blob, client
SDK, logs или audit report и не переиспользует `build_seed`.

Bridge сейчас фактически проверен только на Linux ELF64LE x86_64. Если Bridge явно
запрошен на неподдержанном target/формате, configure/build MUST завершиться
ошибкой; silent disable или незаметный переход к Static SDK без Bridge запрещён.
Фиксированный 512-byte metadata block и все правила `protocol-v1.md` остаются без
изменений. Bridge повышает уверенность в целостности финального файла, но не скрывает
plaintext и не заменяет Static Protection SDK.

## 10. Failure semantics

### 10.1 Build/configure

Следующие случаи MUST fail before publishing a shipping artifact:

- отсутствующий, нечитаемый или не ровно 32-byte `build_seed` file;
- seed, переданный не через ephemeral path, или обнаруженный в env/flags/cache/log;
- неподдержанный literal, runtime expression, header site или aggregate/array form;
- payload более 64 KiB, неразрешимая encoding/type или non-IEEE float;
- collision/неоднозначность site identity;
- невозможность enforce opaque-core no-cross-module-LTO/inlining policy;
- повреждённый/неподдержанный blob, scanner mismatch или необъяснимый plaintext
  finding в Hardened;
- явно запрошенный Bridge не поддерживается target;
- ошибка strip/debug-symbol split, signing/package mutation или final artifact audit.

Ошибка MUST NOT оставлять частичный protected/shipping artifact и MUST NOT
автоматически переключаться в `Dev`.

### 10.2 Runtime

До callback decoder проверяет version, profile, site binding, bounds и integrity
 tag. При любой ошибке callback не вызывается, plaintext не выдаётся, а API
возвращает документированный fail-closed result либо завершает процесс согласно
конкретной реализации SDK. Rust и C++ должны иметь одинаковую семантику ошибки для
общего negative/tamper corpus, но не обязаны иметь общий ABI.

`Dev` — единственный профиль, где unprotected path разрешён, и он должен быть
выбран явно на compile time. Runtime env/token bypass для включения/отключения
защиты не допускается.

## 11. Performance и toolchain gates

- В `Hardened` budget на secret site: не более **4 KiB emitted site code**, без
  ciphertext.
- Latency target: не более **100 microseconds на 4 KiB** на pinned native Linux
  x86_64 benchmark runner. Размер site code проверяется везде, где есть CI;
  latency — hard gate только на этом runner.
- Toolchain commitment: GCC 14+, Clang 18+, MSVC 17.10+, Rust 1.97+.
- CI MUST быть native там, где target доступен. Для недоступных target обязательны
  честные compile/link/artifact audits; нельзя утверждать, что все platform tests
  пройдены.

## 12. C++ Hardened runtime/transform capabilities

Shipping C++ Hardened — одна static library без protected fallback. На native
Linux x86_64 обязательны:

- `PTRACE_TRACEME` probe и `/proc/self/status` tracer validation при первом use;
- calibrated `RDTSCP` timing вокруг decode и callback; текущий upper bound 100 ms
  обнаруживает длительную остановку, но не обещает поймать короткую;
- entry-byte `INT3`/JMP/prologue checks используемого materializer и decoder;
- lazy hash всей loaded `.text` непосредственно перед каждым callback, сверяемый с
  post-link `.mindguard.seal`;
- full RELRO/NOW и relocation anchor в RELRO, указывающий внутрь executable mapping;
- `LD_PRELOAD` и `/proc/self/maps` Frida/Gum scan; maps перечитывается не реже раза
  в 1 ms активного use, environment проверяется на каждом callback;
- отдельный VM/sandbox signal module: CPUID hypervisor bit, известные virtual MAC
  prefixes и syscall timing. VM signal усиливает integrity checks, но сам по себе
  не блокирует корректный запуск в виртуальной машине.

Windows x86_64 PE32+ DLL runtime реализует:

- `IsDebuggerPresent` и `CheckRemoteDebuggerPresent` у callback;
- `RDTSCP` timing, entry-byte checks и CPUID/MAC/timing VM score;
- Toolhelp scan загруженных modules по Frida/Gum signatures;
- проверку critical Kernel32 import targets и read-only relocation anchor;
- lazy hash loaded `.text` против `.mgseal`; PE sealer запрещает `.text` base
  relocations, проверяет ASLR/NX/high-entropy VA и не изменяет Authenticode DLL.

PE32+ cross E2E подтверждает compile/link/pass/seal/strip/scanner, export/flags и
отказ verification после `.text`/seal mutation. Это не заменяет native Windows
runtime/debugger/hook execution tests, которые пока pending.

Все подтверждённые нарушения завершают процесс до callback/output. Проверки повышают
цену user-space instrumentation, но не защищают от kernel debugger, подменённого
loader/kernel, physical-memory capture или полностью переупакованного
attacker-controlled binary.

## 13. Acceptance criteria первого implementation milestone

Этот документ сам по себе не является выполнением milestone. Перед объявлением
готовности требуются реализация и доказательства:

1. независимые C++ и Rust `core + Hardened` проходят общий API/security contract;
2. negative/tamper corpus покрывает unsupported forms, blob/tag mutation, seed
   lifecycle, site identity и cross-language parity;
3. CMake/Cargo создают audit report без seed/plaintext/derived material;
4. source и post-link scanners реально проверяют target source и final stripped
   artifact, включая `non-auditable-short` и numeric assurance;
5. opaque-core LTO/inlining gate, 64 KiB limit, scoped wipe и callback fail-closed
   реально enforced;
6. explicit Dev остаётся unprotected, `Paranoid` остаётся зарезервированным именем;
7. Bridge запускается только opt-in последним и сохраняет fixed 512-byte contract;
8. native checks и непроверенные target/риски задокументированы без extrapolation.

Очередность работ — в [static-sdk-roadmap.md](static-sdk-roadmap.md); специфическая
модель угроз — в [static-threat-model.md](static-threat-model.md).
