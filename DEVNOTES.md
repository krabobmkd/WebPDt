# webp.datatype - developer notes

Design notes, toolchain pitfalls, and the library Expunge analysis. That
analysis also covers EmojiGear's utf8rastport.library, unibutton.gadget, and
unitexteditor.gadget.

---------------------------------------------------------------------------

## 1. What a picture datatype is

A datatype is an exec library that is also a BOOPSI class library:

- `DEVS:DataTypes/WebP`: an IFF FORM DTYP descriptor. When a file is opened,
  datatypes.library compares its first bytes to the descriptor mask
  (`'R' 'I' 'F' 'F' ANY ANY ANY ANY 'W' 'E' 'B' 'P'`, flags
  `DTF_BINARY|DTF_CASE`, group `pict`, ID `webp`). If they match, it opens
  `SYS:Classes/DataTypes/<basename>.datatype`, with basename `webp`.
- The library's first public vector (-30) is `ObtainEngine()`. It returns
  the `Class *` that datatypes.library passes to `NewObject()`.
- Our class is a subclass of `picture.datatype`. On `OM_NEW`, the superclass
  creates the object, then we decode the file and hand over pixels. The
  V43+ picture.datatype does everything else: remapping and dithering to
  8-bit or lower screens (the "8-bit remapped" case), scaling, friend
  bitmaps, and saving as ILBM (`DTM_WRITE` with `DTWM_IFF`).

Pixel hand-over (`src/webpclass.c`):

1. **V47**: `SetDTAttrs(PDTA_SourceMode=PMODE_V43, DTA_NominalHoriz/Vert,
   PDTA_ObtainPixelBuffer=&pbpa)`. If `pbpa.pbpa_PixelData` comes back
   non-NULL, libwebp decodes straight into picture.datatype's buffer, so
   there is no second full-size copy. The format is mapped as
   RGB->MODE_RGB, ARGB->MODE_ARGB, RGBA->MODE_RGBA. Any other format falls
   back to 2.
2. **V43+**: decode into our own RGB (opaque) or ARGB (alpha) buffer, then
   one `PDTM_WRITEPIXELARRAY`. Direct writes and WRITEPIXELARRAY must never
   be mixed.

Other behaviour:

- Alpha images are depth 32 with `bmh_Masking = mskHasAlpha`; opaque ones
  are depth 24 with `mskNone`.
- Animated WebP: frame 1 is decoded through the demux API at its x/y offset
  on the canvas. Any area it doesn't cover stays transparent, because the
  buffer is cleared.
- Sources: `DTST_FILE` (the whole file is read into memory),
  `DTST_MEMORY` (V44), and `DTST_RAM` with no handle (an empty object).
- `DTM_WRITE` with `DTWM_RAW` is refused, since there's no encoder.
- libwebp output byte order is endian-independent: MODE_ARGB writes A,R,G,B
  in memory, which is exactly PBPAFMT_ARGB.

## 2. Shared binary rules (same scheme as libutf8rastport)

- The binary is an "executable" linked with
  `-nostdlib -ffreestanding -nostartfiles -Wl,-e,__start -T src/amiga_library.ld`.
  The linker script puts the `__libinit` section of `libinit.s` first:
  `moveq #-1,d0; rts`, then the RomTag.
- There is no libnix. `lib_amiga_crt.c` provides the following:
  - malloc, calloc, realloc, and free on AllocVec/FreeVec. exec serialises
    them, and realloc reads the size AllocVec stores at `ptr[-1]`.
  - mem* functions, strlen, qsort, and `__bcopz`, which bebbo gcc emits for
    block copies.
  - The file is compiled with `-fno-builtin`, otherwise gcc turns the loops
    into calls to themselves.
- **Globals**: the only writable ones are the library bases, the class
  pointer, and libwebp's dsp function pointers. All are written once in
  `CLibInit()` (called from LibOpen, under Forbid). libwebp normally fills
  its dsp pointers lazily behind a non-atomic flag when built without
  WEBP_USE_THREAD. Calling the `*Init()` functions up front means several
  decoding tasks only ever read them. All per-picture state is on the
  decoding task's stack or in its AllocVec memory.
- **Stack**: methods run on the caller's stack, which can be 4 KB. If less
  than 48 KB is free (checked against `tc_SPLower`), `RunDecode` runs on an
  AllocVec'd stack through `WebpCallWithStack` in libinit.s. That uses exec
  `StackSwap`, called twice with the same StackSwapStruct.
- **Version**: `WEBPDT_VERSION`/`WEBPDT_REVISION` in CMakeLists.txt are
  generated into `libversion.h` (C) and `libversion.i` (asm, for the RomTag
  and LibInit), so the two can't drift apart. There's also a
  `$VER: webp.datatype 45.1 (d.m.yyyy)` string for C:Version.

