# Модель угроз Static Protection SDK v1-alpha

**Статус:** нормативное описание границ и остаточных рисков первого Static SDK
milestone. Оно относится к `docs/static-protection-v1.md`, а не заменяет старую
модель целостности Bridge: [threat-model.md](threat-model.md) остаётся моделью
Optional Binary Integrity Bridge и фиксированного metadata Protocol v1.

## 1. Назначение и security claim

Static SDK — local-only build/runtime слой, повышающий стоимость поиска и анализа
compile-time literals. Его claim ограничен: в Hardened поддержанный
literal не должен оставаться простым статическим plaintext site при корректной
сборке, а decoder проверяет blob до callback и очищает временный plaintext после
использования настолько, насколько позволяют язык, compiler и ОС. Это не
криптографическая гарантия конфиденциальности и не обещание абсолютной,
«невзламываемой» или debugger-proof защиты.

Когда приложение использует значение, plaintext может наблюдаться через debugger,
registers, stack spill, heap копию, crash dump, swap, логирование самого приложения,
API-параметры или side-channel. Scalar direct values по природе могут существовать в
регистрах. Static SDK поднимает стоимость анализа; он не делает использование секрета
невозможным.

## 2. Scope и assets

### В scope

- исходные target `.cpp`/`.cc`/`.cxx` sites и Rust macro sites;
- literal plaintext до 64 KiB, encoded blob, integrity tag и derived material;
- `project_id`, `release_id`, site identity и profile choice;
- build tool temporary directory, audit report и final stripped shipping artifact;
- decoder callback boundary, plaintext lifetime/wipe и fail-closed result;
- явная интеграция последнего шага с Optional Binary Integrity Bridge.

### Активы

| Актив | Требуемое свойство | Что означает компрометация |
|---|---|---|
| Plaintext literal до build | Не появляться в shipping artifact как простой protected site | Статический поиск секрета становится дешевле |
| `build_seed` | Конфиденциальность и non-reuse | Возможность воспроизводить/анализировать material всех известных inputs |
| Derived material/blob/tag | Site binding, integrity, отсутствие raw seed | Подмена может вызвать неправильное значение или bypass callback |
| Source/site identity | Детерминированность без утечки raw path | Collision, cross-site confusion или раскрытие структуры checkout |
| Static core/decoder | Проверка до callback, bounded reconstruction, wipe | Подмена runtime semantics или обход fail-closed |
| Final stripped artifact | Отсутствие debug data и недопустимых plaintext residues | Снижение стоимости reverse engineering |
| Build/audit metadata | Трассируемость без secrets | Утечка seed/plaintext/derived material через отчёт |
| Bridge signing seed/public key path | Разделение от static build material | Выпуск изменённых Bridge artifacts или подмена trust config |

## 3. Trust boundaries и assumptions

1. **Developer source → frontend/scanner.** Исходники и macro invocations считаются
   потенциально ошибочными. Scanner/парсер не должен превращать неподдержанную форму
   в обычный unprotected literal.
2. **Build tool → temporary directory.** `mindguard-build` доверяет выбранным
   `project_id`/`release_id` и toolchain, но seed читается только из внешнего файла.
   Temporary directory создаётся с `0700`; обычный атакующий не должен читать его,
   но privileged/root attacker может.
3. **Build tool → generated blob.** Derivation и domain separation считаются
   корректными; BLAKE3 — pinned/audited dependency. Custom ARX в client — только
   обфускация, не cryptographic confidentiality.
4. **Compiler/linker/strip/signing → final artifact.** Конфигурация считается
   доверенной только после scanner/audit. Любая последующая мутация изменяет
   assumptions; Bridge должен выполняться последним.
5. **Static SDK core → application.** Core и decoder должны быть статически
   связаны через opaque boundary без cross-module LTO/inlining. Приложение может
   включать LTO, но это не должно растворить core boundary.
6. **Runtime process → OS.** Обычная ОС/loader предполагаются честными в local
   baseline. Root/administrator, kernel/hypervisor control нарушают модель.
