# Receive-path copy elision

> **Status:** Planned on this fork. Goal: pin the receive buffer (or one owned
> buffer) with `shared_ptr` and hand spans / shared payloads down to the app,
> instead of re-copying at strip, deserialize, and notify. Mirror of
> [`send-copy-elision.md`](send-copy-elision.md).

## Problem

A remote message that reaches the hosting application today is typically
already contiguous in an endpoint receive buffer (UDP / TCP / local). E2E
`check()` already returns **non-owning spans** into that buffer. Then routing
throws the win away:

1. **`strip_e2e_protected_payload`** — allocates a new vector; copies SOME/IP
   header + hole-free app payload (drops E2E meta)
2. **`deliver_message` → `deserializer::set_data`** — copies the full frame
   into the pooled deserializer
3. **`payload_impl::deserialize`** — copies the payload portion again into
   `shared_ptr<payload>`
4. **`deliver_notification`** — _also_ does `create_payload(...)` for debounce
   / filter before calling `deliver_message`, so events pay an extra payload
   copy on top of (2)+(3)

Non-routing apps add more copies on the proxy hop (`send_local` + client
`its_buffer` + deserialize again). SOME/IP-TP reassembly is an unavoidable
concat when segments arrive separately.

## Copy inventory (today)

| Stage                                   | Copies          | Notes                                     |
| --------------------------------------- | --------------- | ----------------------------------------- |
| Socket → `on_message`                   | **0×**          | Pointer into UDP/TCP/local recv buffer    |
| SOME/IP-TP reassemble                   | **1×** full     | Only when TP segments; keep as-is for now |
| E2E `check()`                           | **0×**          | Spans into recv buffer                    |
| E2E `strip_e2e_protected_payload`       | **1×** full     | Header + app → new `message_buffer_t`     |
| `deserializer::set_data`                | **1×** full     | Entire frame into deserializer `data_`    |
| `payload_->deserialize`                 | **1×** payload  | Into `payload_impl`                       |
| `deliver_notification` `create_payload` | **+1×** payload | Redundant with deliver_message            |
| Proxy `send_local` + client deserialize | **+2–3×**       | Separate process; see open questions      |

### Totals (payload-sized, remote → hosting app)

| Path               | Approx. today | Target                                |
| ------------------ | ------------- | ------------------------------------- |
| Req/resp, no E2E   | ~2×           | **0–1×** (pin recv buffer)            |
| Req/resp, with E2E | ~3×           | **0–1×** (spans; no strip concat)     |
| Event, with E2E    | ~4×           | **0–1×** (same + drop notify extract) |

Kernel → userspace recv is the only unavoidable copy unless the OS gives a
zero-copy receive API. Everything after that is middleware tax.

## Target design

### Lifetime (same idea as send)

Today `_data` in `on_message` is a bare pointer into the endpoint buffer.
That only works because routing finishes _before_ the next `receive_cbk`
reuses / shifts that buffer. Once we stop copying into owned vectors, something
must keep the bytes alive until:

- the app message handler returns (or finishes async work that still holds the
  `shared_ptr<message>` / `shared_ptr<payload>`), and
- any event cache that retained the payload still holds a ref.

**Approach:** treat the receive buffer like send treats `shared_ptr<payload>` —
pin it.

Concrete options (pick during implementation):

1. **Endpoint already owns `shared_ptr<message_buffer_t>` (UDP does).** Pass
   that shared_ptr into routing with an offset/length for the current message
   instead of `const byte_t*`. TCP / local stream buffers may need the same
   ownership model (shared buffer + slice, or copy-once into owned buffer only
   when the stream buffer must be compacted).
2. **Promote strip output to “the” owned buffer** only when the endpoint cannot
   pin (legacy TCP stream). Prefer (1) so E2E never needs a concat strip.

Spans in `check_result` stay valid as long as the pinned buffer’s `shared_ptr`
is held by the delivered `message` / `payload`.

### E2E: replace strip with spans

`check_result` already has:

```
e2e_header | app_payload | e2e_footer   // spans into recv buffer
```

