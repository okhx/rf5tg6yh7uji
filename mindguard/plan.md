# MindGuard — план Static Protection SDK v1-alpha

**Дата архитектурного решения:** 2026-08-12. Этот документ является источником
истины для продукта и очередности работ. Статический SDK — основной продуктовый
трек. Уже реализованный Rust ELF Protector и независимые Rust/C++ integrity runtimes
сохраняются как отдельный явно включаемый **Optional Binary Integrity Bridge**
(далее — Bridge); Bridge не является скрытой обязательной частью Static SDK.

## 1. Решение о продукте и границы

MindGuard — локальный static protection SDK для повышения стоимости
реверс-инжиниринга и отладки приложения. Он не требует сети, web UI, dashboard,
backend, HSM или приватного signing key в репозитории. SDK не обещает абсолютную или
«невзламываемую» секретность: plaintext может быть снят в момент, когда приложение
его использует.

В поставку входят два независимых слоя:

1. **Static Protection SDK (primary).** Защищает поддержанные compile-time literals
   и значения на этапе сборки, восстанавливая их только на ограниченной области
   callback/объекта.
2. **Optional Binary Integrity Bridge (legacy v1).** После окончательной мутации
   бинарника проверяет целостность подписанного контейнера. Это отдельный протокол,
   отдельные ключи и отдельный Rust Protector; его правила фиксированного блока не
   меняются этим планом.

Ни один слой не добавляет обязательный сервер или удалённый канал доверия.

## 2. Текущая реализация (исторически подтверждённый baseline)

В репозитории уже есть рабочий срез **ELF64 little-endian x86_64 Linux**:

- Rust `mindguard-protector` преобразует ELF и создаёт фиксированный блок
  `.mindguard` по [Binary Integrity Protocol v1](docs/protocol-v1.md).
- Независимые Rust и C++ runtime SDK проверяют этот блок и завершают процесс при
  нарушении целостности; общего Rust runtime или общего C ABI между ними нет.
- Локальные Rust/C++ проверки и self-contained E2E для Linux ELF были пройдены
  ранее. Это не означает готовность Static SDK, выпуска v1 или поддержки других
  платформ.

PE32+, Mach-O, arm64 Bridge, all-platform CI, общий golden corpus и release baseline
остаются незавершёнными. Фактический Bridge slice не удаляется и не расширяется
заявлениями о cross-platform готовности.

Подробные старые документы теперь читаются как документы Bridge:
`docs/protocol-v1.md` — неизменяемый фиксированный metadata contract, а
`docs/threat-model.md` — его модель угроз. Новый static contract находится в
`docs/static-protection-v1.md`, его модель угроз — в `docs/static-threat-model.md`.

## 3. Публичный контракт и целевые платформы

**Архитектурное решение 2026-08-13 (C++ protection track):** shipping C++ SDK
поставляется одной static library. Для protected C++ build существует один профиль
`Hardened`: текущие implementation targets — Linux x86_64 executable и Windows
x86_64 PE32+ DLL с Clang/LLVM 18 или 19.
LLVM transformation tier и реализуемые runtime guards обязательны; отсутствие любой
capability завершает configure/build ошибкой. Упрощённого protected fallback нет.
`Dev` остаётся только явно unprotected режимом разработки и не является shipping
заменой `Hardened`. Расширение C++ track не меняет другие отдельные компоненты.

- Публичные SDK: **C++20** и **Rust 1.97+**. Они имеют общий security/API contract,
  но являются независимыми реализациями; общий runtime C ABI не вводится.
- Target design: Linux, Windows и macOS на **x86_64 и arm64**. До появления
  реализации, native CI либо эквивалентного compile/link/artifact-аудита нельзя
  объявлять фактическую cross-platform готовность. Текущий baseline — native Linux
  x86_64 и Windows x86_64 PE32+ DLL cross-build/artifact gate; native Windows
  runtime execution ещё не подтверждён.
- Официальные интеграции: только **CMake** для C++ и **Cargo** для Rust.
- Distribution — hybrid static SDK: C++ headers/templates/macros плюс статически
  связанное core; Rust macro crate плюс статически связанный runtime crate.
- Decoder/core не выполняет сетевых запросов и не принимает секреты через runtime
  API приложения.

## 4. Профили защиты

| Профиль | Семантика | Статус |
|---|---|---|
| **Dev** | Явно `unprotected`: литералы остаются обычными, runtime protection не включается. Это осознанная настройка сборки, не fallback. | Должен быть первым режимом для разработки и тестов интеграции. |
| **Hardened** | Единый обязательный C++ protected profile: literals/values, diversified reconstruction, LLVM transformations, runtime guards, scoped plaintext, wipe и final scanner. | Linux executable и Windows PE32+ DLL, x86_64 + Clang/LLVM 18/19; missing capability — build error. |
| **Paranoid** | Отдельный shipping profile не вводится. | Имя зарезервировано и configure завершается ошибкой. |

