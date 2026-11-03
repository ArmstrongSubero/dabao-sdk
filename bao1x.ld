/*
 * bao1x.ld: Baochip-1x bare metal
 *
 * Ships with every project. Nothing here needs editing, and there are no
 * build flags that change it. If a build fails against one of the checks at
 * the bottom of this file, the message says what to do.
 *
 * MEMORY MAP
 *
 *   0x60000000  boot0            128 KB   immutable, write locked
 *   0x60020000  boot1            256 KB   signed, never written by us
 *   0x60060000  program          1 MB     this image
 *   0x60160000  runtime storage           rram_write() at run time
 *   0x603DA000  one-way counters          security, never touch
 *   0x603E0000  data slots                security, never touch
 *
 *   0x61000000  SRAM             2 MB     .data, .bss, main stack, trap stack
 *   0x50000000  IFRAM0           128 KB   DMA buffers, top 4 KB reserved
 *   0x50020000  IFRAM1           128 KB   DMA buffers
 *
 * PROGRAM SPACE
 *
 * 1,047,296 bytes, from 0x60060300 to 0x60160000. The first 768 bytes at
 * 0x60060000 are the signature block written by the signing tool, so code
 * starts at 0x60060300.
 *
 * The ceiling is the start of the runtime storage region. Above it is where
 * rram_write() puts data at run time, and the image must never overlap it.
 * Below 0x60060000 is boot0 and boot1, and a write there costs the board.
 *
 * KEEPING IMAGES SMALL STILL MATTERS
 *
 * Every 256-byte page written during a flash is one pass through the RRAM
 * controller's stage and commit sequence. A 1 MB image is roughly 4000
 * pages on every rebuild; a 64 KB image is 250. Large constant data such as
 * model weights, lookup tables or audio belongs in runtime storage, written
 * once with rram_write() and read back with rram_read(), rather than
 * compiled into the image and rewritten on every build.
 */

ENTRY(_start)

MEMORY
{
    /* Signature block. Populated by the signing tool, not by the linker. */
    SIGBLOCK (r)  : ORIGIN = 0x60060000, LENGTH = 0x300

    /* Program: 0x60060300 through 0x60160000. */
    RRAM (rx)     : ORIGIN = 0x60060300, LENGTH = 0xFFD00

    SRAM (rwx)    : ORIGIN = 0x61000000, LENGTH = 0x200000

    /* Top 4 KB of IFRAM0 is the boot chain's UART transmit buffer at
     * 0x5001F000. Excluded so DMA buffers cannot grow over the console. */
    IFRAM0 (rw)   : ORIGIN = 0x50000000, LENGTH = 0x1F000

    IFRAM1 (rw)   : ORIGIN = 0x50020000, LENGTH = 0x20000
}

/* Region bounds, referenced by the checks at the end of this file. */
_program_start   = 0x60060000;
_program_end     = 0x60160000;
_boot1_start     = 0x60020000;

/* Two stacks at the top of SRAM. The trap handler saves its register frame
 * and runs the C dispatcher on the trap stack, so an interrupt can never
 * reach main()'s stack however deep the handler's call chain goes
 * (sevs_assert_fail alone needs 320 bytes, mini_printf 144). crt0.S sets
 * main()'s sp to _main_stack_top and the handler's to _trap_stack_top. */
_trap_stack_size = 0x800;
_stack_size      = 0x2000;
_stack_top       = ORIGIN(SRAM) + LENGTH(SRAM);
_trap_stack_top  = _stack_top;
_main_stack_top  = _trap_stack_top - _trap_stack_size;
_stack_limit     = _main_stack_top - _stack_size;

/* Crash record for the assertion handler. Given a fixed address here so it
 * cannot be placed over somebody's .bss, and kept outside the range crt0.S
 * zeroes so it survives a soft reset. */
_sevs_crash_size  = 0x100;
_sevs_crash_start = _stack_limit - _sevs_crash_size;

SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text.start)
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

    .data : ALIGN(4)
    {
        _data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM AT > RRAM

    _data_lma   = LOADADDR(.data);
    _image_end  = _data_lma + (_data_end - _data_start);
    _image_size = _image_end - _program_start;

    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM

    /* Fixed address, so an oversized .bss collides at link time rather than
     * silently at run time. */
    .sevs_crash _sevs_crash_start (NOLOAD) :
    {
        KEEP(*(.sevs_crash))
        . = _sevs_crash_size;
    } > SRAM

    /* Placed where the stack actually is, not wherever .bss happens to end.
     * crt0.S sets sp from the symbols above, so reserving space next to .bss would
     * guard the wrong address. */
    .stack _stack_limit (NOLOAD) :
    {
        _stack_bottom = .;
        . = . + _stack_size + _trap_stack_size;
    } > SRAM

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

    /* ---------------------------------------------------------------- */
    /* Build checks. A project that trips one of these does not link.    */
    /* ---------------------------------------------------------------- */

    ASSERT(_image_end <= _program_end,
        "Program too large: the image runs past 0x60160000 into runtime storage. Maximum program size is 1,047,296 bytes. Move large constant data (model weights, lookup tables, audio) into runtime storage with rram_write() and read it with rram_read() instead of compiling it into the image.")

    ASSERT(ADDR(.text) >= _program_start,
        "Code is placed below 0x60060000, which is the boot0 and boot1 region. The program must start at 0x60060300.")

    ASSERT(_data_lma >= _program_start,
        "Initialised data is placed below 0x60060000, which is the boot0 and boot1 region.")

    ASSERT(_bss_end <= _sevs_crash_start,
        "Out of SRAM: .bss reaches the crash record and stack at the top of memory. Reduce static and global data, or move large buffers into runtime storage.")

    ASSERT(_stack_limit + _stack_size + _trap_stack_size == _stack_top,
        "Stack reservation does not reach the top of SRAM. This file and crt0.S disagree about where the stacks live.")

    ASSERT(_trap_stack_size >= 0x400,
        "Trap stack too small: the handler frame is 144 bytes and sevs_assert_fail needs 320 on top. Keep _trap_stack_size at 0x400 or more.")

    ASSERT(_dma_end <= ORIGIN(IFRAM0) + LENGTH(IFRAM0),
        "Too many DMA buffers: .dma_buffers reaches 0x5001F000, which is the boot chain's UART transmit buffer. Overwriting it kills the serial console.")
}
