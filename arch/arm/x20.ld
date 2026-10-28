ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x46000000;
    .text : { KEEP(*(.text.boot)) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text
    .data : { *(.data .data.*) } :data
    . = ALIGN(4);
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) . = ALIGN(4); } :data
    __bss_end = .;
    . = ALIGN(16);
    .stacks (NOLOAD) : { . += 0x1000; __exception_stack = .; . += 0x10000; __stack_top = .; } :data
    __end = .;
    ASSERT(__end < 0x46900000, "LK exceeds owned memory")
    /DISCARD/ : { *(.comment) *(.note*) *(.ARM.exidx*) *(.ARM.extab*) }
}