Anti-debug, anti-tamper и compiler transformation реализуются в C++ `Hardened`
только как обязательные capability-gated слои. Ни один отсутствующий слой не может
молча отключиться. Запрещённые ниже UB/illegal-instruction техники не разрешаются.

## 5. Literal boundary и API

Нормативные подробности находятся в [static-protection-v1.md](docs/static-protection-v1.md).
Краткий неизменяемый boundary:

- Принимаются только прямые compile-time literals. Runtime values/expressions,
  указатели, ссылки, агрегаты, косвенные макросы и array macros запрещены.
- C++: string/byte literals в нативных encoding `char`, `char8_t`, `wchar_t`,
  `char16_t`, `char32_t`, включая raw strings; scalar `bool`, character,
  signed/unsigned integers; C++ enum — отдельный API. `f32/f64` (C++ `float/double`)
  разрешены только при IEEE-754 32/64 representation. `long double` и non-IEEE
  отклоняются.
- Rust: UTF-8 `str` и byte strings; scalar bool/chars/signed/unsigned integers и
  `f32/f64` с тем же IEEE-ограничением.
- Numeric grammar — прямой numeric literal с optional unary `+` или `-`, без
  вычисляемого выражения. Bytes задаются только string/byte-literal syntax.
- Максимальный plaintext одного site — **64 KiB**; больший payload явно отвергается
  при сборке.

### Публичные имена

C++ scoped API (primary): `MG_WITH_STRING`, `MG_WITH_BYTES`, `MG_WITH_VALUE`,
`MG_WITH_ENUM`. Typed scalar access выполняется только через `MG_VALUE_AS`.
Move-only RAII escape hatch: `MG_STRING`, `MG_BYTES`, `MG_VALUE`, `MG_ENUM`.

Rust scoped macros (primary): `mg_with_str!`, `mg_with_bytes!`,
`mg_with_value!`; explicit value macros: `mg_str!`, `mg_bytes!`, `mg_value!`.
Других публичных имён для v1-alpha не добавляется без обновления контракта.

String views — это pointer плюс exact length. `c_str()` разрешён только для значения
без embedded NUL. Не существует implicit conversion в owning string/vector,
logging или serialization. Scalar direct values могут находиться в регистрах — это
явное ограничение, а не обещание скрыть каждый raw bit.

В C++ v1 защищаются только прямые вызовы в target `.cpp`, `.cc`, `.cxx` sources,
перечисленных в CMake; public-header source protection в Hardened/Paranoid v1 нет.
Build scanner обязан отклонить неподдержанные indirect/macro/runtime forms.

Site identity создаётся автоматически: target + canonical relative source path +
line/column + per-TU ordinal. В Rust аналогично используется macro span и site
ordinal. Raw source path не попадает в shipping artifact.

## 6. Build material и `mindguard-build`

`mindguard-build` — локальный Rust build tool. Конфиденциальный точный raw
32-byte `build_seed` поступает **только из ephemeral file path** (эквивалент
`MG_BUILD_SEED_FILE`). Значение seed запрещено передавать как env value, command-line
value, CMake cache entry, Rust flag/config, log, source или artifact. Build tool:

1. принимает явные `project_id` и `release_id`;
2. создаёт private build directory с правами `0700`;
3. читает ровно 32 байта, проверяет ошибки и связывает material с project/release,
   site identity, profile и contract version;
4. удаляет ephemeral directory и временные plaintext/material после завершения;
5. пишет audit report только с hashes/metadata/tool versions — никогда plaintext,
   seed или derived material.

Одинаковые protocol inputs дают одинаковый MindGuard blob material. C++ code shape
дополнительно получает публичный случайный per-build obfuscation seed, записанный в
manifest; повтор seed обеспечивает воспроизводимость debug build, новый seed обязан
менять структуру binary при неизменном `site_id`. Полная воспроизводимость всего
бинарника требует также фиксированных compiler/linker/strip настроек. `build_seed`
не является Ed25519 signing seed и никогда
не переиспользуется для подписи Bridge или других целей.

BLAKE3 dependency build tool должна быть pinned и audited. Client static SDK не
зависит от system crypto library. BLAKE3 применяется с domain separation для
derivation; client decoder использует custom diversified ARX только как obfuscation,
а не как криптографическую конфиденциальность.

## 7. Static blob и decoder boundary

Protected blob/layout — **новый отдельный versioned static protection contract**. Он
никогда не является расширением и не переиспользует фиксированный 512-byte
`.mindguard` metadata block Bridge. Semantic contract требует как минимум version/
profile marker, site binding, derived payload и integrity tag; точная byte layout и
golden corpus фиксируются до реализации core и затем не меняются молча.

Decoder и его materialization path проходят через opaque static-core boundary.
Приложение может включать LTO, но MindGuard core обязан собираться без cross-module
LTO/inlining; если toolchain не может это обеспечить, configure/build завершается
ошибкой. Перед callback decoder проверяет version, site binding, bounds и tag; любая
модификация blob/tag даёт fail-closed до callback. Plaintext создаётся только на
минимальном scope, callback завершается, затем память wipe-ится настолько, насколько
гарантирует выбранный язык/toolchain.

