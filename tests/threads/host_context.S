/* Umicom Kernel native test adapter, not a boot port.
 * Linux x86-64 System V uses rbx/rbp/r12..r15 as callee-saved registers.
 * Store their continuation using the same opaque context storage as the C
 * scheduler. These slots have host-specific meaning only in this test target.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
    .text
    .global UmicomKernelThreadContextSwitch
    .type UmicomKernelThreadContextSwitch,@function
UmicomKernelThreadContextSwitch:
    mov (%rsp), %rax /* Save the return PC separately from the host stack. */
    mov %rax, 0(%rdi)
    lea 8(%rsp), %rax /* Normalised caller stack is sixteen-byte aligned. */
    mov %rax, 8(%rdi)
    mov %rbx,16(%rdi) /* Host callee-saved context. */
    mov %rbp,24(%rdi)
    mov %r12,32(%rdi)
    mov %r13,40(%rdi)
    mov %r14,48(%rdi)
    mov %r15,56(%rdi)
    mov 0(%rsi), %rax /* Incoming continuation. */
    mov 8(%rsi), %rsp
    mov 16(%rsi), %rbx
    mov 24(%rsi), %rbp
    mov 32(%rsi), %r12
    mov 40(%rsi), %r13
    mov 48(%rsi), %r14
    mov 56(%rsi), %r15
    jmp *%rax /* Return without adding an extra word to the incoming stack. */
    .size UmicomKernelThreadContextSwitch,.-UmicomKernelThreadContextSwitch

    .global UmicomHostThreadBootstrap
    .type UmicomHostThreadBootstrap,@function
UmicomHostThreadBootstrap:
    mov %r12,%rdi /* Stable scheduler prepared by the host adapter. */
    mov %r13,%rsi /* Thread slot. */
    call UmicomKernelThreadStart
    ud2 /* Completion never returns here. */
    .size UmicomHostThreadBootstrap,.-UmicomHostThreadBootstrap
    .section .note.GNU-stack,"",@progbits
