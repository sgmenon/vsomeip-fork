# Receive-path copy elision

> **Status:** In progress on this fork. Goal: pin the receive buffer (or one owned
> buffer) with `shared_ptr` and hand spans / shared payloads down to the app,
> instead of re-copying at strip, deserialize, and notify. Mirror of
> [`send-copy-elision.md`](send-copy-elision.md).
>
> **Ownership rule (routing ingress):** crossing into service-level routing
> receive always takes a required `owned_buffer_slice` (non-null buffer +
> explicit offset/length). Same spirit as send’s required `message_buffer_ptr_t`.
> No `const byte_t*` on the RM data-plane path.
> Copies, if any, happen at the endpoint edge (`copy_of` / copy-out, or `slice`
> of an existing pin).

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

## Copy count by era

Counts are **extra payload-sized copies after the socket/IPC fill**. Owned 16B
E2E headers are noted separately, not as “1×”.

Three columns only — do not mix stages across eras in one row:

| Path                                      | Before scatter / pin work           | Now (required slice)                                                                  | Target                                 |
| ----------------------------------------- | ----------------------------------- | ------------------------------------------------------------------------------------- | -------------------------------------- |
| UDP → hosting app (no E2E)                | several (strip + deserialize + …)   | **0×** (datagram pin via `slice`/`whole`)                                             | **0×**                                 |
| TCP → hosting app (no E2E)                | several                             | **0×** when frame fills window (`gap == 0`); else **1×** copy-out; pool drop if empty | **0×** (assemble-into-lease)           |
| Local UDS/TCP IPC → stub                  | several                             | **1×** (copy-out of complete **command**; stub moves when unique)                     | **0×** (optional move of whole window) |
| Network → hosting app (with E2E)          | several more (materialize strip, …) | UDP **0×** / TCP **0×** if fill else **1×**; then views                               | **0×** (+ shared filter)               |
| Network → other local (E2E strip forward) | full concat / materialize           | same edge as above; then **16B** hdr + app slice                                      | **0×** edge; same **16B** + slice      |
| Stub SEND → RM                            | often already owned / varied        | **0×** (`slice` of IPC frame)                                                         | **0×**                                 |
| SOME/IP-TP reassemble                     | **1×** full                         | **1×** full                                                                           | **1×** until TP elision                |
| Proxy receive (RM → app)                  | **+2–3×** after IPC fill            | **1×** edge; then **0×** (`build_message_from_buffer` pin)                            | **0×** (optional move of whole window) |

**Now, in one sentence:** All routing ingress uses
`routing_host::on_message(owned_buffer_slice)`. UDP pins the datagram
`shared_ptr`. Remote TCP uses `take_stream_frame` + `message_buffer_pool`
(LIFO lease/adopt; drop when empty — no fallback alloc under stall). Move when a
complete frame fills the used region at offset 0; otherwise pooled copy-out.
Local stream servers still copy-out each complete command (compaction
unchanged). After the edge owns a slice, deliver / E2E / local forward /
proxy SEND receive only view or pin.

## Design notes

### Lifetime

`routing_host::on_message(owned_buffer_slice, …)` keeps the frame buffer alive
for the duration of processing and any `payload_impl` / `buffer_sequence`
built from it. Stream endpoints do **not** pin into a still-active compaction
window — they `take_stream_frame` (move when `gap == 0` and the frame fills
used bytes; else copy-out) before any leftover memmove.

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
3. UDP: pass datagram pin as `whole` / used-bytes `slice` (done; server no longer
   recycles the recv buffer).
4. TCP/UDS: remote TCP uses `take_stream_frame` (move when frame fills used
   window at gap 0; else copy-out). Local UDS/TCP still copy-out commands.
   `routing_host` byte\* + RM `copy_of` adapter retired.

### Proxy path

Non-routing apps (`routing_manager_client`) receive SEND over local IPC, parse
the command header in place, and pin the SOME/IP region with
`build_message_from_buffer` (**0×** after the stream edge copy-out). Notifications
to the proxy arrive as `SEND_ID` with notification message type on the same path.

## Implementation checklist

### Done — ownership + RM path (not end-to-end zero-copy)

- [x] Required `owned_buffer_slice` at service-level routing ingress
- [x] Unified slice type for receive and `buffer_sequence` segments
- [x] Stub SEND: `slice` of IPC frame (**0×** on that path)
- [x] Host deliver / notify / E2E strip: views + scatter only (no extra payload copies after the edge owns a slice)
- [x] `payload_impl` view-only; unit tests for payload pin + e2e protect

### Done — network / local edge

- [x] **UDP:** pass datagram recv `shared_ptr` as `whole` / used-bytes `slice`
- [x] `routing_host::on_message(owned_buffer_slice, …)` only (byte\* + `copy_of` adapter retired)
- [x] **TCP:** `take_stream_frame` + `message_buffer_pool` (move when `gap == 0`
      fills used region; else copy-out; **drop** when pool empty)
- [x] **Local UDS/TCP:** copy-out complete command (tags stripped) → stub slice path (move when unique)
- [x] **Integration:** E2E check + strip-by-span with a real `owned_buffer_slice` pin
      (`ut_check_strip_pin` — spans / `payload_impl` / stripped sequence share the frame buffer;
      docker `e2e_*` remains wire CRC smoke only)
- [x] Public `application` / `message` / `payload` APIs unchanged (handlers still get `shared_ptr<message>`; payload may view a pin)
- [x] Proxy / routing-client SEND receive: header-only parse + `payload_impl` pin (notifications via same `SEND_ID`)

### Later / separate tracks

- [ ] SOME/IP-TP reassembly without full flatten
- [x] Optional TCP micro-opt: move whole stream buffer when `gap == 0` and size == full used region (`take_stream_frame`)
- [x] TCP `message_buffer_pool` (LIFO recycle; config `tcp-receive-buffer-pool-size`; drop when empty)
- [ ] Optional UDS micro-opt: same move-whole-window for local stream commands
- [ ] SD `on_message` → `owned_buffer_slice` (deferred; still `byte*` — RM passes pin bytes into SD today)

## Related docs

- [`send-copy-elision.md`](send-copy-elision.md) — send-side zero-copy / pin model
- [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) — E2E protect/check APIs
  and `check_result` spans
