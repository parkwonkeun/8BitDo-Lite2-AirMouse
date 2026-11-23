ENTRY(lite2_start)
/* Pinned v1.06 signed division returns the quotient in r0, remainder in r1. */
__aeabi_idiv = 0x01018245;
__aeabi_idivmod = 0x01018245;
SECTIONS {
    . = 0x0102be00;
    .text : { *(.text*) *(.rodata*) }
    .ARM.exidx : { *(.ARM.exidx*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(SIZEOF(.data) == 0, "No implicit initialized RAM allowed")
    ASSERT(SIZEOF(.bss) == 0, "No implicit global state allowed")
    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
