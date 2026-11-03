/* Memory sizes from XMC.CX32L003_DFP.1.0.7.pdsc. No option-byte/NVR region. */
ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
_stack_limit = _stack_top - 1024;
SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : {
        . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .;
    } > RAM
    .stack _stack_limit (NOLOAD) : { . += 1024; } > RAM
    /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.isr_vector) == 192, "Expected 16 core + 32 device vectors")
ASSERT(_ebss <= _stack_limit, "Static RAM collides with reserved 1 KB stack")
ASSERT(LOADADDR(.data) + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH), "Flash overflow")
