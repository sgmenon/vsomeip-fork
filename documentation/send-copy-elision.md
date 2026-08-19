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

| Stage | Before | After |
| ----- | ------ | ----- |
| App `set_data(ptr)` | 1× into `shared_ptr<payload>` | unchanged (unavoidable from stack/temp) |
| App `set_data(move(vec))` | 0× | 0× |
| Serialize full frame | 1× | **skipped** on routing-host `send(message)` |
| E2E plugin app copy | 1× | **0×** (span into payload) |
| Routing `send_buffer_sequence(ptr)` | 1× | **0×** when sequence pins payload |
| nPDU train | 0× (refs) | 0× |
| Socket queue | holds `shared_ptr`s until `send_cbk` | same |

**Profile 01 exception:** one pack copy into owned buffer (CRC/counter/nibble
in-band). Header/footer profiles: plugin owns E2E meta only.

## Design

### Lifetime

`send()` returns after queueing. The endpoint holds `send_buffer_sequence` until
`async_send` completes. Pins on `shared_ptr<payload>` (and owned E2E/header
vectors) keep bytes alive — no free callback. Same contract as today’s
`append_bytes` copy, without copying.

### Types

- **`protect_result::app_payload`** — non-owning `span` into caller/pinned payload
- **`protect_result::owned_app_payload`** — Profile 01 packed buffer only
- **`send_buffer_sequence::append_payload`** — ordered segment; no byte copy
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
callers are migrated.

Local UDS/TCP routing (`send_local`) still flattens once for the IPC command
format — separate follow-up.

## Implementation checklist

- [x] Ordered `send_segment` list in `send_buffer_sequence`
- [x] `append_payload(shared_ptr<payload>)`
- [x] `protect_result` span + P01 owned buffer
- [x] Profile protectors: span out, no app copy (except P01)
- [x] `compose_e2e_*` pins payload
- [x] `routing_manager_impl::send(message)` zero-copy path
- [x] `notify` / `notify_one` pass payload shared_ptr through
- [ ] Local `send_local` IPC copy elision
- [ ] Routing-client IPC path (non-routing apps)

## Related docs

- [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) — scatter-gather send
  and E2E API
- [`receive-copy-elision.md`](receive-copy-elision.md) — receive-side strip /
  deserialize / notify copy elision