Никаких intentional UB, stack corruption, illegal instructions или unsafe
anti-disassembler tricks. Это повышение стоимости анализа, а не обещание скрыть
plaintext от процесса, debugger или privileged attacker.

## 8. Build, scanner и Bridge integration

Сборка Static SDK должна иметь явные шаги:

1. CMake/Cargo регистрирует target и исходники; scanner извлекает прямые
   `MG_*`/`mg_*` sites и проверяет literal boundary.
2. `mindguard-build` генерирует versioned blob/material через ephemeral seed.
3. target compile/link выполняется с opaque-core policy.
4. Выполняются strip/debug-symbol split и все platform signing/package mutation.
5. Для Hardened post-link scanner проверяет final stripped shipping artifact.
6. Только при явном opt-in запускается Bridge Protector **последним** — после
   compile, link, strip, platform signing и package mutation.

Scanner просматривает весь stripped artifact и ищет protected strings/bytes в
релевантных encoding. Literal короче 8 code units/bytes остаётся protected, но
помечается `non-auditable-short`; отсутствие такой короткой последовательности не
является гарантией отсутствия. Для numeric site scanner сообщает только runtime
reconstruction assurance и не обещает global raw-bit scan.

Bridge opt-in должен передать Protector **только путь** к exact raw 32-byte Ed25519
public-key file. Содержимое не передаётся в аргументе, environment или CMake cache.
Private signing seed Protector остаётся отдельным внешним секретом release CI и не
смешивается с `build_seed`. Если Bridge явно запрошен для неподдержанного target,
configure/build завершается ошибкой, а не silently disables bridge.

## 9. Ошибки и fail-closed

Обязательные ошибки сборки: отсутствующий/нечитаемый/не ровно 32-byte seed file,
неподдержанный literal или expression, payload >64 KiB, конфликт site identity,
невозможность enforce opaque-core policy, scanner mismatch, повреждённый или
неподдержанный blob, недоступная capability C++ Hardened, а также
явно запрошенный Bridge на неподдержанной платформе. Ошибка не должна публиковать
частичный shipping artifact и не превращается в Dev автоматически.

Runtime decoder при неизвестной версии, неверном tag, bounds/site binding или другой
ошибке не вызывает callback и завершает операцию fail-closed по API-правилам
конкретного SDK. Dev — единственный профиль, где unprotected поведение разрешено,
и оно включается явно на compile time.

## 10. Производительность и toolchain commitment

- Для Hardened на каждый secret site: не более **4 KiB emitted site code** без
  ciphertext.
- На pinned native Linux x86_64 benchmark runner: не более **100 microseconds на
  4 KiB** plaintext. Размер проверяется на всех доступных CI; latency hard-gated
  только на этом runner.
- Обязательный baseline toolchain: GCC 14+, Clang 18+, MSVC 17.10+, Rust 1.97+.
  CI запускается native там, где это возможно; иначе обязательны compile/link/
  artifact-аудиты. Нельзя заявлять, что все target tests уже пройдены.

## 11. C++ Hardened runtime/transform capabilities

C++ Hardened configuration отказывает без совместимого Clang/LLVM 18/19 pass, static runtime core
или platform sealer. Linux runtime включает ptrace/tracer probe, RELRO anchor и
preload/maps signals; Windows PE32+ DLL runtime — `IsDebuggerPresent`/
`CheckRemoteDebuggerPresent`, critical import/section checks и Toolhelp module scan.
Обе реализации включают RDTSCP timing, critical prologue checks, callback-time
`.text` hash и отдельный VM score. Обязательный pass применяет
per-build seed-driven MBA/opaque predicates, threaded dispatch, decoder/callback
flattening и recursive thunk guards. Подтверждённое нарушение немедленно завершает
процесс; runtime bypass/policy switch отсутствует. Ограничения user-space detection,
memory dump и privileged attacker фиксируются threat model, а не скрываются.

## 12. Критерии первого Static SDK implementation milestone

Milestone считается выполненным только после реализации и проверок, а не по одному
этому документу:

- независимые C++ и Rust core+Hardened implementations проходят общий contract и
  negative/tamper corpus;
- все перечисленные literal boundary, 64 KiB limit, source identity, scoped wipe,
  tag check и opaque-core build failures реально enforced;
- CMake и Cargo интеграции создают воспроизводимый audit report без seed/plaintext;
- post-link scanner проверяет stripped artifact и выдаёт короткие/числовые статусы;
- Dev explicit unprotected, Hardened fail-closed, Paranoid зарезервирован;
- Bridge остаётся optional, запускается последним и не нарушает fixed 512-byte
  metadata contract;
- выполнены native проверки доступных target и задокументированы все непроверенные
  платформы/ограничения.

Порядок и критерии по этапам собраны в [static-sdk-roadmap.md](docs/static-sdk-roadmap.md).
