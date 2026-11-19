# CP/M-86 C runtime for `owcc -bcpm86`

This directory holds the **source** for the two CP/M-86 runtime artifacts that
Open Watcom's one-command `owcc -bcpm86` build depends on, so they can be
rebuilt **entirely from git** after any `clean.sh`.

## Why this exists

The wlink `system begin cpm86` block (committed in `bld/wl/lnk/specs.sp`)
references:

- `libpath '%WATCOM%/lib286/cpm86'` — where it auto-fetches the model clib
  (`clibs.lib` for small, `clibm.lib` for medium; the object's
  `CMT_DEFAULT_LIBRARY` record drives the model-correct auto-fetch), and
- `format cpm86` — whose implicit start symbol `_cstart_` pulls the C startup
  as a **library member** of that same clib (its entry sits in the front-sorted
  `BEGTEXT` segment, so it still lands at code-group offset 0). This mirrors
  Open Watcom's 16-bit `system dos` target, whose block force-includes NO
  startup via `libfile` — the startup is auto-selected from the model clib.

Both live under `open-watcom-v2/lib286/cpm86/`, which is **build output** and is
git-ignored (`.gitignore` line `/*/`). A full `clean.sh` / `builder -i clean`
therefore deletes them, and — because they were never tracked — there is no
restore point. That is exactly how they went missing once. The fix is to keep
the *source + recipe* in git and regenerate the artifacts on demand.

## Files (in git)

| file            | role                                                        |
|-----------------|-------------------------------------------------------------|
| `cstartcpm.asm` | CP/M-86 startup: entry `_cstart_`, `__STK` stub, BDOS exit  |
| `putchar.c`     | console seam — one char via BDOS C_WRITE (INT 0E0h, CL=2)   |
| `build.sh`      | compiles/archives → installs `lib286/cpm86/{cstartcpm.obj,clibs.lib}`, then self-tests a single-command mandel build against the DR C oracle |
| `env.sh`        | sets `PATH`/`WATCOM`/`WLINK_LNK` for the one-command build   |

`clibs.lib` is assembled from `putchar.c`, the stock Watcom long-math helpers
`bld/clib/cgsupp/a/i4m.asm` + `i4d.asm`, and a real stock clib module
(`bld/clib/string/c/strlen.c`) — all already in git.

## Use

```sh
# once, and after every clean.sh:
sh contrib/ravn/cpm86-clib/build.sh          # -> lib286/cpm86/{cstartcpm.obj,clibs.lib}

# then, one command per program (RC759 = 80186, so -march=i186):
. contrib/ravn/cpm86-clib/env.sh
owcc -bcpm86 -march=i186 -mcmodel=s prog.c -o PROG.CMD
```

`build.sh` ends with a self-test: it links `contrib/ravn/owc-drc/mandel.c` with a
single `owcc -bcpm86` command and checks the emu2 output is **byte-identical** to
the independent Digital Research C build (`owc-drc/MANDEL-DRC.CMD`) — a
cross-compiler correctness oracle.

## Known limitation

`clibs.lib` here is a **seed** CP/M-86 library (console output + long math +
`strlen`): enough for freestanding programs like the mandelbrot. Growing it into
a full CP/M-86 clib (stdio/FILE\*, heap, etc.) is the larger port tracked under
`contrib/ravn/watcom-cpm86-libc/`.

`mandel.c` trips Watcom `wcc` **Internal compiler error 97** at `-O1`/`-O2`/`-Os`
on its ternary-with-string-index line, so the self-test pins `-O0`.
