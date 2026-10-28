# whiskey

A backend API library for [Tripon](https://github.com/TriponOSS/tripon) (.tp).

HTTP/1.1 server plumbing: TCP listener, request parsing, routes with
`:param` capture, static file serving, text/JSON responders, sequential
or fork-per-request serving.

Pointers-per-T7 live only inside one C99 island, `glue.c` (~120 lines): listen,
accept, request read-loop, send, static files. Everything above
it - parsing, routing, escaping, response building - is pure Tripon:
null-free, no UB, bounds-checked.

---

## Install

consumers need two things:

1. whiskey as a dep. `ralt.yaml`:

   ```yaml
   name: myapp
   version: 0.1.0
   deps:
     whiskey: /path/to/whiskey    # or clone into ./vendor/whiskey
   ```

2. `glue.c` linked in (ralt emits C99; glue is the pointer island). Easiest
   via `whiskey-build.sh`, or by hand:

   ```sh
   ralt build main.tp              # ok if it fails on glue symbols
   cc -O1 build/main.c /path/to/whiskey/glue.c -o myapp
   ```

or let `whiskey-build.sh` do both (needs `RALT` at the tripon checkout):

```sh
RALT=../Tripon/build/ralt /path/to/whiskey/whiskey-build.sh main.tp
./build/main-app
```

### Demo

```sh
git clone https://github.com/TriponOSS/whiskey
cd whiskey
./serve.sh                     # builds + runs on 0.0.0.0:8080
```

demo routes (serving root = the CWD where whiskey-demo runs):

| route | result |
|---|---|
| `GET /health` | 200 text |
| `GET /json` | 200 JSON (escaped payload) |
| `GET /users/:id` | 200 text (bad id: 400) |
| `GET /static/index.html` | 200 file |
| `GET /static/../<junk>` | 403/404, traversal blocked |
| `POST /echo` | body mirrored back |
| `POST /nope` | 405 Method Not Allowed |
| unknown | 404 |

---

## Writing a server

```tp
use tier core;

fn send_text(cfd: i32, status: i32, body: str) -> i32 { /* collapsed result */ }

fn dispatch(cfd: i32, req: Request) -> i32 {
    if whiskey_method_is(req, whiskey_get()) {
        let m = whiskey_route(req, "/users/:id");
        match m {
            some(m) => {
                let id = whiskey_param_i32(req, m, 0);
                match id {
                    some(v) => send_text(cfd, 200, "user\n"),
                    none    => send_text(cfd, 400, "bad id\n"),
                }
            }
            none    => send_text(cfd, 404, "not found\n"),
        }
    } else {
        send_text(cfd, 405, "method not allowed\n");
    }
    return 0;
}
```

every result returns `result<_, str>`: E0205 refuses ignored listen/accept/parse
failures by construction, no swallowed bugs.

## API summary

**net.tp**
- `whiskey_listen(port: i32) -> result<i32, str>` - open a 0.0.0.0 listener
- `whiskey_accept(lfd) -> result<i32, str>` - accept one conn
- `whiskey_close(fd) -> result<i32, str>`
- `whiskey_nozombies() -> i32` - call once before a fork loop
- `whiskey_is_child(pid: i32) -> bool` - the fork seam

**io.tp**
- `struct Buf { n: i32; data: [8192]i8 }` - THE wire channel: ralt v0 does
  not have address-of outside extern fences, so value-data crosses fn
  boundaries by copy in this struct
- `whiskey_recv(fd) -> result<Buf, str>` - full request read (headers
  complete + content-length satisfied)
- `whiskey_send_buf(fd, Buf) -> result<i32, str>`
- `buf_new() / buf_put_str / buf_put_byte / buf_put_i32` - builders

**http.tp**
- `whiskey_parse(Buf) -> result<Request, str>` - request line + path span +
  content-length body span (max body 6000 bytes, errors otherwise)
- `struct Request` - `method: i32`, path at `data[pstart..pend)`, body at
  `data[bstart..bstart+bn)`

**route.tp**
- `whiskey_route(req, pattern: str) -> opt<Match>` - exact-shape match,
  up to 3 `:param` captures, query `?` terminates
- `whiskey_param_i32(req, m, slot) -> opt<i32>` - the digits case
- `whiskey_method_is(req, m: i32) -> bool`, plus `whiskey_get() /
  whiskey_post() / whiskey_put() / whiskey_delete() / whiskey_other()` method
  ids (v0 has no top-level consts, so these are niladic fns the compiler inlines)

**resp.tp**
- `whiskey_text(fd, status, body_literal) -> result<i32, str>`
- `whiskey_json(fd, status, payload_literal) -> result<i32, str>` -
  full JSON escaping (quotes, backslash, control bytes) on the payload
- `whiskey_status_msg(i32) -> str` - http reason phrase

**static.tp**
- `whiskey_static(fd, req) -> result<i32, str>` - serve req path under the
  server's CWD (glue guards traversal, absolute paths, directories)

## Constraints (Tripon v0 reality, checked today)

| wall | reality | whiskey's answer |
|---|---|---|
| no pointers outside extern | T7 | glue.c is the fence; lib never sees a `*` |
| no variadic externs | ralt segfault | fixed-arity calls only |
| arrays can't cross fn boundaries | pass-by-value only | Buf struct carries them |
| no function pointers | - | match-based dispatch, caller owns the arm |
| no top-level `const` yet | - | niladic fns |
| str indexing yields u8 | - | cast<i8> at the compare sites |

## Design notes

- `Buf` is 8192 bytes on stack per call. Request parse produces a ~8KB
  Request by value. v0: correctness over bytes-per-request. P4's native
  backend + slices change that wholesale.
- fork-per-request keeps handlers crash Isolated: a broken handler can't
  kill the acceptor. Zombies are kernel-reaped (SIGCHLD/SIG_IGN).
- serving root = the process CWD: bind-mount / chroot is downstream config
  (documented, not hard-coded: deployment choice, not ours).

## Status

- v0.1: all surface, demo verified serving `gcc` + `clang`.
- PLAN.md is scratch (uncommitted), actual verification log in git history.

## License

MIT - [LICENSE](LICENSE).
