# Stage B far-code relocation — CP/M-86 `-mm -zm` test oracles

Regression + acceptance oracles for wlink's CP/M-86 medium-model (`-mm -zm`,
"far code / near data") load-time relocation: a program whose code spans more
than one 64 KB segment. wlink emits CP/M-86 P_LOAD fixup records (header byte
127 bit 7 + `ch_fixrec` + 4-byte records) that the loader applies with
`add es:[fixup], group_base`. See the implementation in `bld/wl/c/loadcpm86.c`
(`cpm86GroupImgPara` etc.) and the format spec in the repo memory
`reference_drc_cpm86_reloc_format.md`.

## Two levels of oracle

### 1. Unicorn (fast, CI-friendly) — `test_stageb_farcall.sh`

Drives the real wcc/wasm/wlink to build forced-split `.CMD`s and runs them under
`cpm86run_unicorn.py`, asserting exact output `OK\r\n`:

- `test_stageb_farcall.c` — **CODE->CODE** far calls (fixups located in CODE,
  record nibble `0x1X`), value oracle via execution.
- `test_stageb_farptr.c`  — **DATA->CODE** far pointers (fixups located in DATA,
  record nibble `0x2X`): follows each relocated pointer and checks the bytes
  there are the stub's own code (`B8 lo hi` = `mov ax,imm`). This memory-check
  technique is the one that scales to very large programs (UnZip).
- `test_stageb_begdata.asm` — base-page reservation so the freestanding tests
  survive the loader zero-filling the first DATA group (genuine CCP/M-86
  `kern/load.sup:477 init_base`, "1st Data Group has Base Page").
- Small-model guard: a single-CODE-segment build must leave header byte 127
  bit 7 clear (no spurious fixup table).

Run: `. cpm86-clib/env.sh && sh test_stageb_farcall.sh` -> `3 passed`.

### 2. MAME rc759 (fully authoritative) — real Concurrent CP/M-86 3.1

The Unicorn runner is a re-implementation; the genuine CCP/M-86 loader in the
MAME rc759 driver is the authoritative oracle. Two builds were booted on the
genuine SW1400 CCP/M-86 3.1 turnkey disk and both printed `OK!` then `....`
(4/4 far calls returned the right char, 4/4 far pointers address the exact
expected relocated code), signalling `DONE-SIGNAL word=0x0008 pass=8 fail=0`:

- `test_stageb_farptr_mame.c` + `test_stageb_crt759.asm` — **wlink's OWN
  output**. This is the decisive check: it exercises the fixup records wlink
  emits, on real hardware. `crt759.asm` is a minimal CP/M-86 startup distilled
  from `kern/load.sup` (base-page reservation; entry at CS:0; the loader hands a
  small stack via `u_initss=lod_lstk`/`ls_sp` plus a RETF frame to `user_retf`,
  so we switch to our own roomier stack, exactly as DR C's CLEARL crt0 does).
- `test_stageb_farptr_drc.c` + `test_stageb_done_far.asm` — the **Digital
  Research C 1.11 reference** ("how it SHOULD be done"). DR C's default LARGE
  model is genuinely far-code/far-data; LINK-86 emits the same CP/M-86 fixup
  record format, and the loader relocates it. Confirms the oracle methodology is
  sound against an independent, period-correct compiler.

Reproduce (needs the mame/ submodule's `regnecentralend` + the turnkey disk;
outside this submodule, in the superproject):

```sh
# wlink-native build:
. open-watcom-v2/contrib/ravn/cpm86-clib/env.sh
cd open-watcom-v2/contrib/ravn
wasm -bt=dos test_stageb_crt759.asm -fo=crt759.obj
wcc  -bt=dos -mm -zm -s test_stageb_farptr_mame.c -fo=fpm.obj
wlink format cpm86 option packcode=8 option start=start_ option undefsok \
      name FPTRMAME.CMD file crt759.obj file fpm.obj
# boot it headless; expect: DONE-SIGNAL word=0x0008 pass=8 fail=0
sh <workspace>/scratch/rc759-cmd-toolchain/mame-tests/run-mame-prebuilt.sh \
      "$PWD/FPTRMAME.CMD"

# DR C reference build:
DRC_PUTCHAR=1 DRC_MAMEMARK=1 \
  MAMEMARK_ASM=$PWD/test_stageb_done_far.asm \
  bash <workspace>/scratch/rc759-cmd-toolchain/drc-oracle.sh \
       test_stageb_farptr_drc.c DRCFPTR.CMD
sh <workspace>/scratch/rc759-cmd-toolchain/mame-tests/run-mame-prebuilt.sh \
       "$PWD/DRCFPTR.CMD"
```

## Why the paragraph came from the packed image, not `grp_addr.seg`

The pointer oracle first caught a real linker bug: a far reference's
group-relative paragraph MUST be the running sum of `CMD_PARAS(CalcGroupSize())`
over preceding non-empty same-class groups (`cpm86GroupImgPara`), because the
`.CMD` image is paragraph-packed but wlink's frame numbers increment by 1 per
segment regardless of size. For a 4-target test the correct image paragraphs
were `{5,6,7,9}` vs the buggy frame deltas `{1,2,3,4}`. Full account:
repo memory `reference_stageb_farcode_reloc_verified.md`.