## 3. Toolchain pitfalls found (bebbo m68k-amigaos-gcc 6.5)

- **`-Wl,--gc-sections` must not be used.** The hunk ld doesn't follow
  references out of the vasm object, so the whole library gets stripped
  (2.5 KB binary).
- **Soft-float**: libgcc (libm020) has no `__mulsf3`, `__adddf3` and the
  like. libnix's `libc.a` has them, but implements them through
  mathieee*.library bases that only libnix's startup opens. In a
  -nostartfiles binary they are NULL, which crashes. libwebp needs four of
  these helpers (utils/random_utils.c, `VP8InitRandom`), so
  `src/softfloat_min.c` implements them with integer code. They were
  validated against the x86 FPU: 30M random and edge cases (subnormals,
  Inf, NaN), 0 mismatches.
- **dsp/yuv.c** always `#define`s `USE_GAMMA_COMPRESSION`. That enables
  encoder-only gamma tables, which need `pow()` and double soft-float.
  CMake compiles a copy (`build/webp_amiga/yuv_nogamma.c`) with only that
  line removed. `libwebp-main` stays pristine, and configure fails loudly if
  a future libwebp changes that line.
- `WORDS_BIGENDIAN` is defined explicitly (PUBLIC on webpdec). cpu.h would
  derive it from `__BYTE_ORDER__`, but some files test it before including
  cpu.h.
- `struct Hook.h_Entry` is `ULONG (*)()` while `HOOKFUNC` is
  `unsigned long (*)()`, which are different types in this NDK. Cast with
  `(ULONG (*)())`.
- The vasm rule of the platform file has no per-target include path.
  CMakeLists.txt overrides `CMAKE_VASM_COMPILE_OBJECT` locally to add
  `-I<build dir>` (for libversion.i) and to use `-m68020`.

## 4. The descriptor

`tools/mkdtdesc.py` is a host replacement for DTDesc/createdtdesc. It
writes FORM DTYP with FVER, NAME and DTHD chunks. DTHD is a 32-byte
big-endian `DataTypeHeader`, where string and mask pointers are offsets
from the start of the DTHD data. The mask comes right after the header,
then the strings, each NUL-padded to an even length. ANY is stored as
0xFFFF. The output was checked against an existing descriptor made by the
Amiga tool, only to confirm the file format; no third-party file is used or
shipped.

Our descriptor's contents are all dictated by function: the WebP
signature from Google's spec (`RIFF????WEBP`), the standard `pict` group,
the format name "WebP", and our own base name `webp`. Other WebP
datatypes (e.g. WarpWebP) necessarily use the same mask and name, so
installing one replaces the other's descriptor. Install_WebPDT offers to
save the old one first.

Installer script: written from scratch, and now minimal. It asks one
question, copies the class and the descriptor (+ icon) to SYS:, checks the
files exist, and asks for a reboot. It doesn't run AddDataTypes or check
the OS/CPU/picture.datatype. An earlier, larger draft reused some message
strings from WarpWebP's installer; it was replaced on 2026-09-30.

`WarpWebPdt/` in this directory is a third-party shareware package, kept
for reference only. Never package it, and don't publish it with the
sources.

Icons (`tools/mkicon.py`, generated by make_package.sh) are classic
4-colour PROJECT icons:

- `DEVS:DataTypes/WebP.info` must have default tool `C:AddDataTypes`, so
  that double-clicking it registers the descriptor. A test install once
  ended up with a MultiView project icon there; our package shipped no
  icon at the time. The package now ships our own icon with the
  installer's `(infos)` copy.
- `Install_WebPDT.info` has default tool `C:Installer`, tool types
  `APPNAME=webp.datatype` and `MINUSER=AVERAGE`, and a stack of 20000.

---------------------------------------------------------------------------

## 5. Library Expunge, and how it's done here

### Who calls what

- **Open vector**: called by `OpenLibrary()`, under Forbid.
- **Close vector**: called by `CloseLibrary()`, under Forbid. Returns the
  seglist when the library may be unloaded now (delayed expunge), otherwise
  0.
- **Expunge vector**: called under Forbid by `RemLibrary()`, typically
  from ramlib's low-memory handler when an `AllocMem()` can't be satisfied.
  `Avail FLUSH` triggers that path on purpose. If Expunge returns a
  seglist, the caller `UnLoadSeg()`s it.

Each library keeps its own reference count. Exec never touches
`lib_OpenCnt`: the library increments it in its Open vector, decrements it
in Close, and decides in Expunge whether it may go away.

What the OS does before calling Expunge:

- **AROS** (`rom/lddemon/lddemon.c`, `LDFlush`) only calls `RemLibrary()`
  on libraries whose `lib_OpenCnt` is 0. Verified in the source.
