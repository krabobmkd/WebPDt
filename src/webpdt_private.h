#ifndef WEBPDT_PRIVATE_H
#define WEBPDT_PRIVATE_H
/*
 * webp.datatype - private declarations shared by libcinit.c / webpclass.c
 *
 * Globals rule for this shared binary: the only writable globals are the
 * library bases and the class pointer, written once in CLibInit() (under
 * Forbid, from LibOpen) and cleared in CLibExpunge(). Everything that
 * belongs to one picture lives on the stack or in AllocVec() memory of
 * the task doing the OM_NEW, so any number of tasks can load pictures
 * at the same time.
 */
#include <exec/types.h>
#include <intuition/classes.h>
#include <utility/hooks.h>
#include <proto/utility.h>

#include "compilers.h"

extern struct DosLibrary    *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library       *UtilityBase;
extern struct Library       *DataTypesBase;

/* webpclass.c */
ULONG ASM SAVEDS Webp_Dispatch(REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg));

/* libinit.s: run func(arg) on the stack described by sss (exec StackSwap). */
struct StackSwapStruct;
ULONG WebpCallWithStack(ULONG (*func)(APTR), APTR arg, struct StackSwapStruct *sss);

/* amiga.lib equivalents: we link with -nostdlib, so no amiga.lib. */
INLINE ULONG DoSuperMethodA_(Class *cl, Object *o, Msg msg)
{
    return CallHookPkt(&cl->cl_Super->cl_Dispatcher, o, msg);
}
INLINE ULONG CoerceMethodA_(Class *cl, Object *o, Msg msg)
{
    return CallHookPkt(&cl->cl_Dispatcher, o, msg);
}

#endif /* WEBPDT_PRIVATE_H */
