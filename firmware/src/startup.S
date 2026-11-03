/* GNU startup authored for this project; vectors follow DFP ARM startup. */
.syntax unified
.cpu cortex-m0plus
.thumb
.section .isr_vector,"a",%progbits
.balign 256
.global __Vectors
__Vectors:
    .word _stack_top
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word 0
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word GPIOA_IRQHandler
    .word GPIOB_IRQHandler
    .word GPIOC_IRQHandler
    .word GPIOD_IRQHandler
    .word FLASH_IRQHandler
    .word 0
    .word UART0_IRQHandler
    .word UART1_IRQHandler
    .word LPUART_IRQHandler
    .word 0
    .word SPI_IRQHandler
    .word 0
    .word I2C_IRQHandler
    .word 0
    .word TIM10_IRQHandler
    .word TIM11_IRQHandler
    .word LPTIM_IRQHandler
    .word 0
    .word TIM1_IRQHandler
    .word TIM2_IRQHandler
    .word 0
    .word PCA_IRQHandler
    .word WWDG_IRQHandler
    .word IWDG_IRQHandler
    .word ADC_IRQHandler
    .word LVD_IRQHandler
    .word VC_IRQHandler
    .word 0
    .word AWK_IRQHandler
    .word OWIRE_IRQHandler
    .word RTC_IRQHandler
    .word CLKTRIM_IRQHandler

.section .text.Reset_Handler,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0]
    str r3, [r1]
    adds r0, #4
    adds r1, #4
    b 1b
2:  ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1]
    adds r1, #4
    b 3b
4:  bl SystemInit
    bl main
5:  b 5b
.size Reset_Handler, .-Reset_Handler

.section .text.Default_Handler,"ax",%progbits
.global Default_Handler
.type Default_Handler,%function
.thumb_func
Default_Handler:
    b .
.size Default_Handler, .-Default_Handler
.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
.weak GPIOA_IRQHandler
.thumb_set GPIOA_IRQHandler, Default_Handler
.weak GPIOB_IRQHandler
.thumb_set GPIOB_IRQHandler, Default_Handler
.weak GPIOC_IRQHandler
.thumb_set GPIOC_IRQHandler, Default_Handler
.weak GPIOD_IRQHandler
.thumb_set GPIOD_IRQHandler, Default_Handler
.weak FLASH_IRQHandler
.thumb_set FLASH_IRQHandler, Default_Handler
.weak UART0_IRQHandler
.thumb_set UART0_IRQHandler, Default_Handler
.weak UART1_IRQHandler
.thumb_set UART1_IRQHandler, Default_Handler
.weak LPUART_IRQHandler
.thumb_set LPUART_IRQHandler, Default_Handler
.weak SPI_IRQHandler
.thumb_set SPI_IRQHandler, Default_Handler
.weak I2C_IRQHandler
.thumb_set I2C_IRQHandler, Default_Handler
.weak TIM10_IRQHandler
.thumb_set TIM10_IRQHandler, Default_Handler
.weak TIM11_IRQHandler
.thumb_set TIM11_IRQHandler, Default_Handler
.weak LPTIM_IRQHandler
.thumb_set LPTIM_IRQHandler, Default_Handler
.weak TIM1_IRQHandler
.thumb_set TIM1_IRQHandler, Default_Handler
.weak TIM2_IRQHandler
.thumb_set TIM2_IRQHandler, Default_Handler
.weak PCA_IRQHandler
.thumb_set PCA_IRQHandler, Default_Handler
.weak WWDG_IRQHandler
.thumb_set WWDG_IRQHandler, Default_Handler
.weak IWDG_IRQHandler
.thumb_set IWDG_IRQHandler, Default_Handler
.weak ADC_IRQHandler
.thumb_set ADC_IRQHandler, Default_Handler
.weak LVD_IRQHandler
.thumb_set LVD_IRQHandler, Default_Handler
.weak VC_IRQHandler
.thumb_set VC_IRQHandler, Default_Handler
.weak AWK_IRQHandler
.thumb_set AWK_IRQHandler, Default_Handler
.weak OWIRE_IRQHandler
.thumb_set OWIRE_IRQHandler, Default_Handler
.weak RTC_IRQHandler
.thumb_set RTC_IRQHandler, Default_Handler
.weak CLKTRIM_IRQHandler
.thumb_set CLKTRIM_IRQHandler, Default_Handler

.section .note.GNU-stack,"",%progbits
