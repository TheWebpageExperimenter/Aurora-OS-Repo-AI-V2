OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kmain)

PHDRS
{
    text   PT_LOAD FLAGS((1 << 0) | (1 << 2));  /* R+X */
    rodata PT_LOAD FLAGS((1 << 2));             /* R   */
    data   PT_LOAD FLAGS((1 << 1) | (1 << 2));  /* R+W */
}

SECTIONS
{
    . = 0xffffffff80000000;

    .text : { __text_start = .; *(.text .text.*) __text_end = .; } :text

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : {
        *(.data .data.*)
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :data

    .bss : { *(.bss .bss.*) *(COMMON) } :data

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
