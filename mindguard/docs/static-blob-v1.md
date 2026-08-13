# MindGuard Static Blob v1

Этот документ фиксирует byte-level contract M2. Он не расширяет и не использует
512-byte `.mindguard` block Binary Integrity Bridge.

Все integers little-endian. Blob состоит из 96-byte header и `payload_len` bytes
payload. Total size обязан быть ровно `96 + payload_len`, без trailing data.

| Offset | Size | Field | v1 value / rule |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `MGSTV1\0\0` |
| 8 | 2 | `version` | `1` |
| 10 | 1 | `profile` | `1` (`Hardened`) |
| 11 | 1 | `kind` | `1` string, `2` bytes, `3` scalar |
| 12 | 2 | `header_size` | `96` |
| 14 | 2 | `flags` | `0` |
| 16 | 8 | `site_id` | expected compile-time site hash |
| 24 | 4 | `plaintext_len` | `0..65536` |
| 28 | 4 | `payload_len` | равно `plaintext_len` в v1 |
| 32 | 8 | `diversifier` | derived per-site ARX selector |
| 40 | 32 | `blob_share` | первая XOR share decoder material |
| 72 | 16 | `tag` | ARX tag, описанный ниже |
| 88 | 8 | `reserved` | все нули |
| 96 | variable | `payload` | ARX-stream XOR ciphertext |

Вторая conceptual 32-byte `code_share` генерируется отдельно. Для backward
compatibility build tool поддерживает `raw-v1` (ровно 32 байта), но shipping flow
должен использовать `packed-v1` (ровно 256 байт), чтобы exact `code_share` не лежала
contiguous в artifact. Decoder material равен `blob_share XOR code_share`; ни одна
share отдельно не является material.

### Packed code share v1

`packed-v1` не меняет Static Blob v1 и выбирается build integration/API отдельно.
256-byte table сначала детерминированно заполняется BLAKE3 XOF с context
`mindguard.dev/static-protection/v1/packed-code-share/2026-08-13` от site root.

Для logical index `j = 0..255` физическая позиция:

```text
odd = low8(site_id XOR rotl(diversifier, 17)) OR 1
offset = low8(rotr(site_id, 11) XOR diversifier)
position(j) = low8(j * odd + offset)
```

Нечётный `odd` задаёт перестановку всех 256 позиций. Для каждого byte `i = 0..31`
семь XOF bytes остаются на `position(i + lane*32)`, `lane = 0..6`, а byte lane 7
корректируется так, чтобы XOR всех восьми lanes был равен `code_share[i]`. Поэтому
каждый packed byte участвует ровно в одном reconstructed byte; mutation любого из
256 bytes меняет material и должна завершиться `Tag` до plaintext. Это статическое
рассеивание, а не криптографическая секретность: reverse engineer всё ещё может
воспроизвести перестановку из decoder.

## Derivation

`mindguard-build` использует pinned BLAKE3 `1.8.5` и
`Hasher::new_derive_key` с context
`mindguard.dev/static-protection/v1/site-material/2026-08-12`.
Length-prefixed input в фиксированном порядке:

1. exact raw 32-byte `build_seed`;
2. UTF-8 `project_id`;
3. UTF-8 `release_id`;
4. little-endian `site_id`;
5. `profile`, затем `kind`.

От полученного root отдельными BLAKE3 derive-key contexts
`mindguard.dev/static-protection/v1/decoder-material/2026-08-12`,
`mindguard.dev/static-protection/v1/blob-share/2026-08-12` и
`mindguard.dev/static-protection/v1/diversifier/2026-08-12` выводятся 32-byte
decoder material, 32-byte `blob_share` и 32 bytes, первые 8 из которых образуют
`diversifier`. `code_share`
вычисляется XOR. Domain strings являются частью v1 и не меняются молча.

## ARX stream

Material читается как четыре little-endian `u64`. Для каждого 32-byte block
state связывается с `site_id`, `diversifier` и block counter, затем выполняет
восемь ARX rounds (`wrapping add`, XOR, rotate-left). Один из четырёх rotation
schedules выбирается двумя bits diversifier/counter:

```text
(32,24,16,63) (31,17,47,23) (13,37,29,43) (7,19,41,53)
```

Для block counter `n` начальный state:

```text
a = k0 XOR site_id
b = k1 XOR diversifier
c = k2 XOR n
d = k3 XOR 0x9e3779b97f4a7c15
```

Для `round = 0..7` schedule выбирается как
`(diversifier XOR n XOR round) & 3`, затем выполняется:

```text
a += b; d = rotl(d XOR a, r0)
c += d; b = rotl(b XOR c, r1)
a += b; d = rotl(d XOR a, r2)
c += d; b = rotl(b XOR c, r3)
a ^= 0x9e3779b97f4a7c15 + round
c ^= n + round
```

Все additions wrapping `u64`. Output block — little-endian `a,b,c,d`.
State bytes XOR-ятся с plaintext/ciphertext. Последний block обрабатывается до
exact length, padding не добавляется.

## Tag и порядок decoder checks

Tag — две `u64` ARX accumulators, инициализированные как
`t0 = k0 XOR k2 XOR site_id`, `t1 = k1 XOR k3 XOR diversifier`.
Он покрывает header bytes `0..72`, `88..96` и весь ciphertext; поле tag исключено.
Для каждого byte `x` с непрерывным index `i`, начиная с нуля:

```text
t0 = rotl(t0 XOR (x + i * 0x9e3779b97f4a7c15), 13) + t1
t1 = rotl(t1 + (x XOR i) + 0xd6e8feb86659fd93, 29) XOR t0
```

После данных для `round = 0..7`:

```text
t0 = rotl(t0 + t1 + round * 0x9e3779b97f4a7c15, 17) XOR k[round & 3]
t1 = rotl(t1 XOR t0, 41) + k[(round + 1) & 3]
```

Output tag — little-endian `t0 || t1`. Comparison constant-time.

Decoder проверяет до materialization в таком порядке: total/header bounds,
magic, header size/flags/reserved, version, profile, kind, expected site,
length equality и 64 KiB limit, exact total size, tag. Только после успеха
разрешены decrypt и callback. Любая ошибка не вызывает callback.

Tag обнаруживает случайное/неполное изменение и связывает поля, share и payload.
Он не является криптографической аутентификацией против локального attacker,
который контролирует binary и может восстановить обе shares или переписать
decoder; такой attacker уже может patch-нуть сам callback.
