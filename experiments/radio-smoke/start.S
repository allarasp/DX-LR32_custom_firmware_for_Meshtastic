.syntax unified
.cpu cortex-m0plus
.thumb
.section .text.entry,"ax"
.global _start
.thumb_func
_start:
 cpsid i
 ldr r0,=0x20001000
 mov sp,r0
 bl ram_main
.global finished
finished:
 bkpt #0
 b .