7. **Bridge boundary.** Bridge — отдельный trust layer: он получает только path к
   raw 32-byte public-key file, а private signing seed остаётся в release CI. Старые
   fixed metadata rules не наследуются Static SDK автоматически.

Не предполагаются HSM, сеть, серверная проверка, remote revocation или private key в
репозитории. Target design включает Linux/Windows/macOS x86_64/arm64. Native runtime
baseline сейчас только Linux x86_64; Windows x86_64 PE32+ DLL подтверждена
cross-build/artifact gate без native execution.

## 4. Атакующий

### 4.1 Локальный reverse/debug attacker (in-scope)

Атакующий может читать shipping artifact, symbols, strings и публичный build/audit
metadata; дизассемблировать decoder; запускать приложение много раз; ставить
обычный debugger; пытаться наблюдать callback arguments и API use; модифицировать копию
артефакта; вводить malformed blob/tag; подменять исходные literal forms до сборки,
если контролирует source branch; сравнивать Dev и Hardened builds; анализировать
временные и размерные характеристики.

Static SDK должен усложнять поиск, обнаруживать проверяемые user-space debugger/hook
signals и быстро отвергать malformed/modified blob, но не обещает скрыть значение
от attacker после materialization callback.

### 4.2 Build/cache/debug-symbol attacker (in-scope при слабом CI)

Атакующий с доступом к рабочему каталогу, CI cache, process arguments, environment,
CMake cache, Rust flags/config, generated source, compiler logs, crash dumps, swap,
intermediate objects или unstripped symbols может получить seed, plaintext, derived
material или source paths. Это отдельный риск поставки: даже идеальный decoder не
исправляет утечку на build host.

Требования mitigation:

- seed только из ephemeral path; не из env/CLI/cache/flags/log/source/artifact;
- private build directory `0700`, очистка после build;
- audit report только hashes/metadata/tool versions;
- generated source/blob не содержит raw seed и raw absolute source path;
- split/strip debug symbols до scanner и shipping;
- CI cache не должен сохранять seed, plaintext, unstripped objects или temporary
  material; конкретная retention policy должна быть проверена отдельно.

Если эти требования нарушены, результат — утечка секрета/материала, а не «защищённый
артефакт с тем же уровнем assurance».

### 4.3 Privileged attacker (out-of-scope)

Root/administrator, контролирующий kernel/hypervisor/loader, может менять процесс,
перехватывать file/memory reads, патчить core, подменять открытые ключи/бинарник,
снимать plaintext и отключать abort. То же относится к атакующему, который заменил
SDK/application до доверенной сборки. Static SDK не защищает от полного контроля
execution environment.

## 5. Угрозы и требуемые реакции

