# BSS Zero-Fill Fix Plan — ravn/open-watcom-v2-ccpm86 #47

## Rodårsag (bekræftet)

CP/M-86-loaderen allokerer G-Min paragraffer men skriver kun G-Length paragraffer
fra filen. Differencen (G-Min − G-Length) er BSS — uinitialiserede statics.
`crt0sm.asm` har ingen zero-fill-løkke, så BSS starter som skrald på rigtig hardware.
emu2 skjulte dette fordi `memory[]` er et globalt C-array (nul-initialiseret af C).
Med `-P 255` er fejlen nu reproducerbar på emu2.

## Implementering

> **Blueprint: Open Watcoms egen cstart gør præcis dette.**
> `bld/clib/startup/a/cstrt086.asm` (linje 64, 172-177, 322, 396-400) er den
> autoritative reference — vi kopierer dens idiom frem for at opfinde nye
> markørsegmenter.
>
> **Nøgleindsigt (verificeret i wlink-kilden):** under `option dosseg` definerer
> wlink SELV grænsesymbolerne for hele `'BSS'`-klassen — vi behøver ikke egne
> markører. `bld/wl/c/loadfile.c:534 GetBSSSize()` finder `'BSS'`-klassen
> (`LocateBSSClass`) og sætter, frame-relativt til gruppen:
> - `_edata` / `__edata` = **start** af BSS-klassen (`RingFirst`) — `specials.h:63,65`
> - `_end`   / `__end`   = **slut**  af BSS-klassen (`RingLast` + size) — `specials.h:64,66`
>
> Grænserne beregnes fra første til sidste segment i klassen, uafhængigt af
> modulrækkefølge. Egne `BEGBSS`/`ENDBSS`-markører ville tværtimod fejle: under
> dosseg ordnes ens-klasse-segmenter i modulrækkefølge, og da crt0.obj
> processeres først, ville BÅDE BEGBSS og ENDBSS lande før brugerens `_BSS` →
> tomt interval, intet nulstillet.

### Trin 1 — Deklarér et tomt `_BSS`-segment + extrn på grænsesymbolerne

I hvert crt0-fil:

```asm
        extrn   _edata : byte           ; wlink/dosseg: start of BSS class
        extrn   _end   : byte           ; wlink/dosseg: end of BSS class
...
_BSS    segment word public 'BSS'
_BSS    ends
```

Den tomme `_BSS`-deklaration er ikke pynt: den garanterer at `'BSS'`-klassen
EKSISTERER, så `_edata`/`_end` altid resolver — også hvis brugerprogrammet har
nul BSS (ellers "undefined symbol"-linkfejl). Dette er netop hvorfor
`cstrt086.asm:172` selv deklarerer et tomt `_BSS`.

### Trin 2 — Opdater DGROUP-deklaration

`crt0sm.asm` (og cm/mm/lm analogt):
```
; FØR:
DGROUP  group   BEGDATA, _DATA, STACK
; EFTER:
DGROUP  group   BEGDATA, _DATA, _BSS, STACK
```

`crt0cpp.asm`:
```
; FØR:
DGROUP  group   BEGDATA, _DATA, XIB, XI, XIE, YIB, YI, YIE, STACK
; EFTER:
DGROUP  group   BEGDATA, _DATA, XIB, XI, XIE, YIB, YI, YIE, _BSS, STACK
```

Compilerens objektfiler putter allerede `_BSS` i DGROUP via deres GRPDEF; at
liste det her sikrer korrekt placering og at det tomme segment tilhører frames.

### Trin 3 — Zero-fill-løkke i _cstart_

Indsæt FØR `call wc_heap_init_` (ingen libkode må afhænge af nul-statics
inden dette punkt). `rep stosb` skriver til `ES:DI`:

```asm
; Zero-fill BSS: CP/M-86 loader allocates G-Min but only writes G-Length from
; the file; the gap [G-Length, G-Min) is uninitialised BSS. _edata/_end are the
; BSS-class bounds wlink emits under `option dosseg` (see loadfile.c:GetBSSSize).
        mov     ax, ds                  ; ES must point at DGROUP for stosb
        mov     es, ax
        cld
        mov     di, offset DGROUP:_edata ; start of _BSS
        mov     cx, offset DGROUP:_end   ; end of _BSS (start of STACK)
        sub     cx, di                   ; # bytes (0 => rep stosb is a no-op)
        xor     al, al
        rep     stosb
```

Samme kodeblok tilføjes i alle fem crt0-filer (sm/cm/mm/lm/cpp). `_end` stopper
FØR `STACK` (anden klasse), så løkken rører ikke stakken.

**ES per model (verificér, sæt eksplicit hvis nødvendigt):** i small model
sætter CP/M-86-loaderen DS=ES=DGROUP (`crt0sm.asm:27`), men `mov es, ax`-linjen
er billig forsikring og PÅKRÆVET i cm/mm/lm hvor ES ikke nødvendigvis peger på
DGROUP. OW's egen cstart sætter også ES=DGROUP eksplicit før løkken
(`cstrt086.asm:320-322`).

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

| Fil | Model | Ændring | ES-note |
|---|---|---|---|
| `crt0sm.asm` | Small | Trin 1-3 | ES=DS=DGROUP fra loader; `mov es,ax` som forsikring |
| `crt0cm.asm` | Compact | Trin 1-3 | **Verificér ES** — sæt eksplicit før løkken |
| `crt0mm.asm` | Medium | Trin 1-3 | **Verificér ES** — sæt eksplicit før løkken |
| `crt0lm.asm` | Large | Trin 1-3 (DGROUP er anderledes — far data) | **Verificér ES** |
| `crt0cpp.asm` | C++ | Trin 1-3 (XI/YI-tabeller bevar) | ES=DS=DGROUP; forsikring |

### Trin 6 — Opdater KNOWN_ISSUES.md

Markér issue 3c som løst med commit-reference.

## Afgrænsning

- Kun `_BSS`-segmentet nulstilles (near BSS i DGROUP).
- Far BSS (large model far data) er separat problem — out of scope her.
- Ændringen er usynlig for eksisterende fungerende kode (BSS var allerede nul
  på emu2 uden -P 255, og crt0-ændringen gør det eksplicit/garanteret).

## Risiko

Lav. `rep stosb` er deterministic; der er ingen datarace. Vi bruger wlinks egne
`_edata`/`_end`-grænser (samme som OW's egen cstart), så BSS-intervallet er
korrekt per konstruktion. De to resterende fejlkilder er (1) ES ikke = DGROUP i
cm/mm/lm — afhjulpet af eksplicit `mov es,ax`; (2) at glemme det tomme
`_BSS`-segment så `_edata`/`_end` ikke resolver. Begge fanges af `bss_assert.c`
(bracket-test) og `bssprobe.c` under `emu2 -P 255`.
