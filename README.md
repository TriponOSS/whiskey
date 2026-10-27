# Whiskey

Cheap, lightweight HTTP API server written **in Tripon** (TriponOSS/tripon).
The reference consumer of the language: everything Whiskey needs, Tripon's
stdlib + tier system must deliver, exactly once.

## Design brief (v0, locked decisions)
- Written in Tripon .tp, compiled by ralt. No C server code, ever.
- Binary: single static-binary goal, < 300KB target; idle RAM < 8MB target.
- Tier: core-tier event loop first (T1: explicit epoll + arena per request,
  zero per-request malloc/free), async/await (T3) comes when coroutines land.
- Handlers are panic-free: result<T,E> + `?` only. Traps in a request must
  never take the process down for other connections (run trap-permissive mode).
- Routing: routes registered in source, dispatch table generated at compile
  time - no runtime route parsing.
- JSON: stdlib json module (slices-based, streaming later). No reflection.
- Transport: HTTP/1.1 v0 (keep-alive), HTTP/2 later behind a build flag.

## Why these numbers
"most best" means measured: every release gets binary size, idle RSS,
req/s, p99 latency on one fixed benchmark box. Bench harness lives in
bench/ from day one, same discipline as ralt.

## Status
Server code starts when Tripon lands slices/str + net externs (see
Tripon PLAN.md stdlib roadmap). This repo currently holds the design
brief and, later, the bench harness written in a language that already
works, so the numbers are honest from the start.
