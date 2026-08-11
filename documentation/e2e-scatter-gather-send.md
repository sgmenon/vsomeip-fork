# E2E scatter-gather send in vsomeip

> **Status.** Implemented in this fork for stock AUTOSAR E2E
> (`libvsomeip3-e2e`). Written as a mainline candidate: the wire format is
> unchanged, the receive path stays contiguous, and apps that already sent
> hole-free payloads keep working. Apps that still pre-allocate E2E holes
> will double-size on the wire — which is a feature if you like oversized
> CRCs, and a bug if you do not.

## 1. Why this belongs in mainline

E2E protection is supposed to be a stack concern. In practice, old vsomeip
asked applications to know header sizes, leave placeholder bytes in the
payload, and hope the plugin wrote into the right holes. That is the
middleware equivalent of asking the passenger to bring their own seatbelts.

The redesign has three goals that mainline benefits from equally:

1. **Apps send user data only.** No pre-sized E2E holes. Profile/config
   changes stop leaking into every client and service sample.
2. **The plugin owns the E2E header buffer.** `protect_parts` returns
   owned pieces instead of mutating a caller buffer in place.
3. **Send stays scatter-gather to the socket.** Routing and endpoints
   pass a multi-buffer sequence into Asio (`async_write` /
   `async_send`) instead of concatenating everything into one
   `vector<byte_t>` first.

Item (3) matters: if you only invent a nicer protect API and then
`memcpy` the result back into a single train buffer, you have improved
the paperwork and kept the copy tax. The queue type and the socket type
had to change together.

Receive is intentionally boring. Datagrams and TCP streams already
reassemble to contiguous memory; `check()` and strip stay on that path.

## 2. What changed (short version)

| Layer | Before | After |
|---|---|---|
| App payload | Often pre-sized with E2E holes | Hole-free user data |
| E2E plugin | In-place `protect(e2e_buffer&)` | Public `protect_parts` → `protect_result` |
| Routing send | Protect, maybe flatten, `send(byte*, size)` | Compose `send_buffer_sequence`, always `send(sequence)` |
| Endpoint queue / train | One contiguous `message_buffer_ptr_t` | Owning `send_buffer_sequence` |
| TCP / UDP sockets | Single `const_buffer` | `vector<const_buffer>` (`buffers()`) |

JSON E2E configuration is unchanged. Profiles, offsets, and
`e2e_enabled` keep working the same way — only the *application payload
contract* changed.

## 3. Application contract (migration)

**New rule:** the SOME/IP payload you hand to vsomeip is unprotected
application data only. The stack inserts the E2E header (and updates the
SOME/IP length field) before the frame hits the wire.

**Breaking for old hole-prealloc apps:** if your app still pads the
payload with room for the E2E header, the plugin will add *another*
header on top. The CRC will look fine to someone who enjoys chaos; the
peer will not.

Receive delivery to the application is hole-free again after a successful
check + strip. Application handlers should not see the E2E header bytes.

External E2E plugins (for example GM SecOC) should implement the same
`protect_parts` contract when they move onto this path.

## 4. The type that carries it: `send_buffer_sequence`

Defined in
[`implementation/endpoints/include/buffer.hpp`](../implementation/endpoints/include/buffer.hpp).

It is an owning multi-buffer send unit:

- **`storage_`** — `shared_ptr`s to the real byte vectors (lifetime across
  async completion)
- **`buffers_`** — Asio `const_buffer` views over those vectors

Useful operations:

- `append` / `append_sequence` — build or batch without forced concat
- `buffers()` — what the socket write path consumes
- `read_byte` / `read_uint16_be` — peek SOME/IP fields without flattening
- `flatten()` — escape hatch for legacy pointer-arithmetic paths
  (SOME/IP-TP split, tracing). Prefer not to live there.

The nPDU `train` now holds a `send_buffer_sequence_ptr_t` and appends
passenger messages with `append_sequence` instead of copying bytes into
one blob.

## 5. Protect API

Public surface
([`e2e_provider.hpp`](../implementation/e2e_protection/include/e2e/profile/e2e_provider.hpp),
[`protector.hpp`](../implementation/e2e_protection/include/e2e/profile/profile_interface/protector.hpp)):

```cpp
protect_result protect_parts(buffer_view app_payload, instance_t instance);
```

[`protect_result`](../implementation/e2e_protection/include/e2e/profile/protect_result.hpp)
is either:

- **scatter:** optional `leading_gap` + `e2e_header` + `app_payload`
  (common `offset_ == 0` case: header then payload), or
- **`contiguous`:** one owned buffer for awkward layouts (Profile 01 bit
  packing, non-zero offsets that do not map cleanly to a prefix).

Private per-profile `protect(e2e_buffer&)` helpers may still exist as
implementation details. They are not part of the public plugin API.

Routing composes the on-wire sequence in
`compose_e2e_protected_sequence` inside
[`routing_manager_impl.cpp`](../implementation/routing/src/routing_manager_impl.cpp):

```
[SOME/IP header with fixed length] + [E2E pieces from protect_parts]
```

then calls `endpoint::send(sequence)`. Contiguous non-E2E sends wrap
`byte*` into a one-buffer sequence. The preferred endpoint API is the
sequence overload; `send(byte*, size)` remains a thin non-virtual
wrapper for SD hosts and similar callers.

## 6. Data path

```
App (hole-free payload)
  → serialize SOME/IP
  → protect_parts (plugin owns E2E header)
  → send_buffer_sequence
  → train.append_sequence (batch without concat)
  → async_write / async_send (buffers())
```

On receive: contiguous buffer → `check()` → strip E2E framing → deliver
hole-free payload to the application.

## 7. Compatibility notes

- **Wire format:** unchanged for standard profiles at `offset_ == 0`.
  Peers that already spoke AUTOSAR E2E keep interoperating.
- **Config:** unchanged (`e2e` block in JSON).
- **Local IPC quirk:** the local send path historically skips E2E
  protect; this redesign does not invent a new story there.
- **Open tech debt:** SOME/IP-TP still flattens multi-buffer sequences
  before splitting. Scatter through TP is a follow-up, not a blocker for
  the E2E framing goal.

## 8. How to verify

```bash
# Unit / in-process
bazel test //test/unit_tests/endpoint_tests:endpoint_tests
bazel test //test/network_tests/fake_socket_tests:fake_socket_tests

# Two-node Docker (host Bazel build + Compose)
./test/network_tests/docker_tests/run-test.sh e2e_crc
./test/network_tests/docker_tests/run-test.sh e2e_p04
./test/network_tests/docker_tests/run-test.sh e2e_p07
```

See also [`test/network_tests/docker_tests/README.md`](../test/network_tests/docker_tests/README.md).

## 9. Mainline ask

Land the stock path as the default E2E send model:

1. Public protect API is `protect_parts` only.
2. Endpoint send prefers `send_buffer_sequence`.
3. Document the hole-free app contract (this file + configuration guide).

Optional follow-ups that need not block the merge: train-batching stress
without E2E, SecOC plugins on the same contract, TP without `flatten()`.
