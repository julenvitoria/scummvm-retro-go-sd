/*
 * An engine image (src/svm_xip.h): a whole ScummVM with one engine, in two parts like
 * scummvm_core.ld's -- but its RAM part is linked above the loader (ld/scummvm_stub.ld,
 * src/stub.c), which reads it there and jumps to svm_engine_entry (boot.c).
 */
INCLUDE gnw_ram_emu.ld

XIP_VMA = 0x98000000;
SVM_ENG_RAM = __RAM_EMU_START__ + 12K;   /* = SVM_ENG_RAM in src/svm_xip.h */

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__ + 12K, LENGTH = __RAM_EMU_LENGTH__ - 12K
  XIP     (rx)  : ORIGIN = 0x98000000, LENGTH = 32M
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  xip  PT_LOAD FLAGS(5);
}

ENTRY(svm_engine_entry)

SECTIONS
{
  .core_entry SVM_ENG_RAM :
  {
    __SVM_RAM_START__ = .;
    KEEP(*boot.o(.text.svm_engine_entry))
    . = ALIGN(4);
    /* resident: is called constantly, or by the firmware while the flash is busy */
    *boot.o(.text* .rodata*)
    *platform_core.o(.text* .rodata*)
    *gw_core_bridge.o(.text* .rodata*)
    *heap.o(.text* .rodata*)
    *tlsf.o(.text* .rodata*)
    *libc.a:lib_a-mem*.o(.text* .rodata*)
    *libc.a:libc_a-mem*.o(.text* .rodata*)
    *libc.a:*str*.o(.text* .rodata*)
    *libc.a:*malloc*.o(.text* .rodata*)
    *libc.a:*freer.o(.text* .rodata*)
    *libc.a:*callocr.o(.text* .rodata*)
    *libc.a:*reallocr.o(.text* .rodata*)
    *libc.a:*mlock.o(.text* .rodata*)
    *libc.a:*sbrkr.o(.text* .rodata*)
    *libgcc.a:*(.text* .rodata*)
    INCLUDE hot_objects.ld
    *(.glue_7) *(.glue_7t)
    . = ALIGN(4);
  } >RAM_EMU :text

  .data :
  {
    . = ALIGN(4);
    __svm_init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __svm_init_array_end = .;
    *(.data*)
    . = ALIGN(4);
    __CORE_CODE_END__ = .;
  } >RAM_EMU :data

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    __CORE_BSS_END__ = .;
  } >RAM_EMU :data

  ASSERT(__CORE_BSS_END__ <= __RAM_EMU_END__, "RAM image does not fit in RAM_EMU")

  .xip XIP_VMA :
  {
    __xip_start = .;
    *(.text*)
    *(.rodata*)
    . = ALIGN(16);
    __xip_end = .;
  } >XIP :xip

  /DISCARD/ :
  {
    *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) *(.fini_array*) *(.fini) *(.init)
    *(.comment) *(.ARM.attributes) *(.gw_core_bridge_probe) *(.core_entry)
  }

  PROVIDE(__exidx_start = 0);
  PROVIDE(__exidx_end = 0);
  /* (referenced by the SDK's headers; an engine image has no firmware-run constructor list) */
  PROVIDE(__CORE_INIT_ARRAY_START__ = 0);
  PROVIDE(__CORE_INIT_ARRAY_END__ = 0);
}
