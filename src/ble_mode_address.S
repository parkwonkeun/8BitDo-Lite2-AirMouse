/* The radio accepts its LE and Classic addresses through separate commands.
 * Replace only the three-byte LE suffix calculation in the v1.06 initializer.
 * Read the physical selector directly because radio initialization can precede
 * the native mode latch. Both addresses differ from the shared r55 identity.
 * Stack layout at this instruction is fixed by the pinned base image. */
.syntax unified
.cpu cortex-m0
.thumb
.section .le_address,"ax",%progbits
    add r0, sp, #4
    movs r3, #3
1:  ldrb r1, [r0]
    ldrb r2, [r0, #3]
    adds r1, r1, r2
    strb r1, [r0]
    strb r1, [r0, #8]
    adds r0, #1
    subs r3, #1
    bne 1b

    bl read_buttons
    lsls r0, r0, #2
    bmi 3f
    movs r2, #0x80
    b 2f
3:  movs r2, #0x40
2:  mov r0, sp
    ldrb r1, [r0, #12]
    eors r1, r2
    strb r1, [r0, #12]
    movs r1, #0
    add r0, sp, #32
    strb r1, [r0, #2]
    b next_stock_instruction
    .org 0x36
next_stock_instruction:
