/*
 * webp.datatype - picture.datatype subclass decoding WebP with libwebp.
 *
 * We only produce truecolour pixels (V43 "PMODE_V43" interface).
 * picture.datatype does everything else: remapping/dithering to 8-bit
 * and lower screens, scaling, friend bitmaps, alpha handling on RTG.
 *
 * Two ways to hand the pixels over:
 *  1. V47 picture.datatype: PDTA_ObtainPixelBuffer gives us direct access
 *     to its own pixel buffer, libwebp decodes straight into it
 *     (no extra full-size copy).
 *  2. V43+: decode into a temporary buffer, then PDTM_WRITEPIXELARRAY.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>

#include "webp/decode.h"
#include "webp/demux.h"

#include "webpdt_private.h"

/* Stack wanted by the libwebp decoder. If the calling task has less
 * available, the decode is run on a temporary stack (StackSwap). */
#define WEBPDT_DECODE_STACK   (48 * 1024)

/* sanity limit on the compressed file we load in memory */
#define WEBPDT_MAX_FILESIZE   (64UL * 1024 * 1024)

/*------------------------------------------------------------------------*/

struct DecodeJob
{
    const UBYTE   *data;
    ULONG          size;
    UBYTE         *dst;      /* first pixel of the frame in the dest buffer */
    ULONG          stride;
    ULONG          dstsize;  /* bytes available from dst */
    WEBP_CSP_MODE  mode;
    VP8StatusCode  status;
};

static ULONG RunDecode(APTR arg)
{
    struct DecodeJob *job = (struct DecodeJob *)arg;
    WebPDecoderConfig cfg;

    if (!WebPInitDecoderConfig(&cfg))
    {
        job->status = VP8_STATUS_INVALID_PARAM;
        return 0;
    }
    cfg.output.colorspace         = job->mode;
    cfg.output.is_external_memory = 1;
    cfg.output.u.RGBA.rgba        = job->dst;
    cfg.output.u.RGBA.stride      = (int)job->stride;
    cfg.output.u.RGBA.size        = job->dstsize;

    job->status = WebPDecode(job->data, job->size, &cfg);
    WebPFreeDecBuffer(&cfg.output); /* nothing to free with external memory */

    return job->status == VP8_STATUS_OK;
}

static ULONG CallDecode(struct DecodeJob *job)
{
    struct Task *me = FindTask(NULL);
    struct StackSwapStruct sss;
    ULONG avail = (ULONG)&sss - (ULONG)me->tc_SPLower;
    APTR stack;
    ULONG r;

    if (avail >= WEBPDT_DECODE_STACK)
        return RunDecode(job);

    stack = AllocVec(WEBPDT_DECODE_STACK, MEMF_ANY);
    if (!stack)
    {
        job->status = VP8_STATUS_OUT_OF_MEMORY;
        return 0;
    }
    sss.stk_Lower   = stack;
    sss.stk_Upper   = (ULONG)stack + WEBPDT_DECODE_STACK;
    sss.stk_Pointer = (APTR)sss.stk_Upper;

    r = WebpCallWithStack(RunDecode, job, &sss);

    FreeVec(stack);
    return r;
}

/*------------------------------------------------------------------------*/

static LONG StatusToError(VP8StatusCode st)
{
    switch (st)
    {
        case VP8_STATUS_OUT_OF_MEMORY:     return ERROR_NO_FREE_STORE;
        case VP8_STATUS_NOT_ENOUGH_DATA:   return DTERROR_NOT_ENOUGH_DATA;
        case VP8_STATUS_UNSUPPORTED_FEATURE:
        case VP8_STATUS_BITSTREAM_ERROR:
        default:                           return DTERROR_INVALID_DATA;
    }
}

