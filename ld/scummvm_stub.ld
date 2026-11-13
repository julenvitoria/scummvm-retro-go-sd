/*
 * The loader (src/stub.c): the scummvm.bin the firmware loads at the start of RAM_EMU. All of
 * it in RAM, and below SVM_ENG_RAM, where the engine image it loads begins.
 */
INCLUDE gnw_ram_emu.ld

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = 12K
}

SECTIONS
{
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    *(.text*) *(.rodata*)
    *(.glue_7) *(.glue_7t)
    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    __CORE_INIT_ARRAY_END__ = .;
  } >RAM_EMU

  .data :
  {
    . = ALIGN(4);
    *(.data*)
    . = ALIGN(4);
    __CORE_CODE_END__ = .;
  } >RAM_EMU

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    __CORE_BSS_END__ = .;
  } >RAM_EMU

  /DISCARD/ :
  {
    *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.init_array*) *(.fini_array*) *(.fini) *(.init)
    *(.comment) *(.ARM.attributes)
  }
  PROVIDE(__exidx_start = 0);
  PROVIDE(__exidx_end = 0);
}
