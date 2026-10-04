/* Test-only System V callee-saved register probe. It verifies the host context
 * adapter, not the RV64 register file. Sammy Hegab, Umicom Foundation. MIT. */
    .text
    .global UmicomRiscvThreadRegistersProbe
    .type UmicomRiscvThreadRegistersProbe,@function
UmicomRiscvThreadRegistersProbe:
    push %rbx /* Preserve the caller before seeding host saved registers. */
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    sub $24,%rsp /* Align for the ordinary C call; keep the seed on our stack. */
    mov %rsi,(%rsp)
    lea 1(%rsi),%rbx
    lea 2(%rsi),%rbp
    lea 3(%rsi),%r12
    lea 4(%rsi),%r13
    lea 5(%rsi),%r14
    lea 6(%rsi),%r15
    call UmicomKernelThreadYield
    test %eax,%eax
    jnz .Lhost_probe_failed
    mov (%rsp),%r10
    lea 1(%r10),%r11
    cmp %r11,%rbx
    jne .Lhost_probe_failed
    lea 2(%r10),%r11
    cmp %r11,%rbp
    jne .Lhost_probe_failed
    lea 3(%r10),%r11
    cmp %r11,%r12
    jne .Lhost_probe_failed
    lea 4(%r10),%r11
    cmp %r11,%r13
    jne .Lhost_probe_failed
    lea 5(%r10),%r11
    cmp %r11,%r14
    jne .Lhost_probe_failed
    lea 6(%r10),%r11
    cmp %r11,%r15
    jne .Lhost_probe_failed
    mov $1,%eax
    jmp .Lhost_probe_restore
.Lhost_probe_failed:
    xor %eax,%eax
.Lhost_probe_restore:
    add $24,%rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
    .size UmicomRiscvThreadRegistersProbe,.-UmicomRiscvThreadRegistersProbe
    .section .note.GNU-stack,"",@progbits