| Угроза | Ожидаемая реакция v1-alpha | Остаток |
|---|---|---|
| Прямой literal остаётся в read-only data | Post-link scanner сообщает finding и build fail в Hardened | Сложные compiler copies/encoding могут требовать будущего анализа |
| Неподдержанный runtime expression замаскирован macro | Source scanner fail; silent fallback запрещён | Parser bugs требуют cross-language corpus |
| Blob/tag/site mutation | Decoder fail-closed до callback | Патч attacker-owned decoder/application вне model |
| Unknown blob version/profile/bounds | Fail-closed, callback не вызывается | Верность всех bounds checks ещё должна быть доказана кодом |
| Seed попал в env/CLI/cache/log | Build audit/review fail; artifact не публикуется | OS forensic traces и privileged access остаются |
| Debug symbols/absolute path попали в shipping | Split/strip и scanner fail | Symbols могут легально храниться отдельно и быть доступны разработчику |
| Scalar raw bits видны в регистрах/ABI | Документируется как residual; numeric scan обещает только reconstruction assurance | Нельзя гарантировать отсутствие всех raw bits |
| Plaintext захвачен callback/debugger | Явная non-goal; минимальный scope и wipe снижают окно | Copy, register, swap, dump и API consumer могут сохранить его |
| LTO/inlining растворяет decoder boundary | Configure/build fail, если no-cross-module policy не enforce | Compiler/platform coverage ещё незавершена |
| Bridge применён до package/signing mutation | Integration pipeline должен отклонить порядок | CI orchestration и all-platform signing ещё не завершены |
| Bridge явно включён на unsupported target | Configure/build fail; silent disable запрещён | Bridge фактически проверен только Linux ELF64LE x86_64 |
| Короткий literal (<8 units/bytes) найден/не найден scanner | Остаётся protected, статус `non-auditable-short` | Нет глобальной гарантии отсутствия короткой последовательности |
| Software breakpoint/JMP в entry критичной функции | Entry-byte prologue check и sealed `.text` завершают процесс до callback | Hardware breakpoint, kernel patch и полностью resealed attacker build остаются |
| Runtime `.text` mutation | `.text` hash вычисляется у callback и сравнивается с post-link seal | Attacker, патчащий и check, и seal/loader, находится за пределом claim |
| Frida-Gum/preload module | Linux проверяет `LD_PRELOAD`/`/proc/self/maps`, Windows перечисляет modules через Toolhelp; scan rate-limited до 1 ms активного use | Переименованный/reflective injector и kernel concealment могут обойти эвристику |
| Windows debugger/IAT redirect | `IsDebuggerPresent`, `CheckRemoteDebuggerPresent`, critical Kernel32 import targets и PE section protections проверяются у callback | Native Windows runtime fixtures ещё не выполнены; kernel/API spoofing остаётся |
| Длительный single-step/breakpoint pause | Calibrated `RDTSCP` window abort при превышении 100 ms | Короткие паузы и TSC virtualization могут не обнаружиться |
| VM/sandbox | CPUID/MAC/syscall timing формируют отдельный signal и усиливают integrity checks | VM сама по себе разрешена; эвристика не доказывает наличие анализа |

## 6. Profiles и failure semantics

- **Dev** — единственный явно разрешённый unprotected profile. Он не даёт
  confidentiality claim и не должен быть выбран автоматически из-за build error.
- **Hardened** — единый C++ shipping profile: ARX-obfuscated blob, mandatory LLVM
  transformations/runtime guards, `.text` sealing, scoped callback, wipe и scanners.
  Любая отсутствующая build capability либо подтверждённое runtime нарушение
  останавливает build/process.
- **Paranoid** — зарезервированное имя, не отдельный shipping profile.

Runtime decoder обязан проверить version, profile, site binding, bounds и integrity
tag до callback. При ошибке callback не вызывается и plaintext не выдаётся; exact
abort/error surface закрепляется отдельным Rust/C++ API contract, но семантика
fail-closed и parity corpus обязательны. Build error не должен публиковать partial
shipping artifact.

## 7. Что именно гарантируется при assumptions

При honest compiler/toolchain/OS, корректном source scanner, неповреждённом core и
отсутствии privileged control Static SDK может:

1. повысить стоимость grep/strings и прямого извлечения поддержанных literals;
2. обнаружить malformed/tampered blob до callback;
3. ограничить время жизни plaintext scoped callback/RAII object и попытаться wipe;
4. дать audit evidence о site/profile/tool versions без публикации seed/plaintext;
5. проверить отсутствие части длинных protected strings/bytes в финальном stripped
   artifact и отдельно пометить короткие/numeric ограничения.
6. на Linux обнаружить проверенные runtime E2E случаи CFG/watermark/material/
   `.text`/seal mutation, entry `INT3`, preload signal и callback pause; на PE32+
   artifact gate обнаружить `.text`/seal mutation offline verification.

Это не доказательство того, что plaintext нигде не существует, что binary нельзя
патчить, или что decoder нельзя instrument/replace.

## 8. Residual risks

- **Runtime exposure:** plaintext неизбежен во время use; compiler registers/spills,
  copies, allocator, swap, crash dump и downstream API могут его сохранить.
- **User-space memory dump:** process-memory snapshot в точном окне callback может
  снять plaintext независимо от static obfuscation, sealing и watermark.
