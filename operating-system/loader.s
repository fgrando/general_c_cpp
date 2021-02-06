
.section .text
.extern kernelMain
.global loader

loader:
    mov $kernel_stack, %esp
    call kernelMain

_stop:
    cli
    hlt
    jmp _stop



.section .bss
.space 2*1024*1024  # 2MiB (mebibytes = 2 * 2^20)
kernel_stack:
