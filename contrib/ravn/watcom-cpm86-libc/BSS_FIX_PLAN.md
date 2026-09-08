# BSS Zero-Fill Fix Plan — ravn/open-watcom-v2-ccpm86 #47

## Rodårsag (bekræftet)

CP/M-86-loaderen allokerer G-Min paragraffer men skriver kun G-Length paragraffer
fra filen. Differencen (G-Min − G-Length) er BSS — uinitialiserede statics.
`crt0sm.asm` har ingen zero-fill-løkke, så BSS starter som skrald på rigtig hardware.
emu2 skjulte dette fordi `memory[]` er et globalt C-array (nul-initialiseret af C).
Med `-P 255` er fejlen nu reproducerbar på emu2.

## Implementering

### Trin 1 — BEGBSS/ENDBSS markersegmenter

Tilføj to tomme segmenter med class `'BSS'` i hvert crt0-fil:

```asm
BEGBSS  segment word public 'BSS'
        public  __bss_start
__bss_start label byte
BEGBSS  ends

ENDBSS  segment word public 'BSS'
        public  __bss_end
__bss_end label byte
ENDBSS  ends
```

`option dosseg` placerer class-`BSS`-segmenter efter `CONST`/`CONST2` og før
`STACK`. Da crt0.obj er det første objekt der processeres, ankommer BEGBSS før
_BSS fra brugerens objektfiler → korrekt rækkefølge garanteret.

### Trin 2 — Opdater DGROUP-deklaration

`crt0sm.asm` (og cm/mm/lm analogt):
```
; FØR:
DGROUP  group   BEGDATA, _DATA, STACK
; EFTER:
DGROUP  group   BEGDATA, _DATA, BEGBSS, _BSS, ENDBSS, STACK
```

`crt0cpp.asm`:
```
; FØR:
DGROUP  group   BEGDATA, _DATA, XIB, XI, XIE, YIB, YI, YIE, STACK
; EFTER:
DGROUP  group   BEGDATA, _DATA, XIB, XI, XIE, YIB, YI, YIE, BEGBSS, _BSS, ENDBSS, STACK
```

### Trin 3 — Zero-fill-løkke i _cstart_

Indsæt FØR `call wc_heap_init_` (ingen libkode må afhænge af nul-statics
inden dette punkt):

```asm
; Zero-fill BSS: CP/M-86 loader does not clear the G-Min - G-Length gap.
        cld
        mov     di, offset DGROUP:__bss_start
        mov     cx, offset DGROUP:__bss_end
        sub     cx, di
        jz      bss_done
        xor     al, al
        rep     stosb
bss_done:
```

Samme kodeblok tilføjes i alle fem crt0-filer (sm/cm/mm/lm/cpp).

### Trin 4 — Verificering med bssprobe under emu2 -P 255

`bssprobe.c` tæller ikke-nul bytes i et 16 KB uinitialiseret array:
- FØR fix: ~16128/16384 ikke-nul (arver 0xFF poison)
- EFTER fix: 0/16384 (BSS er nulstillet af crt0)

Byggestep (kræver Docker med Open Watcom):
```sh
# byg bss_assert.cmd
owcc -mcmodel=s -o BSSAS.CMD test/bss_assert.c -I port -L build-sm port/clibs.lib
# kør med poison — forventer PASS (0 failures)
emu2 -P 255 BSSAS.CMD
```

`bss_assert.c` tjekker tre assertions (se fil for detaljer):
```sh
# byg bssprobe.cmd med small model
owcc -mcmodel=s -o BSSPRO.CMD test/bssprobe.c -I port -L build-sm port/clibs.lib
# kør med poison
/Users/ravn/z80/emu2-cpm86/emu2 -P 255 BSSPRO.CMD
# forventet: "0 of 16384 bytes non-zero -- BSS clean"
```

### Trin 5 — Samme fix i alle fire varianter

| Fil | Model | Ændring |
|---|---|---|
| `crt0sm.asm` | Small | Trin 1-3 |
| `crt0cm.asm` | Compact | Trin 1-3 |
| `crt0mm.asm` | Medium | Trin 1-3 |
| `crt0lm.asm` | Large | Trin 1-3 (DGROUP er anderledes — far data) |
| `crt0cpp.asm` | C++ | Trin 1-3 (XI/YI-tabeller bevar) |

### Trin 6 — Opdater KNOWN_ISSUES.md

Markér issue 3c som løst med commit-reference.

## Afgrænsning

- Kun `_BSS`-segmentet nulstilles (near BSS i DGROUP).
- Far BSS (large model far data) er separat problem — out of scope her.
- Ændringen er usynlig for eksisterende fungerende kode (BSS var allerede nul
  på emu2 uden -P 255, og crt0-ændringen gør det eksplicit/garanteret).

## Risiko

Lav. `rep stosb` er deterministic; der er ingen datarace. Den eneste mulige
fejl er forkert BEGBSS/ENDBSS-placering (BSS rækker ind i CONST eller STACK),
som bssprobe-testen vil afsløre.