**Delete the concat in `strip_e2e_protected_payload`.** Instead:

- Keep the SOME/IP header view (first 16 bytes of the pinned buffer).
- Update length in a small **owned** 16-byte header buffer _or_ mutate a
  writable copy of only those 16 bytes (not the whole payload).
- Build delivery from `{ owned_or_viewed SOME/IP header, span app_payload }`
  with E2E header/footer dropped — no `insert` of app bytes.

Wire to the peer is unchanged. The application still sees hole-free payload
bytes; they just live as a span / pinned slice of the receive buffer rather
than a freshly allocated vector.

Profile 01: check already surfaces in-band CRC/counter/nibble as `e2e_header`
and the rest as `app_payload` — same strip-by-span rules apply.

### Deliver without full-frame deserialize

`deliver_message` today:

```
set_data(_data, _size) → deserialize_message() → payload_->deserialize()
```

Target:

- Parse SOME/IP header fields from the pinned buffer (or from the small owned
  header) without copying the payload.
- Attach `shared_ptr<payload>` that either:
  - **views** a slice of the pinned receive buffer, or
  - is a `payload_impl` constructed by **moving** / sharing that slice’s
    ownership (prefer shared backing store over `assign`).

App API stays `shared_ptr<message>` / `get_payload()` — handlers that only
read `get_data()` keep working. Document that mutating payload bytes after
delivery may race with other holders of the same pin (same class of rule as
send-side “do not mutate after send”).

### Notifications: drop redundant `create_payload` (low-hanging fruit)

```cpp
// today in deliver_notification
auto its_payload = runtime::get()->create_payload(&_data[VSOMEIP_PAYLOAD_POS], its_length);
auto its_subscribers = its_event->update_and_get_filtered_subscribers(its_payload, ...);
...
deliver_message(_data, _length, ...);  // copies payload again
```

After pin + span delivery:

- Build **one** `shared_ptr<payload>` from the pinned slice.
- Pass that into debounce / filter **and** into local delivery (or into
  `event::update_*` then deliver the same shared_ptr).
- Do not call `create_payload` from raw `_data` and then deserialize the same
  bytes again.

This alone removes ~1× payload on the event path even before full strip
elision.

### Proxy path

Non-routing apps:

```
client:  send(message) → [IPC meta | SOME/IP hdr | pinned payload] scatter on UDS
RM stub: pin IPC frame; parse SEND meta; pass pin into on_message
RM send: remote sequence = header slice + payload slice (or E2E: owned 16B hdr + pin payload)
```

Open: parse SEND meta without `send_command::deserialize` copying `message_`.
App delivery (deserializer / notify) still copies — see checklist below.

## End-state diagram

```
Endpoint recv buffer (shared_ptr)
  → on_message(pin, offset, size)
  → check() → check_result spans (0 copy)
  → no strip concat; length fix on 16-byte header only
  → deliver: header parse + shared_ptr<payload> viewing pin
  → application handler (holds message/payload refs)
```

## Implementation checklist

- [ ] Pin receive buffer through routing (`shared_ptr` + slice, or equivalent)
- [ ] Replace `strip_e2e_protected_payload` with span-based delivery (no payload concat)
- [ ] `deliver_message`: header-only parse; payload pins recv slice
- [ ] `deliver_notification`: one shared payload for filter + delivery (remove extra `create_payload`)
- [ ] Unit / network tests: E2E check + strip-by-span; hole-free app payload
- [ ] Docs: update [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) receive notes
- [ ] _(open)_ Proxy / `send_local` + routing-client deserialize elision
- [ ] _(open)_ TCP/local stream pin when buffer compaction would invalidate slices
- [ ] _(later)_ SOME/IP-TP reassembly without full flatten (scatter-gather receive)

## Related docs

- [`send-copy-elision.md`](send-copy-elision.md) — send-side zero-copy / pin model
- [`e2e-scatter-gather-send.md`](e2e-scatter-gather-send.md) — E2E protect/check APIs
  and `check_result` spans