- **AmigaOS 3.x**: not verified. In practice, `Avail FLUSH` with EmojiGear
  running never crashed, which suggests open libraries are skipped there
  too.

Either way, the RKM contract is that Expunge may be called at any time,
since anybody may call `RemLibrary()`, so the **library itself** must
refuse to go away while in use. The standard answer, from the RKM
sample.library, is:

```
Expunge:  if (lib_OpenCnt != 0) { set LIBF_DELEXP; return 0; }   ; "later"
          ...else Remove(), free resources, FreeMem(base), return seglist
Close:    if (--lib_OpenCnt == 0 && (lib_Flags & LIBF_DELEXP)) -> Expunge
```

For a **class** library there is one more condition. `FreeClass()` fails
(returns FALSE) while objects of the class still exist, and the library
must then stay loaded, because those objects' dispatcher is its code.

### webp.datatype (src/libinit.s + src/libcinit.c)

- `LibExpunge`: if the open count is not 0, it sets DELEXP and returns 0.
  It never looks at `cl_Class` for that decision.
- `DoExpunge` calls `CLibExpunge(base)` first. That function does
  `RemoveClass` and then `FreeClass`.
  - If `FreeClass` fails, it re-does `AddClass` and returns 0. The asm then
    sets DELEXP, returns 0, and the library stays loaded.
  - Only on success does the asm `Remove()` and `FreeMem()` the base and
    return the seglist.
- `CLibInit(base)` runs on every OpenLibrary. It guards on
  `WebpClass != NULL`, so system libraries and the class are set up once
  per residency, and it stores `base->cl_Class`, which `ObtainEngine`
  returns.
- The reserved vector (-24) points to `LibNull` (`moveq #0,d0; rts`), not
  to address 0.

---------------------------------------------------------------------------

## 6. Review: the shared libraries in the EmojiGear repo (utf8rastport.library, unibutton.gadget, unitexteditor.gadget)

Roles: the EmojiGear and MUImojiGear executables are only *consumers*. They
call OpenLibrary()/CloseLibrary() and create gadget objects, and they have
no Expunge of their own. The bugs below are in the libraries' own
`libinit.s`/init code. The executables are where the crash shows up,
because they keep using a library that was unloaded underneath them.

All three libraries use a libinit.s derived from the Developer CD example
`Extras/BOOPSI/led_ic/classinit.asm`. That example's Expunge is:

```
LibExpunge:
        tst.w   LIB_OPENCNT(a6)
        beq.s   DoExpunge          ; nobody has it open -> expunge
        tst.l   cl_Class(a6)
        beq.s   DoExpunge          ; <-- no class -> expunge EVEN IF OPEN
        bset    #LIBB_DELEXP,LIB_FLAGS(a6)
        moveq   #0,d0
        rts
```

In led_ic this is harmless. Its C code (`classbase.c`) always stores the
class in `cb->cb_Library.cl_Class` when the class is created, and clears it
only when `FreeClass()` succeeds. So `cl_Class == NULL` while open can
hardly happen there.

**In EmojiGear, `cl_Class` is never written.** The C side keeps the class in
its own global (`UniButtonClass`, `UniTextEditorClass`), and GetClass
returns that global. utf8rastport has no class at all. `LibInit` never
clears the field either, but exec's MakeLibrary allocates the base with
MEMF_CLEAR, so it is 0. The `tst.l cl_Class(a6)` is therefore **always
zero**, and:

> **Status: all the fixes below are applied in EmojiGear (2026-09-30) and
> build cleanly, but are untested on Amiga. See `git diff` in EmojiGear.**
> 8 files changed: the 3 `libinit.s`, `libutf8rastport/libcinit.c`,
> `class_unibutton_lib.c`, `class_unitexteditor_lib.c`,
> `sdk/include/gadgets/unibutton.h`, and `sdk/include/gadgets/unitexteditor.h`.

### BUG 1: all three libraries accept an Expunge while open

Severity correction: this breaks the library contract, but the system
flush doesn't trigger it on AROS, since `LDFlush` skips open libraries, and
it was never seen on 3.x either. It only matters if something calls
`RemLibrary()` on an open library. The description below is what would
happen then.

If Expunge is called while a program has unitexteditor.gadget open, the
open count is not 0, but `cl_Class == 0`, so it branches to DoExpunge. `UniTextEditor_Exit()` then runs:

- `RemoveClass()`
- `FreeClass()` fails, because EmojiGear's gadget objects are alive; the
  return value is ignored.
- `UniTextEditorClass = NULL`
- utf8rastport.library, bevel.image, keymap, layers, and the rest are
  closed.
- The base is `Remove()`d and `FreeMem()`ed, and the seglist is returned
  and **unloaded**.

