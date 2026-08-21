# TODO — gøre CP/M-86-clib'en komplet

Status-øjebliksbillede: `lib286/cpm86/clibm.lib` = **229 offentlige symboler**,
bygget grønt i alle modeller (s/m/c). Math ligger separat i `libmm.lib`
(transcendentaler pr. model). ctype er tabel-/makro-drevet (`alphabet.obj`,
`istable.obj`) — `isalpha` m.fl. er makroer, ikke selvstændige symboler, så de
tæller som til stede.

Baggrund: clib'en er vokset **on-demand** (build-lib.sh arkiverer kun de moduler
et testet program har refereret). Intet er slettet — hullerne nedenfor er ting
der aldrig har været i scope endnu. Zip er det første program der rammer dem.
Se `README.md` for lag-modellen (Lag 1 genbrugt Watcom-kilde · Lag 2 vores
BDOS-søm · Lag 3 BDOS).

Mål med denne TODO: løfte subset'et til en **komplet C89/C99-clib**, så nye
programmer ikke længere udløser en undefined-symbol-jagt.

## A. Portable huller — tilføj Watcom-kildemodul til `build-lib.sh` (trivielt)

Ren OS-agnostisk Watcom-kilde findes allerede i træet; det er ét linje-tillæg til
modullisten + genbyg. Ingen ny søm-kode.

- [ ] **string.h:** `strncat`, `strstr`, `strspn`, `strcspn`, `strpbrk`, `memchr`,
      `strdup`, `strlwr`, `strupr`, `stricmp`, `strnicmp`
- [ ] **stdlib.h:** `atol`, `abs`, `labs`, `bsearch`, `rand`, `srand`, `atexit`
- [ ] **stdio.h:** `snprintf`, `fileno`
- [ ] **time.h:** `asctime`, `ctime`, `difftime`, `clock`

## B. OS-specifikke — kræver CP/M-86-søm eller bevidst stub (Lag 2)

Disse har ingen direkte stock-Watcom-CP/M-implementering; de skal enten
implementeres mod BDOS eller stubbes med defineret semantik.

- [ ] **`mktemp`** — temp-filnavn; kan implementeres rent i C (PID-fri variant)
- [ ] **`getch`/`kbhit`** — rå konsol (BDOS 6/direkte console I/O)
- [ ] **`lstat`/`stat`** — FCB/BDOS-baseret filstat (ingen symlinks på CP/M)
- [ ] **`fdopen`/POSIX fd-lag** — WONTFIX-kandidat (FILE\*/FCB er den rigtige vej;
      samme beslutning som unzip). Dokumentér som stub.
- [ ] **`system`, `spawnlp`, `getpid`** — ingen processer/shell på CP/M-86 →
      dokumenterede stubs (fejl-retur / konstant PID)

## C. Ingen handling (allerede dækket)

- ctype-prædikater (`isalpha`, `isalnum`, `isupper`, …) — tabel/makro, til stede
- math (`sin`, `cos`, `sqrt`, `pow`, `fabs`, …) — i separat `libmm.lib`/`libmc.lib`

## Arbejdsgang

1. Tilføj bunke A-moduler til `build-lib.sh` modullisten (én ad gangen, verificér
   symbol dukker op i `wlib clibm.lib`).
2. Implementér/stub bunke B i Lag 2-kilder (`portmisc.c`, ny `cpmstat.c` m.m.).
3. Genbyg alle modeller: `./run-all-models.sh` (eller `MODEL=m ./build-lib.sh`).
4. Regressionsgate: MEDTEST.CMD + stdcbench under emu2 (nu med fixups) + MAME rc759.

Zip-kritiske delmængder er markeret i `infozip-cpm86-builds/ZIP_CPM86_PLAN.md`.
