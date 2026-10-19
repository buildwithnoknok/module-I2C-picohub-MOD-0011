/* SPDX-License-Identifier: MIT
 * noknok PicoHub (MOD-0011) power manager - CH32X035 application linker script
 *
 * The application is linked at the noknok bootloader's app base from day one (ADR-004
 * decision 4), so nothing is relinked when the X035 bootloader port (DEV-124) lands.
 * Same layout as the CH32V003 modules (layout 2):
 *
 *   0x0000_0000  stage-0 / stage-1 bootloader   5 KB  (until DEV-124: a 4-byte jump stub,
 *                                                     see tools/mk_swd_image.py)
 *   0x0000_1400  application                    this image, up to 56 KB
 *   0x0000_F400  metadata                       last 1 KB of the 62 KB flash (reserved)
 *
 * 0x1400 is 1 KB aligned, which keeps the vector table on a 1 KB boundary (mtvec).
 * The top 16 bytes of RAM (0x20004FF0) are reserved for the bootloader handoff cell,
 * like the V003's 0x200007F0: the stack ends below it.
 * Derived from ch32fun's generated_CH32X035G8U6_.ld (ch32fun 95030fd).
 */
ENTRY( InterruptVector )
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00001400, LENGTH = 0xE000   /* 56 KB: 0x1400 .. 0xF3FF */
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 20464    /* 20 KB - 16 B (handoff @ 0x20004FF0) */
}
SECTIONS
{
  .init :
  {
   _sinit = .;
   . = ALIGN(4);
   KEEP(*(SORT_NONE(.init)))
   . = ALIGN(4);
   _einit = .;
  } >FLASH AT>FLASH
  ASSERT( (ADDR(.init) & 0x3FF) == 0, "vector table must start on a 1 KB boundary" )
  .text :
  {
   . = ALIGN(4);
   *(.text)
   *(.text.*)
   *(.rodata)
   *(.rodata*)
   *(.gnu.linkonce.t.*)
   . = ALIGN(4);
  } >FLASH AT>FLASH
  .fini :
  {
   KEEP(*(SORT_NONE(.fini)))
   . = ALIGN(4);
  } >FLASH AT>FLASH
  PROVIDE( _etext = . );
  PROVIDE( _eitcm = . );
  .preinit_array :
  {
   PROVIDE_HIDDEN (__preinit_array_start = .);
   KEEP (*(.preinit_array))
   PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH AT>FLASH
  .init_array :
  {
   PROVIDE_HIDDEN (__init_array_start = .);
   KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
   KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
   PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH AT>FLASH
  .fini_array :
  {
   PROVIDE_HIDDEN (__fini_array_start = .);
   KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
   KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
   PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH AT>FLASH
  .ctors :
  {
   KEEP (*crtbegin.o(.ctors))
   KEEP (*crtbegin?.o(.ctors))
   KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
   KEEP (*(SORT(.ctors.*)))
   KEEP (*(.ctors))
  } >FLASH AT>FLASH
  .dtors :
  {
   KEEP (*crtbegin.o(.dtors))
   KEEP (*crtbegin?.o(.dtors))
   KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
   KEEP (*(SORT(.dtors.*)))
   KEEP (*(.dtors))
  } >FLASH AT>FLASH
  .dalign :
  {
   . = ALIGN(4);
   PROVIDE(_data_vma = .);
  } >RAM AT>FLASH
  .dlalign :
  {
   . = ALIGN(4);
   PROVIDE(_data_lma = .);
  } >FLASH AT>FLASH
  .data :
  {
   . = ALIGN(4);
   __global_pointer$ = . + 0x3fc;
   *(.gnu.linkonce.r.*)
   *(.data .data.*)
   *(.gnu.linkonce.d.*)
   . = ALIGN(8);
   *(.sdata .sdata.*)
   *(.sdata2*)
   *(.gnu.linkonce.s.*)
   . = ALIGN(8);
   *(.srodata.cst16)
   *(.srodata.cst8)
   *(.srodata.cst4)
   *(.srodata.cst2)
   *(.srodata .srodata.*)
   . = ALIGN(4);
   PROVIDE( _edata = .);
  } >RAM AT>FLASH
  .bss :
  {
   . = ALIGN(4);
   PROVIDE( _sbss = .);
   *(.sbss*)
   *(.gnu.linkonce.sb.*)
   *(.bss*)
   *(.gnu.linkonce.b.*)
   *(COMMON*)
   . = ALIGN(4);
   PROVIDE( _ebss = .);
  } >RAM
  PROVIDE( _end = _ebss);
  PROVIDE( end = . );
  PROVIDE( _eusrstack = ORIGIN(RAM) + LENGTH(RAM));   /* = 0x20004FF0 */
  /DISCARD/ : {
   *(.note .note.*)
   *(.eh_frame .eh_frame.*)
   *(.comment .comment.*)
   *(.ARM.extab* .gnu.linkonce.armextab.*)
   *(.ARM.exidx*)
  }
}
