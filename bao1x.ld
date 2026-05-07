/*
 * bao1x.ld - Linker script for Baochip-1x bare metal
 *
 * Memory layout:
 *   RRAM:  0x60000000 - 0x603FFFFF (4 MiB, boot0+boot1+user)
 *   SRAM:  0x61000000 - 0x611FFFFF (2 MiB, volatile)
 *   IFRAM: 0x50000000 - 0x5001FFFF (128 KiB, DMA buffers)
 *
 * User code starts at 0x60060000 (BAREMETAL_START).
 * First 768 bytes (0x300) are the signature block.
 * Actual code entry point is 0x60060300.
 */

ENTRY(_start)

MEMORY
{
    /* Signature block occupies the first 768 bytes */
    SIGBLOCK (r)  : ORIGIN = 0x60060000, LENGTH = 0x300

    /* User code and read-only data in RRAM after signature */
    RRAM (rx)     : ORIGIN = 0x60060300, LENGTH = 0x39A000 - 0x300

    /* Stack, heap, .data, .bss in SRAM */
    SRAM (rwx)    : ORIGIN = 0x61000000, LENGTH = 0x200000

    /* DMA buffers - UDMA can ONLY access this region */
    IFRAM (rw)    : ORIGIN = 0x50000000, LENGTH = 0x20000
}

/* Stack at top of SRAM, 8 KiB */
_stack_size = 0x2000;
_stack_top  = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    /* Code in RRAM */
    .text : ALIGN(4)
    {
        *(.text.start)     /* _start entry point first */
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > RRAM

    /* Initialized data: stored in RRAM, copied to SRAM at startup */
    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM AT > RRAM

    _data_lma = LOADADDR(.data);

    /* Uninitialized data in SRAM, zeroed at startup */
    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM

    /* DMA buffer section - place UDMA buffers here explicitly */
    .dma_buffers (NOLOAD) : ALIGN(4)
    {
        _dma_start = .;
        *(.dma_buffers .dma_buffers.*)
        . = ALIGN(4);
        _dma_end = .;
    } > IFRAM

    /* Global pointer for linker relaxation (middle of SRAM .data/.bss) */
    __global_pointer$ = _data_start + 0x800;

    /* Stack at the top of SRAM */
    .stack (NOLOAD) :
    {
        . = . + _stack_size;
    } > SRAM

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
