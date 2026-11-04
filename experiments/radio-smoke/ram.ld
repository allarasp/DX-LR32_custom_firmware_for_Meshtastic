ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 0xb00 }
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > RAM
 .data : { *(.data*) } > RAM
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(SIZEOF(.text)+SIZEOF(.data)+SIZEOF(.bss)<=0xb00,"RAM test overlaps result area")
