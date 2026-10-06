# Development

> [Docs](README.md) › Development

How the repository is laid out, how to build and test it, and what CI checks.

## Repository Layout

| Path                                   | What is there                                                     |
|----------------------------------------|-------------------------------------------------------------------|
| [`include/hudp.h`](../include/hudp.h)  | the public API of the library                                     |
| [`src/`](../src)                       | the library ([`hudp.c`](../src/hudp.c)) and the wire format ([`protocol.h`](../src/protocol.h)) |
| [`examples/`](../examples)             | [`client.c`](../examples/client.c) at 250 Hz and [`server.c`](../examples/server.c) |
| [`tools/`](../tools)                   | the [CGNAT emulator](emulator.md): library and `natemu` command   |
| [`tests/`](../tests)                   | C tests, smoke tests and the documentation checks                 |
| [`bench/`](../bench)                   | the [benchmark](benchmark.md)                                     |
| [`docs/`](.)                           | this documentation and the images of the README                   |

`make` puts its output in `obj/`, `lib/` and `bin/`, which git ignores.

## Make Targets

| Command       | What it does                                                                 |
|---------------|------------------------------------------------------------------------------|
| `make`        | builds `lib/libhudp.a`, `bin/client`, `bin/server` and `bin/natemu`          |
| `make test`   | builds and runs every test below                                             |
| `make bench`  | runs the [benchmark](benchmark.md), about 80 s                               |
| `make clean`  | removes `obj/`, `lib/` and `bin/`                                            |

`CC` picks the compiler and `EXTRA_CFLAGS` adds flags, for example `make test CC=clang EXTRA_CFLAGS=-Werror`.

## Tests

`make test` runs, in order:

| Test                                                    | What it checks                                                                 |
|---------------------------------------------------------|--------------------------------------------------------------------------------|
| [`tests/test_hudp.c`](../tests/test_hudp.c)             | the library over localhost: handshake, data both ways, keep-alives and their answer, a foreign `CONN_REQ`, migration to a new port, reconnecting after a loss, a new client after a loss, `hudp_send()` errors |
| [`tests/test_nat.c`](../tests/test_nat.c)               | the same library behind the [emulator](emulator.md): mapping timeouts, port changes, an outage, a lossy and jittery link |
| [`tests/smoke.sh`](../tests/smoke.sh)                   | `bin/server` and `bin/client` connect and keep running                         |
| [`tests/smoke_nat.sh`](../tests/smoke_nat.sh)           | the same through `bin/natemu`, with a port change every 700 ms                 |
| [`tests/doc_examples.sh`](../tests/doc_examples.sh)     | every complete C program in the README and in `docs/` compiles with `-Werror`  |

[`tests/check_links.py`](../tests/check_links.py) checks the documentation itself: every relative link in the README and in `docs/` must lead to an existing file, and every `#anchor` to an existing heading. Run it with `make docs-check`; it needs Python 3.

## Continuous Integration

[GitHub Actions](../.github/workflows/ci.yml) runs on every push to `master` and on every pull request:

| Job                           | What it runs                                                          |
|-------------------------------|-----------------------------------------------------------------------|
| Build and test (gcc, clang)   | `make test` with `-Werror`, once per compiler                         |
| Sanitizers                    | `make test` under AddressSanitizer and UndefinedBehaviorSanitizer     |
| Documentation                 | `make docs-check`                                                     |
| Benchmark                     | `bin/bench`, with the table in the run summary                        |

## Windows

The code needs POSIX sockets, which the **MSYS** environment of [MSYS2](https://www.msys2.org/) provides; the MinGW environments do not. In the MSYS shell:

```sh
pacman -S gcc make
make test
```

Binaries come out as `bin/client.exe` and so on, and the scripts find them under their usual names. When `bin/server` starts listening, Windows may ask whether to allow it through the firewall; the tests only use localhost and work either way.