/* Load the whole (compressed) file. Returns AllocVec'd buffer or NULL. */
static UBYTE *ReadWholeFile(BPTR fh, ULONG *psize, LONG *perr)
{
    LONG size;
    UBYTE *buf;

    if (Seek(fh, 0, OFFSET_END) == -1)
    {
        *perr = IoErr();
        return NULL;
    }
    size = Seek(fh, 0, OFFSET_BEGINNING); /* returns previous pos = size */
    if (size < 12)
    {
        *perr = (size < 0) ? IoErr() : ERROR_OBJECT_WRONG_TYPE;
        return NULL;
    }
    if ((ULONG)size > WEBPDT_MAX_FILESIZE)
    {
        *perr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    buf = AllocVec((ULONG)size, MEMF_ANY);
    if (!buf)
    {
        *perr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    if (Read(fh, buf, size) != size)
    {
        *perr = IoErr() ? IoErr() : DTERROR_NOT_ENOUGH_DATA;
        FreeVec(buf);
        return NULL;
    }
    *psize = (ULONG)size;
    return buf;
}

static int ModeFromPixelFormat(ULONG fmt, WEBP_CSP_MODE *mode, ULONG *bpp)
{
    switch (fmt)
    {
        case PBPAFMT_RGB:  *mode = MODE_RGB;  *bpp = 3; return 1;
        case PBPAFMT_RGBA: *mode = MODE_RGBA; *bpp = 4; return 1;
        case PBPAFMT_ARGB: *mode = MODE_ARGB; *bpp = 4; return 1;
        default: return 0;
    }
}

/*------------------------------------------------------------------------*/

static BOOL Webp_Load(Class *cl, Object *o)
{
    ULONG srctype = 0;
    ULONG handle = 0;
    struct BitMapHeader *bmhd = NULL;
    STRPTR name = NULL;

    UBYTE *filebuf = NULL;
    const UBYTE *data = NULL;
    ULONG datasize = 0;

    WebPDemuxer *dmx = NULL;
    WebPIterator iter;
    BOOL haveiter = FALSE;

    WebPBitstreamFeatures feat;
    struct DecodeJob job;
    struct pdtBlitPixelArray pbpa;

    UBYTE *ownbuf = NULL;
    UBYTE *dst;
    ULONG width, height, stride, dstsize, bpp, pixfmt;
    ULONG xoff = 0, yoff = 0, fw, fh;
    const UBYTE *bits;
    ULONG bitssize;
    WEBP_CSP_MODE mode;
    BOOL hasalpha, direct = FALSE;
    LONG err = 0;
    BOOL ok = FALSE;

    if (GetDTAttrs(o, DTA_SourceType,    (ULONG)&srctype,
                      DTA_Handle,        (ULONG)&handle,
                      PDTA_BitMapHeader, (ULONG)&bmhd,
                      TAG_DONE) != 3 || !bmhd)
    {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return FALSE;
    }

    switch (srctype)
    {
        case DTST_RAM:
            /* empty object, to be filled by someone else */
            if (!handle) return TRUE;
            SetIoErr(ERROR_NOT_IMPLEMENTED);
            return FALSE;

        case DTST_FILE:
            if (!handle)
            {
                SetIoErr(ERROR_OBJECT_NOT_FOUND);
                return FALSE;
            }
            filebuf = ReadWholeFile((BPTR)handle, &datasize, &err);
            if (!filebuf) goto done;
            data = filebuf;
            break;

        case DTST_MEMORY:
            if (GetDTAttrs(o, DTA_SourceAddress, (ULONG)&data,
                              DTA_SourceSize,    (ULONG)&datasize,
                              TAG_DONE) != 2 || !data)
            {
                SetIoErr(ERROR_OBJECT_NOT_FOUND);
                return FALSE;
            }
            break;

        default:
            SetIoErr(ERROR_NOT_IMPLEMENTED);
            return FALSE;
    }

    if (WebPGetFeatures(data, datasize, &feat) != VP8_STATUS_OK)
    {
        err = ERROR_OBJECT_WRONG_TYPE;
        goto done;
    }
    width    = (ULONG)feat.width;
    height   = (ULONG)feat.height;
    hasalpha = feat.has_alpha ? TRUE : FALSE;
    bits     = data;
    bitssize = datasize;

    if (feat.has_animation)
    {
        /* Animated WebP: show the first frame on the canvas. */
        WebPData wd;
        wd.bytes = data;
        wd.size  = datasize;
        dmx = WebPDemux(&wd);
        if (!dmx || !WebPDemuxGetFrame(dmx, 1, &iter))
        {
            err = DTERROR_INVALID_DATA;
            goto done;
        }
        haveiter = TRUE;
        width  = WebPDemuxGetI(dmx, WEBP_FF_CANVAS_WIDTH);
        height = WebPDemuxGetI(dmx, WEBP_FF_CANVAS_HEIGHT);
        bits     = iter.fragment.bytes;
        bitssize = iter.fragment.size;
        xoff = (ULONG)iter.x_offset;
        yoff = (ULONG)iter.y_offset;
        fw   = (ULONG)iter.width;
        fh   = (ULONG)iter.height;
        if ((WebPDemuxGetI(dmx, WEBP_FF_FORMAT_FLAGS) & ALPHA_FLAG) || iter.has_alpha)
            hasalpha = TRUE;
        /* uncovered canvas area is transparent */
        if (fw != width || fh != height) hasalpha = TRUE;
        if (xoff + fw > width || yoff + fh > height)
        {
            err = DTERROR_INVALID_DATA;
            goto done;
        }
    }
    else
    {
        fw = width;
        fh = height;
    }

    if (!width || !height || width > 0xFFFF || height > 0xFFFF)
    {
        err = DTERROR_INVALID_DATA;
        goto done;
    }

    /* --- describe the picture to picture.datatype --- */
    bmhd->bmh_Width      = (UWORD)width;
    bmhd->bmh_Height     = (UWORD)height;
    bmhd->bmh_PageWidth  = (WORD)width;
    bmhd->bmh_PageHeight = (WORD)height;
    bmhd->bmh_Depth      = hasalpha ? 32 : 24;
    bmhd->bmh_Masking    = hasalpha ? mskHasAlpha : mskNone;

    GetDTAttrs(o, DTA_Name, (ULONG)&name, TAG_DONE);
    SetDTAttrs(o, NULL, NULL,
               DTA_ObjName,      (ULONG)(name ? FilePart(name) : (STRPTR)""),
               DTA_NominalHoriz, width,
               DTA_NominalVert,  height,
               PDTA_SourceMode,  PMODE_V43,
               TAG_DONE);

    /* --- 1. V47: try to decode directly into picture.datatype's buffer --- */
    pbpa.MethodID           = 0;
    pbpa.pbpa_PixelData     = NULL;
    pbpa.pbpa_PixelFormat   = 0;
    pbpa.pbpa_PixelArrayMod = 0;
    pbpa.pbpa_Left = pbpa.pbpa_Top = pbpa.pbpa_Width = pbpa.pbpa_Height = 0;
    SetDTAttrs(o, NULL, NULL,
               PDTA_SourceMode,        PMODE_V43,
               DTA_NominalHoriz,       width,
               DTA_NominalVert,        height,
               PDTA_ObtainPixelBuffer, (ULONG)&pbpa,
               TAG_DONE);

    if (pbpa.pbpa_PixelData &&
        ModeFromPixelFormat(pbpa.pbpa_PixelFormat, &mode, &bpp) &&
        pbpa.pbpa_PixelArrayMod >= width * bpp)
    {
        direct  = TRUE;
        dst     = (UBYTE *)pbpa.pbpa_PixelData;
        stride  = pbpa.pbpa_PixelArrayMod;
        pixfmt  = pbpa.pbpa_PixelFormat;
    }
    else
    {
        /* --- 2. own buffer + PDTM_WRITEPIXELARRAY --- */
        pixfmt = hasalpha ? PBPAFMT_ARGB : PBPAFMT_RGB;
        ModeFromPixelFormat(pixfmt, &mode, &bpp);
        stride = width * bpp;
        ownbuf = AllocVec(stride * height,
                          (fw != width || fh != height) ? (MEMF_ANY | MEMF_CLEAR) : MEMF_ANY);
        if (!ownbuf)
        {
            err = ERROR_NO_FREE_STORE;
            goto done;
        }
        dst = ownbuf;
    }
    dstsize = stride * height;

    /* --- decode --- */
    job.data    = bits;
    job.size    = bitssize;
    job.dst     = dst + yoff * stride + xoff * bpp;
    job.dstsize = dstsize - (yoff * stride + xoff * bpp);
    job.stride  = stride;
    job.mode    = mode;
    job.status  = VP8_STATUS_OK;

    if (!CallDecode(&job))
    {
        err = StatusToError(job.status);
        goto done;
    }

    if (!direct)
    {
        struct pdtBlitPixelArray wpa;
        wpa.MethodID           = PDTM_WRITEPIXELARRAY;
        wpa.pbpa_PixelData     = ownbuf;
        wpa.pbpa_PixelFormat   = pixfmt;
        wpa.pbpa_PixelArrayMod = stride;
        wpa.pbpa_Left          = 0;
        wpa.pbpa_Top           = 0;
        wpa.pbpa_Width         = width;
        wpa.pbpa_Height        = height;
        if (!DoSuperMethodA_(cl, o, (Msg)&wpa))
        {
            err = ERROR_NO_FREE_STORE;
            goto done;
        }
    }
    (void)pixfmt;

    ok = TRUE;

done:
    if (ownbuf) FreeVec(ownbuf);
    if (haveiter) WebPDemuxReleaseIterator(&iter);
    if (dmx) WebPDemuxDelete(dmx);
    if (filebuf) FreeVec(filebuf);
    if (!ok) SetIoErr(err ? err : DTERROR_INVALID_DATA);
    return ok;
}

/*------------------------------------------------------------------------*/

ULONG ASM SAVEDS Webp_Dispatch(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    switch (msg->MethodID)
    {
        case OM_NEW:
        {
            ULONG r = DoSuperMethodA_(cl, o, msg);
            if (r && !Webp_Load(cl, (Object *)r))
            {
                LONG err = IoErr();
                ULONG dispose = OM_DISPOSE;
                CoerceMethodA_(cl, (Object *)r, (Msg)&dispose);
                SetIoErr(err);
                r = 0;
            }
            return r;
        }

        case DTM_WRITE:
            /* No WebP encoder: raw saving is not supported.
             * DTWM_IFF goes to picture.datatype (writes an ILBM). */
            if (((struct dtWrite *)msg)->dtw_Mode == DTWM_RAW)
            {
                SetIoErr(ERROR_NOT_IMPLEMENTED);
                return 0;
            }
            return DoSuperMethodA_(cl, o, msg);

        default:
            return DoSuperMethodA_(cl, o, msg);
    }
}
