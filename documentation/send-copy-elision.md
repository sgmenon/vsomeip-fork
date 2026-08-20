# Send-path copy elision

> **Status:** In progress on this fork. Goal: one payload-sized copy (or zero with
> move) from application bytes to `async_send`, down from three or more on
> upstream COVESA 3.6.1.

## Problem

On upstream vsomeip, a typical remote send with E2E and nPDU debouncing copied
application payload several times before the socket:

1. **App** — pre-sized E2E holes (old contract): copy into padded buffer
2. **Serialize** — `payload` → pooled serializer `vector` (full SOME/IP frame)
3. **E2E routing** — `its_buffer.assign(_data + base, _data + _size)` + protect
   ([upstream routing_manager_impl.cpp ~669](https://github.com/COVESA/vsomeip/blob/master/implementation/routing/src/routing_manager_impl.cpp#L669))
4. **Train (pre scatter-gather)** — `train_->buffer_->insert(...)` concat copy

Scatter-gather removed (4). Hole-free E2E removed (1). This work removes (2) and
(3) for the `app_->send(message)` path on the routing host, and plugin copy **D**.

## Target

| Stage                               | Before                               | After                                       |
| ----------------------------------- | ------------------------------------ | ------------------------------------------- |
| App `set_data(ptr)`                 | 1× into `shared_ptr<payload>`        | unchanged (unavoidable from stack/temp)     |
| App `set_data(move(vec))`           | 0×                                   | 0×                                          |
| Serialize full frame                | 1×                                   | **skipped** on routing-host `send(message)` |
| E2E plugin app copy                 | 1×                                   | **0×** (span into payload)                  |
| Routing `send_buffer_sequence(ptr)` | 1×                                   | **0×** when sequence pins payload           |
| nPDU train                          | 0× (refs)                            | 0×                                          |
| Socket queue                        | holds `shared_ptr`s until `send_cbk` | same                                        |

**Profile 01 exception:** one pack copy into owned buffer (CRC/counter/nibble
in-band). Header/footer profiles: plugin owns E2E meta only.

## Design

### Lifetime

`send()` / `notify()` return after queueing. The endpoint holds
`send_buffer_sequence` until `async_send` / `async_write` completes. Pins on
`shared_ptr<payload>` (and owned E2E/header vectors) keep bytes alive — no free
callback.

### Exclusive pin vs shared snapshot

At the public API boundary (`application::send` / `notify`):

- **Exclusive** — caller passes the only remaining `shared_ptr` (typically via
  `std::move`). Payload is **pinned**; no payload-sized copy.
- **Shared** — caller still holds a live `shared_ptr`. Bytes are **snapshotted
  once** into a fresh payload before routing stores/pins it, so later
  `set_data` cannot corrupt in-flight async writes.

Do not poll `use_count()` from application code; use `std::move` when you want
zero-copy.

- **`notify` / `notify_one`:** `snapshot_payload_if_shared` on the payload
  argument.
- **`send(message)`:** `ensure_exclusive_message_payload` on the message (shared
  message, or exclusive message whose payload is still shared with the caller).

Routing **always pins** `shared_ptr<payload>` into `send_buffer_sequence`. The
`_message_exclusive` flag is not threaded through RM.

### Optional completion handler

```cpp
app->send(msg, [](bool ok) { /* local writes done */ });
app->notify(service, instance, event, payload, false, [](bool ok) { ... });
```

Invoked **once** when all async writes started by that call **in this process**
complete (`ok` is the AND of those results). Posted through the application
dispatcher.

- **Proxy client:** covers the IPC write to the routing manager only.
- **Routing host:** covers remote / local-subscriber writes started here
  (first hop = last hop).
- Debounce-delayed later sends are out of scope.
- If nothing is queued (unchanged field / no subscribers): fires immediately
  with `true`.

### Types

- **`protect_result::app_payload`** — non-owning `span` into caller/pinned payload
- **`protect_result::owned_app_payload`** — Profile 01 packed buffer only
- **`send_buffer_sequence::append_payload`** — ordered segment; no byte copy
- **`send_completion_state`** — latch attached to sequences; fired from endpoint
  `send_cbk`
- **`compose_e2e_protected_sequence`** — SOME/IP header + E2E pieces + pinned payload

Wire layout unchanged:

```
[SOME/IP header] [e2e_header] [app_payload] [e2e_footer]
```

### Routing

`routing_manager_impl::send(message)` builds a scatter sequence from
`message` + `payload` shared_ptrs, applies E2E without flattening, and passes
the sequence to endpoints. Service Discovery is an exception: SD messages store
entries/options (not a payload), so that path still serializes via
`serializer` into one owned buffer.

Legacy `send(const byte_t*, size)` (routing stub, SD unicast via `send_via_sd`,
serialized forwards) still copies once at sequence construction until those
callers are migrated — except the **local-proxy → RM → remote** path, which
now pins the IPC frame and scatters SOME/IP header + payload into the remote
send sequence (E2E still allocates a writable 16-byte header for length).

Local UDS/TCP `send_local` builds scatter `[IPC meta | SOME/IP header | payload]`
(wire bytes unchanged). Routing-client `send(message)` skips full-frame
serializer flatten for non-SD messages and always pins `shared_ptr<payload>`
(app layer already snapshotted if needed).

## Implementation checklist

- [x] Ordered `send_segment` list in `send_buffer_sequence`
- [x] `append_payload(shared_ptr<payload>)`
- [x] `protect_result` span + P01 owned buffer
- [x] Profile protectors: span out, no app copy (except P01)
- [x] `compose_e2e_*` pins payload
- [x] `routing_manager_impl::send(message)` zero-copy path
- [x] `notify` / `notify_one` pass payload shared_ptr through
- [x] Local `send_local` IPC scatter (meta + header + payload)
- [x] Routing-client `send(message)` pin path (non-SD)
- [x] RM stub pins IPC frame; remote send uses header/payload slices
- [x] Exclusive pin / shared snapshot at `send`/`notify` API boundary (RM always pins)
- [x] Optional `send_completion_handler_t` on `send`/`notify`/`notify_one`
- [ ] Receive path (check → strip → deserialize) for app delivery
- [ ] Avoid stub `send_command` message\_ copy when parsing SEND meta only

## Related docs

- [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) — scatter-gather send
  and E2E API
- [`receive-copy-elision.md`](receive-copy-elision.md) — receive-side strip /
  deserialize / notify copy elision
