/*
 * bao1x_mmu.ld: Baochip-1x bare metal with the BAO_MMU sandbox
 *
 * Used instead of bao1x.ld when an example is built with BAO_MMU=1 (or
 * its folder holds a file named bao_mmu). RRAM and IFRAM are laid out as
 * in bao1x.ld; SRAM is split so the page table can leave unmapped guard
 * pages between the parts:
 *
 *   0x61000000  machine data        16 KB   never mapped for supervisor
 *   0x61004000  machine stack       16 KB   never mapped for supervisor
 *   0x61008000  page tables         80 KB   read only to supervisor code
 *   0x6101C000  guard                4 KB   never mapped
 *   0x6101D000  main stack           8 KB   supervisor, grows down
 *   0x6101F000  guard                4 KB   never mapped
 *   0x61020000  trap stack           4 KB   supervisor interrupt handler
 *   0x61021000  .data, .bss                 supervisor
 *               crash record
 *   ...         unmapped up to the end of SRAM at 0x61200000
 *
 * Machine data is at the bottom so nothing supervisor code can name lies
 * below it within reach: a buffer handed to a machine mode service only
 * ever runs upward from its start, and every supervisor page is above the
 * machine region or more than 2 MB below it (bao_mmu.c caps lengths at
 * 2 MB). A main stack that runs past its 8 KB reaches the guard page under
 * it and the first push there ends in a store page fault; a store that got
 * past it would still land in SRAM. Below SRAM is unmapped as well; what
 * answers there is not settled (rrc.sv decodes only part of the address).
 *
 * The objects bao_flash.sh compiles as machine mode code (msvc_*.o) have
 * their .data and .bss placed in the machine data region; everything else
 * is supervisor data.
 */

ENTRY(_start)

MEMORY
{
    SIGBLOCK (r)  : ORIGIN = 0x60060000, LENGTH = 0x300
    RRAM (rx)     : ORIGIN = 0x60060300, LENGTH = 0xFFD00
    SRAM_M (rw)   : ORIGIN = 0x61000000, LENGTH = 0x4000
    SRAM_PT (rw)  : ORIGIN = 0x61008000, LENGTH = 0x14000
    SRAM_S (rw)   : ORIGIN = 0x61021000, LENGTH = 0x1DF000
    IFRAM0 (rw)   : ORIGIN = 0x50000000, LENGTH = 0x1F000
    IFRAM1 (rw)   : ORIGIN = 0x50020000, LENGTH = 0x20000
}

_program_start   = 0x60060000;
_program_end     = 0x60160000;
_boot1_start     = 0x60020000;

_sram_start      = 0x61000000;
_sram_end        = 0x61200000;

_m_stack_bottom  = 0x61004000;
_m_stack_top     = 0x61008000;
_stack_size      = 0x2000;
_main_stack_bottom = 0x6101D000;
_main_stack_top  = _main_stack_bottom + _stack_size;
_guard_page      = _main_stack_top;
_trap_stack_size = 0x1000;
_trap_stack_bottom = _guard_page + 0x1000;
_trap_stack_top  = _trap_stack_bottom + _trap_stack_size;

_mmu_tables_count = 20;

SECTIONS
{
    .text : ALIGN(4)
    {
        KEEP(*(.text.entry))
        . = 0x100;
        KEEP(*(.text.start))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
    } > RRAM

    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > RRAM

    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > RRAM

    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > RRAM

    .mmu_tables (NOLOAD) : ALIGN(0x1000)
    {
        _mmu_tables_start = .;
        . = . + _mmu_tables_count * 0x1000;
        _mmu_tables_end = .;
    } > SRAM_PT

    .m_data : ALIGN(4)
    {
        _m_data_start = .;
        *msvc_*.o(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        _m_data_end = .;
    } > SRAM_M AT > RRAM

    _m_data_lma = LOADADDR(.m_data);

    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM_S AT > RRAM

    _data_lma   = LOADADDR(.data);
    _image_end  = _data_lma + (_data_end - _data_start);
    _image_size = _image_end - _program_start;

    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(EXCLUDE_FILE(*msvc_*.o) .bss EXCLUDE_FILE(*msvc_*.o) .bss.*)
        *(EXCLUDE_FILE(*msvc_*.o) .sbss EXCLUDE_FILE(*msvc_*.o) .sbss.*)
        *(EXCLUDE_FILE(*msvc_*.o) COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM_S

    /* Not zeroed by crt0, so it survives a soft reset as in bao1x.ld. */
    .sevs_crash (NOLOAD) : ALIGN(4)
    {
        _sevs_crash_start = .;
        KEEP(*(.sevs_crash))
        . = _sevs_crash_start + 0x100;
    } > SRAM_S

    _s_data_end = ALIGN(0x1000);

    .m_bss (NOLOAD) : ALIGN(4)
    {
        _m_bss_start = .;
        *msvc_*.o(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        _m_bss_end = .;
    } > SRAM_M

    _m_region_start = ORIGIN(SRAM_M);
    _m_region_end = _m_stack_top;

    .dma_buffers (NOLOAD) : ALIGN(4)
    {
        _dma_start = .;
        *(.dma_buffers .dma_buffers.*)
        . = ALIGN(4);
        _dma_end = .;
    } > IFRAM0

    __global_pointer$ = _data_start + 0x800;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }

    ASSERT(_image_end <= _program_end,
        "Program too large: the image runs past 0x60160000 into runtime storage.")

    ASSERT(ADDR(.text) >= _program_start,
        "Code is placed below 0x60060000, which is the boot0 and boot1 region.")

    ASSERT(_m_data_lma >= _program_start && _data_lma >= _program_start,
        "Initialised data is placed below 0x60060000, which is the boot0 and boot1 region.")

    ASSERT(_trap_stack_top == ORIGIN(SRAM_S),
        "Supervisor data must start right above the trap stack.")

    ASSERT(_s_data_end <= _sram_end,
        "Out of SRAM: supervisor data runs past the end of SRAM.")

    ASSERT(_mmu_tables_start == 0x61008000 && _mmu_tables_end + 0x1000 == _main_stack_bottom,
        "The page tables must sit between the machine stack and the main stack guard.")

    ASSERT(_m_bss_end <= _m_stack_bottom,
        "Machine mode data reaches the machine stack (16 KB at 0x61000000).")

    ASSERT(_dma_end <= ORIGIN(IFRAM0) + LENGTH(IFRAM0),
        "Too many DMA buffers: .dma_buffers reaches 0x5001F000, which is the boot chain's UART transmit buffer.")
}