- **Static emulation:** текущий bounded Rizin `aae` run не восстановил plaintext и
  остановился на unsupported relocation/memory semantics; это не доказательство,
  что ручная ESIL/RzIL/QEMU/Unicorn-эмуляция невозможна.
- **Build host/cache:** ephemeral cleanup не гарантирует уничтожение forensic следов;
  CI cache, backups, shell history, process inspection и debug tooling требуют
  отдельной hardening policy.
- **Toolchain drift:** незафиксированные compiler/linker/LTO/strip flags, optimizer
  changes и platform ABI могут раскрыть literals или нарушить site identity.
- **Scanner coverage:** disassembly, custom encodings, generated code, linker folding,
  dead data и формат-specific sections могут потребовать дополнительных rules; short
  literals intentionally non-auditable.
- **ARX limits:** diversified ARX не является доказанной криптографической защитой;
  известный plaintext/material и reverse-engineering могут снижать стоимость анализа.
- **Key separation:** компрометация Bridge private signing seed позволяет выпускать
  целостные Bridge artifacts, но не должна автоматически раскрывать static
  `build_seed`; компрометация `build_seed` не должна давать Bridge signing ability.
- **Cross-platform readiness:** Windows x86_64 PE32+ DLL compile/link/seal/artifact
  gate реализован, но native runtime execution/CI pending; macOS и arm64 остаются
  design targets. Нельзя переносить Linux runtime baseline на эти платформы.
- **Application misuse:** logging, serialization, `c_str()` with embedded NUL,
  storing RAII objects, broad callbacks и копирование в owning containers могут
  продлить plaintext lifetime за пределы защиты.
- **Privileged/runtime patching:** root, kernel, hypervisor, loader replacement,
  preloading/interposition или patching trusted core полностью обходят assumptions.
- **Physical access/kernel debugger:** cold-boot/DMA/physical-memory access и
  kernel debugger явно не покрываются; они способны скрыть tracer/modules и менять
  loaded code/check results.
- **Timing/VM heuristics:** false negatives возможны; VM signal не является причиной
  отказа сам по себе, а 100 ms timing threshold не ловит короткий single-step.
- **Replay/freshness:** Static SDK не является online freshness/entitlement system;
  старый корректно собранный artifact может быть воспроизведён.
- **Watermark limits:** site-derived watermark помогает связать утечку с site/build
  lineage, но может быть найден и удалён опытным attacker; он не является подписью,
  remote attestation или доказательством происхождения.

## 9. Relationship to Binary Integrity Bridge

Bridge добавляет проверку целостности **финального** binary после последней мутации.
Он не скрывает static literals, не заменяет decoder tag, не меняет profile semantics
и не делает Static SDK cross-platform автоматически. Его fixed 512-byte `.mindguard`
metadata, Ed25519 signing seed/public key lifecycle, format coverage и fail-closed
rules остаются в [protocol-v1.md](protocol-v1.md).

Bridge является explicit opt-in: если пользователь попросил его, но target не
поддерживается текущим Protector, configure/build fail. Если opt-in не задан, Static
SDK всё равно обязан выполнить собственные scanner/blob checks; pipeline не должен
silently включать Bridge или обещать anti-tamper.

## 10. Non-goals

Не являются обещаниями v1-alpha:

- абсолютная/невзламываемая secrecy или защита plaintext после use;
- абсолютная debugger/hook/VM detection и защита от переименованного либо
  kernel-assisted instrumentation;
- intentional UB, illegal instructions, stack corruption, unsafe anti-disassembler;
- web UI, dashboard, backend, network, HSM, remote revocation;
- private signing/build key в repository, artifact, log или generated source;
- автоматические Authenticode/Apple signing operations;
- arbitrary header/source protection за пределами literal boundary;
- global absence guarantee для коротких literals или raw-bit scan для numeric values;
- заявление, что все Linux/Windows/macOS x86_64/arm64 targets уже реализованы и
  протестированы.