EmojiGear's window still contains gadget objects whose dispatcher pointed
into the unloaded code. The next input event or render crashes. Later,
EmojiGear's `CloseLibrary()` writes into the freed base, corrupting memory.
unibutton.gadget does the same thing. utf8rastport.library does the same:
`CLibExpunge` frees the shared FreeType font pool while DCs are still using
it, and unloads the code.


**Fix** (the same 4-line change in each of the three libinit.s): drop the
cl_Class test.

```
LibExpunge:
        tst.w   LIB_OPENCNT(a6)
        beq.s   DoExpunge
        bset    #LIBB_DELEXP,LIB_FLAGS(a6)
        moveq   #0,d0
        rts
```

### BUG 2 (gadgets, minor after fix 1): FreeClass() result ignored

`UniButton_Exit()` and `UniTextEditor_Exit()` call `FreeClass()` and
continue even if it returns FALSE. After fix 1, Expunge only runs with the
open count at 0. Objects can then only still exist if a program disposed
its objects after CloseLibrary() (a program bug), so this becomes
defensive only. The robust version, as done in webp.datatype:

```c
int UniTextEditor_Exit(void)          /* returns 1 if the lib may unload */
{
    if (UniTextEditorClass) {
        RemoveClass(UniTextEditorClass);
        if (!FreeClass(UniTextEditorClass)) {
            AddClass(UniTextEditorClass);   /* objects alive: stay */
            return 0;
        }
        UniTextEditorClass = NULL;
    }
    ... close libraries ...
    return 1;
}
```

In DoExpunge, after `jsr _UniTextEditor_Exit`: if `d0 == 0`, set DELEXP and
return 0 without Remove/FreeMem. See webp.datatype's libinit.s `DoExpunge`.
The failinit path in `_Init` can keep ignoring the result.

### BUG 3 (utf8rastport): CLibInit runs again on every 0 -> 1 open

`LibOpen` calls `_CLibInit` whenever `LIB_OPENCNT == 0`, and `CLibClose`
does nothing. The comment in libcinit.c says these functions "run once
regardless of how many processes OpenLibrary() this". That's not true.
They run each time the open count goes from 0 back to 1 **without an
expunge in between**. For example:

1. Program A opens utf8rastport.library, then quits. The count is 0, and
   the library stays in memory because nothing expunged it.
2. Program B opens it. `CLibInit()` runs again:
   - `urp_shared_fonts_init()` memsets the face and size tables and calls
     `FT_Init_FreeType()` again. The previous FreeType instance and every
     cached face and size (potentially MBs of font data) leak, and the
     semaphores are re-initialised.
   - DOS, graphics, intuition, utility, layers, and cybergraphics are
     opened again without being closed. The open counts only grow, which
     is harmless for ROM libraries but keeps cybergraphics from ever
     expunging.
   - `urp_shared_cluts_init()` wipes the shared CLUT cache.

This is mostly hidden inside EmojiGear, because the gadgets keep
utf8rastport open for as long as they are resident. It shows up after a
gadget expunge, or with any program that opens utf8rastport directly and
is run twice.

The gadgets don't have this problem: their `_Init` is guarded by
`if (Class == NULL)`.

**Fix**: guard CLibInit the same way.

```c
int CLibInit()
{
    if (DOSBase) return 0;            /* already set up for this residency */
    ...
```

This needs CLibExpunge to keep resetting the bases to NULL, which it
already does. It also needs the failinit path to call CLibExpunge, which
it already does.

### Minor

- The reserved vector is `DC.L 0` in all three `LibFuncTable`s. Nobody
  should call -24, but pointing it at a `moveq #0,d0 / rts` stub costs
  nothing.
- `FailInit` in libinit.s is dead code.
- Opening other disk-based libraries (utf8rastport, bevel.image) from
  inside LibOpen breaks the Forbid while ramlib loads them. That is common
  practice and fine, as long as Open and Init don't rely on the Forbid
  being unbroken across those calls, and they don't.

---------------------------------------------------------------------------

## 7. Status / TODO

- Builds cleanly (68020, 141 KB), but it is **untested on Amiga**. First
  tests: `webpdtinfo pic.webp`, `webpdtinfo pic.webp SAVE ram:x.ilbm`, and
  MultiView on an RTG screen and on an 8-bit screen, with lossy, lossless,
  alpha, and animated files.
- Test the expunge path too: open a picture in MultiView, run
  `Avail FLUSH` (webp.datatype must stay), close MultiView, run
  `Avail FLUSH` again (now it must disappear from the library list).
- Ideas:
  - `PDTA_WhichPicture` and `PDTA_GetNumPictures` for animation frames.
  - 030/040/060 builds.
  - A WebP encoder for `DTWM_RAW`.
  - A license for our own sources.
