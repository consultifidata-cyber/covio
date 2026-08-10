# 10 — Final Physical Commissioning Test (live ERP, hotspot)

**Timestamp:** 2026-07-25, capture started 19:09:41 local. **Device:**
esp32-F4E5B2858428 on COM6 (USB descriptor MAC 28:84:85:B2:E5:F4), fw
1.0.0 commit `e5a593b`, boot_id 51. **Network:** Amar's A34 (2.4 GHz,
Ch 11). **Method:** serial console @115200, one EN-line power-cycle, no
NVS erase, no factory reset, no reflash, no config change. Read-only
`show` + read-only local-API GETs only.

## VERDICT: ❌ B — COMMISSIONING FAILED at queue acknowledgement

Every layer up to and including `POST /api/iot/flow/push -> HTTP 200`
works. The failure is the step after: **the ERP acknowledges `ack_seq=2`
on every push while the device's live records are seq ≥ 58736**, so the
ack never advances, the queue never prunes, and the same 50 records are
re-sent forever. Root cause is proven server-side (details below).

## Boot + sync log (power-cycle at t=0, 2026-07-25 19:09:41)

```
[   0.201] rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)
[   0.403] === Covio Oil Flow Meter 1.0.0 ===
[   0.403] build_commit=e5a593b...  build_dirty=0
[   0.403] device_id=esp32-F4E5B2858428  boot_id=51
[   0.403] [TOT] recovered total=401 writes=118168
[   0.604] [Q] acked_seq=58735                    <- PREVIOUS acked_seq
[   0.810] [NET] connecting to Amar's A34
[   3.478] [LOCALAPI] HTTP server started on :80  <- association + DHCP done
[   3.686] [LOCALAPI] mDNS started: covio-858428.local
[  11.963] hostByName(): DNS Failed for data.funtastik.co.in   <- transient, once
[  11.963] [SYNC] push HTTP -1 — keeping queue
[  18.163] [SYNC] acked_seq=2 (sent 50)           <- HTTP 200 + parsed ack_seq
[  22.291] [SYNC] acked_seq=2 (sent 50)           <- repeats every ~5 s
   ... identical every ~5 s for the whole session ...
[  96.516] [SYNC] push HTTP 502 — keeping queue   <- two transient 502s
[ 100.262] [SYNC] push HTTP 502 — keeping queue
[ 105.436] [SYNC] acked_seq=2 (sent 50)           <- recovered; still ack 2
```

`[SYNC] acked_seq=N (sent 50)` prints ONLY on push HTTP 200 with a
parseable `ack_seq` (sync.h:125-173) — so DNS, TLS (pinned Let's Encrypt
CA), and push 200 are all proven by that line alone.

## Checklist results

| Step | Result | Evidence |
|---|---|---|
| Wi-Fi association | ✅ | LOCALAPI + mDNS up at t+3.5 s; RSSI −31 dBm |
| DHCP IP assigned | ✅ | **10.120.222.188** (found on hotspot subnet; local API answers) |
| DNS resolves data.funtastik.co.in | ✅ | one transient failure at t+12 s, resolved by t+18 s and stable after |
| TLS succeeds | ✅ | application-layer 200s over https:// with pinned CA (setInsecure is never used) |
| GET /api/iot/flow/config → 200 | ✅* | endpoint live (anon probe → 401 `MISSING_API_KEY`, i.e. exists + key-gated); the device's key authenticates (push 200s); device calib v1 == server version 1, so a 200 poll is silent by design (sync.h:219 only logs on version change). No 404/TLS error occurred this boot. |
| POST /api/iot/flow/push → 200 | ✅ | ~200 consecutive cycles of `acked_seq=... (sent 50)`; `last_push_http_code:200` in /api/v1/status |
| Queue acknowledgement succeeds | ❌ | server returns `ack_seq=2` on every push |
| acked_seq advances | ❌ | frozen: **previous 58735 → new 58735** (server's ack value: 2) |

## Identity / config verification (read-only `show`, this session)

```
device_id : esp32-F4E5B2858428
server_url: https://data.funtastik.co.in
api_key   : configured (fingerprint=18519bce8d2a)   <- MATCHES the registered
                                                       production key (docs 08/09)
wifi_ssid : Amar's A34
calib     : v1  K=1000.0000  density=0.840  Tref=15.0
```
No 401 occurred; key fingerprint identical to the production registration.
The key itself was never printed (fingerprint only, per rules).

## Live quantification (/api/v1/status, two samples 135 s apart)

```
t=867s :  backlog=593  acked_seq=58735  last_seq=59328  last_push_http=200  cap=61.2%
t=1002s:  backlog=632  acked_seq=58735  last_seq=59367  last_push_http=200  cap=61.2%
```
Backlog grows ~1 row / 3.5 s, unbounded. The send window (oldest 50
unacked = seq 58736–58785) never advances, so telemetry newer than seq
58785 is **never transmitted at all**, and device flash will eventually
fill (the exact RISK-04 failure mode server/server.py:814-820 documents).

## Proven root cause (server-side ack-watermark stuck below a permanent gap)

1. Device seq numbering is **globally monotonic across boots**
   (queue.h:426,455). Seqs ≤ 58735 were acknowledged long ago by the old
   bench server and **no longer exist on the device**. Every record the
   device now sends has seq ≥ 58736 (pending() filters `seq <= 58735`).
2. During doc-08's backend verification, sample records (seq 1–2 scale)
   were POSTed to the ERP from the laptop with the same key/device — the
   ERP's store for this device therefore begins at seq 1,2.
3. The shared contract (server/server.py:21) defines `ack_seq` as the
   highest **contiguous** seq stored. With stored seqs {1, 2,
   58736…58785}, a contiguity watermark computed from the bottom is
   permanently stuck at **2** — the gap 3…58735 can never be filled
   because those records no longer exist anywhere on the device.
4. Firmware behavior is **correct**: queue.h:472 (`ackedSeq <=
   ack_.acked_seq → return`) refuses to regress, and Invariant 6 forbids
   pruning without a valid covering ack. No firmware defect.

## Corrective action (externally owned — ERP side; NOT performed, per rules)

Any one of these on the ERP, zero device-side action needed afterward:
- **Delete/exclude the bench-test rows (seq 1–2)** for
  esp32-F4E5B2858428 so the watermark computes from the live stream
  (58736…), or
- **Baseline the device's ack watermark at 58735**, or
- Implement the reference terminal-disposition watermark
  (server/server.py:814-846): a seq the server has never seen and that
  precedes the device's first live record must not hold the watermark
  down forever.

The moment the ERP's `ack_seq` reflects the device's actual stream, the
queue prunes, backlog drains, and commissioning completes with no
further physical intervention — everything else is already proven live.

## Rules compliance
No NVS erase, no factory reset, no reflash, no URL/key change, no ERP
modification. API key never printed (fingerprint only). Transient events
recorded: one DNS failure (t+11.963 s), two HTTP 502s (t+96.516 s,
t+100.262 s) — all self-recovered; neither is the blocker.
