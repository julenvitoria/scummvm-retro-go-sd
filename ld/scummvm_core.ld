/*
 * ScummVM core: two images from one link.
 *
 *  RAM image  (.core_entry + .data + .bss in RAM_EMU) -- what the firmware loads from the
 *             .bin like any core: the entry, boot.c, the platform layer, the firmware bridge,
 *             the C library pieces that must run while external flash is being written, and
 *             the hot parts of ScummVM (HOT_OBJECTS below), plus all writable data.
 *  XIP image  (.xip, linked at XIP_VMA) -- everything else: the bulk of ScummVM's code and
 *             read-only data. tools/xip_pack.py appends it to the .bin; at boot, boot.c has the
 *             firmware cache the .bin in external flash and relocates the image to wherever it
 *             landed (address words that point into XIP are listed in two bitmaps).
 *
 * Code in RAM calling code in XIP (and back) is out of BL range: the linker puts long-branch
 * veneers in, whose target words xip_pack.py also relocates.
 */
INCLUDE gnw_ram_emu.ld

XIP_VMA = 0x98000000;

MEMORY
{
  RAM_EMU (xrw) : ORIGIN = __RAM_EMU_START__, LENGTH = __RAM_EMU_LENGTH__
  XIP     (rx)  : ORIGIN = 0x98000000, LENGTH = 32M
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
  xip  PT_LOAD FLAGS(5);
}

SECTIONS
{
  .core_entry __RAM_EMU_START__ :
  {
    KEEP(*(.core_entry))
    KEEP(*(.gw_core_bridge_probe))
    . = ALIGN(4);
    /* resident: must run while the flash is being programmed, or is called constantly */
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
    /* the firmware's entry stub runs this list before the entry target: kept empty, the
       real constructors run from boot.c once the XIP image is in place */
    . = ALIGN(4);
    __CORE_INIT_ARRAY_START__ = .;
    __CORE_INIT_ARRAY_END__ = .;
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
    *(.comment) *(.ARM.attributes)
  }

  PROVIDE(__exidx_start = 0);
  PROVIDE(__exidx_end = 0);
}
