/*
 * webp.datatype - library init / expunge, class creation.
 *
 * Called from libinit.s:
 *   CLibInit(base)    on every OpenLibrary() (under Forbid). Does the real
 *                     work only once per residency; returns 0 on success.
 *   CLibExpunge(base) when exec wants us out of memory. Returns non-zero
 *                     if the library may be unloaded.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/classes.h>
#include <datatypes/pictureclass.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include "webpdt_private.h"
#include "libversion.h"

/* libwebp dsp function-pointer setup (see CLibInit) */
#include "src/dsp/dsp.h"
#include "src/dsp/lossless.h"

struct DosLibrary    *DOSBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *UtilityBase   = NULL;
struct Library       *DataTypesBase = NULL;
static struct Library *PictureDTBase = NULL;
static Class         *WebpClass     = NULL;

const char Lib_ID[]        = "webp.datatype";
const char VersionString[] = "webp.datatype " WEBPDT_VERSTR " (" WEBPDT_DATE ")\r\n";
/* for the C:Version command */
const char VersionTag[] __attribute__((used)) =
    "\0$VER: webp.datatype " WEBPDT_VERSTR " (" WEBPDT_DATE ") libwebp decoder";

int CLibExpunge(struct ClassLibrary *base);

static void CloseSysLibs(void)
{
    if (PictureDTBase) CloseLibrary(PictureDTBase);
    PictureDTBase = NULL;
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    DataTypesBase = NULL;
    if (UtilityBase) CloseLibrary(UtilityBase);
    UtilityBase = NULL;
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    IntuitionBase = NULL;
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    DOSBase = NULL;
}

int CLibInit(struct ClassLibrary *base)
{
    if (WebpClass)
    {
        base->cl_Class = WebpClass;
        return 0;
    }

    DOSBase       = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    UtilityBase   = OpenLibrary((CONST_STRPTR)"utility.library", 39);
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    /* our superclass: V43+ is needed for PDTM_WRITEPIXELARRAY / truecolor */
    PictureDTBase = OpenLibrary((CONST_STRPTR)"datatypes/picture.datatype", 43);

    if (!DOSBase || !IntuitionBase || !UtilityBase || !DataTypesBase || !PictureDTBase)
        goto failinit;

    /* libwebp lazily fills global dsp function pointers on first decode,
     * guarded only by a non-atomic flag (no WEBP_USE_THREAD here).
     * Do it once now, single-threaded, so concurrent decodes from several
     * tasks only ever read them. */
    VP8DspInit();
    VP8LDspInit();
    VP8FiltersInit();
    WebPInitAlphaProcessing();
    WebPInitSamplers();
    WebPInitUpsamplers();
    WebPRescalerDspInit();

    WebpClass = MakeClass((ClassID)Lib_ID, (ClassID)PICTUREDTCLASS, NULL, 0, 0);
    if (!WebpClass) goto failinit;

    WebpClass->cl_Dispatcher.h_Entry    = (ULONG (*)())Webp_Dispatch; /* h_Entry is ULONG (*)(), not HOOKFUNC */
    WebpClass->cl_Dispatcher.h_SubEntry = NULL;
    WebpClass->cl_Dispatcher.h_Data     = NULL;
    AddClass(WebpClass);

    base->cl_Class = WebpClass;
    return 0;

failinit:
    CloseSysLibs();
    return 1;
}

int CLibExpunge(struct ClassLibrary *base)
{
    if (WebpClass)
    {
        RemoveClass(WebpClass);
        if (!FreeClass(WebpClass))
        {
            /* objects still alive: stay in memory */
            AddClass(WebpClass);
            return 0;
        }
        WebpClass = NULL;
        base->cl_Class = NULL;
    }
    CloseSysLibs();
    return 1;
}
