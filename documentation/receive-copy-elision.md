# Receive-path copy elision

> **Status:** In progress on this fork. Goal: pin the receive buffer (or one owned
> buffer) with `shared_ptr` and hand spans / shared payloads down to the app,
> instead of re-copying at strip, deserialize, and notify. Mirror of
> [`send-copy-elision.md`](send-copy-elision.md).
>
> **Ownership rule (routing ingress):** crossing into service-level routing
> receive always takes a required `owned_buffer_slice` (non-null buffer +
> explicit offset/length). Same spirit as send’s required `message_buffer_ptr_t`.
> No `const byte_t*` + optional `_pin = nullptr` on the RM data-plane path.
> Copies, if any, happen at the endpoint/stub edge (`copy_of` or `slice` of an
> existing pin).

## Problem

A remote message that reaches the hosting application is typically already
contiguous in an endpoint receive buffer (UDP / TCP / local). E2E `check()`
returns **non-owning spans** into that buffer. Upstream then threw that away
with full-frame strip + deserialize copies.

## Ownership model

**Crossing into routing receive ⇒ always an `owned_buffer_slice`.**

```cpp
struct owned_buffer_slice {
    message_buffer_ptr_t buffer; // required when valid()
    std::size_t offset{0};
    std::size_t length{0};       // explicit; never "0 means whole buffer"
};
```

Defined in `implementation/endpoints/include/buffer.hpp`. Same type is also a
segment of `buffer_sequence`. Helpers: `whole`, `slice`, `copy_of`,
`from_pointer`, `is_whole()`. Length is always explicit — never “0 ⇒ whole”.

**`payload_impl`:** always non-owning — `buffer_` + `offset_` + `length_`.

**Local forward after E2E check:** `send_local(buffer_sequence)` with:

- owned 16-byte SOME/IP header (length patched for hole-free size)
- `append_buffer_slice` of `check_result.app_payload` when it lies in the
  frame’s buffer, else one owned buffer of **app bytes only**

```mermaid
flowchart LR
  ep["endpoint / stub"]
  slice["owned_buffer_slice"]
  rm["RM on_message / deliver"]
  payload["payload_impl view"]
  ep -->|"pin or one copy"| slice --> rm --> payload
```

## Copy inventory (routing host, after this work)

| Stage                                      | Copies                     | Notes                                                         |
| ------------------------------------------ | -------------------------- | ------------------------------------------------------------- |
| UDP/TCP/UDS → service `on_message` (today) | **1×** full frame          | Endpoints still call `routing_host(byte*)`; RM does `copy_of` |
| Stub SEND → service `on_message`           | **0×**                     | `owned_buffer_slice::slice` of IPC frame                      |
| UDP → service `on_message` (target)        | **0×**                     | Pass datagram pin as `whole` / used-bytes `slice`             |
| TCP/UDS → service `on_message` (target)    | **0×** or **1×**           | Exact-frame alloc (0× after read) or copy-out of stream       |
| SOME/IP-TP reassemble                      | **1×** full                | Only when TP segments                                         |
| E2E `check()`                              | **0×**                     | Spans into pinned / copied frame                              |
| Host deliver                               | **0×** payload             | `payload_impl` views frame buffer                             |
| Local forward after E2E                    | **0×** app; **16B** header | `compose_e2e_stripped_sequence(frame, …)`                     |
| Proxy client deserialize                   | **+2–3×**                  | Open — see checklist                                          |

### Totals (payload-sized, remote → hosting app on RM)

| Path               | Today (no endpoint pin) | Target (with pin / exact frame) |
| ------------------ | ----------------------- | ------------------------------- |
| Req/resp, no E2E   | **1×** (edge `copy_of`) | **0×**                          |
| Req/resp, with E2E | **1×**                  | **0×**                          |
| Event, with E2E    | **1×**                  | **0×** (+ shared filter)        |
| Stub IPC → RM      | **0×**                  | **0×**                          |

After the edge owns an `owned_buffer_slice`, host/local paths are view/scatter only.

## Design notes

### Lifetime

`routing_host::on_message(const byte_t*, …)` pointers stay valid only until the
next receive reuse. After `copy_of` / `slice`, the `owned_buffer_slice` (and
any `payload_impl` / `buffer_sequence` built from it) keep the buffer alive
until handlers / async local writes complete.

### Deliver without full-frame deserialize

`deliver_message(owned_buffer_slice)` parses SOME/IP header fields and attaches
a viewing `shared_ptr<payload>`. No `deserializer::set_data` of the full frame
on the host path.

### Notifications

One `shared_ptr<payload>` for debounce/filter and host delivery. Other local
subscribers get `send_local(sequence)`.

### Staging (endpoints)

1. Stub SEND: slice of IPC SOME/IP region (done).
2. RM internal forwards / deliver: consume slice only (done).
3. UDP: pass datagram `message_buffer_ptr_t` as `whole(recv_buf)` (next).
4. TCP/UDS: exact-frame alloc after 16B length parse, or one copy out of the
   stream window (avoids compaction invalidation) — open until then.

### Proxy path (open)

Non-routing apps still copy on IPC receive + `send_command` + deserialize.
Scatter send already works; receive elision is separate.

## Implementation checklist

### Done — ownership + RM path (not end-to-end zero-copy)

- [x] Required `owned_buffer_slice` at service-level routing ingress
- [x] Unified slice type for receive and `buffer_sequence` segments
- [x] Stub SEND: `slice` of IPC frame (**0×** on that path)
- [x] Network interim: `routing_host(byte*)` → `copy_of` into RM (**1×** until endpoints pin)
- [x] Host deliver / notify / E2E strip: views + scatter only (no extra payload copies after the edge owns a slice)
- [x] `payload_impl` view-only; unit tests for payload pin + e2e protect

### Remaining — toward network **0×** target

- [ ] **UDP:** pass datagram recv `shared_ptr` as `whole` / used-bytes `slice` (drops the edge `copy_of`)
- [ ] **TCP/UDS:** exact-frame alloc after 16B length parse (preferred), or copy one message out of the stream window — do **not** pin into a buffer that compaction can move
- [ ] Prefer `on_message(owned_buffer_slice, endpoint*, …)` at `routing_host`; retire byte\* + `copy_of` once endpoints are plumbed
- [ ] Network / integration coverage for E2E check + strip-by-span with a real pin

### Later / separate tracks

- [ ] Proxy / routing-client deserialize elision
- [ ] SOME/IP-TP reassembly without full flatten
- [ ] Sync receive notes in [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md)
- [ ] SD `on_message` (stays `byte*` until SD needs it)
- [ ] Public `application` / `message` APIs unchanged by design

## Related docs

- [`send-copy-elision.md`](send-copy-elision.md) — send-side zero-copy / pin model
- [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) — E2E protect/check APIs
  and `check_result` spans
