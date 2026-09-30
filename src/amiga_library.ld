/*
 * Linker script for an AmigaOS m68k shared library (.library / .datatype).
 *
 * The __libinit section from libinit.s must be first in the output:
 * offset 0 is "moveq #-1,d0 / rts" (rejects direct execution), followed by
 * the RomTag that exec's loader scans for.
 *
 * Usage: target_link_options(... -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/src/amiga_library.ld)
 */

SECTIONS
{
    . = 0;

    .text :
    {
        *(__libinit)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4);
    .data :
    {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(4);
    .bss :
    {
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
