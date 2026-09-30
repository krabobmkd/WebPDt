/*
 * webpdtinfo - test tool for webp.datatype
 *
 * Usage: webpdtinfo <file> [SAVE <ilbmfile>]
 *
 * Loads <file> through datatypes.library (so it exercises the descriptor
 * recognition + webp.datatype), lays it out, and prints what
 * picture.datatype received. With SAVE, writes it back as IFF ILBM
 * through DTM_WRITE (checks the pixel data really went through).
 */
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/datatypes.h>

struct Library *DataTypesBase = NULL;

int main(int argc, char **argv)
{
    Object *o;
    struct BitMapHeader *bmhd = NULL;
    struct DataType *dtn = NULL;
    ULONG modeid = 0;
    int rc = RETURN_FAIL;

    if (argc < 2)
    {
        printf("usage: %s <file> [SAVE <ilbmfile>]\n", argv[0]);
        return RETURN_ERROR;
    }

    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    if (!DataTypesBase)
    {
        printf("can't open datatypes.library v39\n");
        return RETURN_FAIL;
    }

    o = NewDTObject((APTR)argv[1],
                    DTA_GroupID, GID_PICTURE,
                    PDTA_Remap,  FALSE,
                    PDTA_DestMode, PMODE_V43,
                    TAG_DONE);
    if (!o)
    {
        LONG err = IoErr();
        char buf[100];
        Fault(err, (STRPTR)"NewDTObject", (STRPTR)buf, sizeof(buf));
        printf("%s (%ld)\n", buf, (long)err);
        goto out;
    }

    GetDTAttrs(o, DTA_DataType,      (ULONG)&dtn,
                  PDTA_BitMapHeader, (ULONG)&bmhd,
                  PDTA_ModeID,       (ULONG)&modeid,
                  TAG_DONE);

    if (dtn)
        printf("datatype : %s (base %s)\n",
               dtn->dtn_Header->dth_Name, dtn->dtn_Header->dth_BaseName);
    if (bmhd)
        printf("size     : %u x %u, depth %u, masking %u\n",
               (unsigned)bmhd->bmh_Width, (unsigned)bmhd->bmh_Height,
               (unsigned)bmhd->bmh_Depth, (unsigned)bmhd->bmh_Masking);
    printf("modeid   : $%08lx\n", (unsigned long)modeid);

    if (!DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, 0, 1))
        printf("DTM_PROCLAYOUT failed (%ld)\n", (long)IoErr());
    else
        printf("layout   : ok\n");

    if (argc >= 4 && !strcmp(argv[2], "SAVE"))
    {
        BPTR fh = Open((STRPTR)argv[3], MODE_NEWFILE);
        if (fh)
        {
            ULONG r = DoDTMethod(o, NULL, NULL, DTM_WRITE, 0, (ULONG)fh, DTWM_IFF, 0);
            Close(fh);
            printf("save ILBM: %s\n", r ? "ok" : "failed");
        }
    }

    DisposeDTObject(o);
    rc = RETURN_OK;
out:
    CloseLibrary(DataTypesBase);
    return rc;
}
