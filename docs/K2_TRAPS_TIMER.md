# K2 — RISC-V Machine Traps, Exception Return and Timer Interrupt

Status: source delivery awaiting target-machine QEMU acceptance  
Base repository commit: `eb4e2dbd161067702aaaed11e2a8c34d9e7dfd06`  
Target: QEMU RISC-V 64 `virt,aclint=off`  
Implementation: C23 + minimum required RISC-V Assembly  
Framework dependency: none  
Script dependency: none

## 1. What K2 adds

K1 proved:

```text
QEMU reset ROM
    -> Umicom _start
    -> bootstrap stack/BSS
    -> UmiKernelMain()
    -> UART evidence
    -> QEMU clean exit
```

K2 preserves that complete path and adds:

```text
mtvec installation
    |
    +-- synchronous M-mode ECALL
    |       -> trap.S saves registers/CSRs
    |       -> trap.c recognises cause 11
    |       -> mepc += 4
    |       -> MRET resumes after ECALL
    |
    +-- asynchronous machine timer interrupt
            -> mtimecmp programmed
            -> mie.MTIE enabled
            -> mstatus.MIE enabled
            -> WFI
            -> trap.S saves registers/CSRs
            -> trap.c recognises interrupt cause 7
            -> timer acknowledged/disabled
            -> MRET returns
```

This is the first Umicom Kernel milestone where execution can leave the normal
instruction stream and return safely through a CPU trap.

## 2. Why traps matter

A kernel cannot provide:

- system calls;
- page-fault handling;
- hardware interrupts;
- timers;
- scheduling;
- device interrupt processing;
- debugger breakpoints;

without a trustworthy exception/interrupt entry mechanism.

K2 therefore builds the common mechanism before adding higher-level kernel
features.

## 3. RISC-V machine trap CSRs used

K2 uses these machine-mode Control and Status Registers:

| CSR | K2 purpose |
|---|---|
| `mtvec` | Address of `UmiRiscvTrapEntry` |
| `mepc` | Program counter to resume after trap |
| `mcause` | Interrupt flag + exception/interrupt code |
| `mtval` | Cause-specific diagnostic value |
| `mstatus` | Global/previous interrupt state and return privilege |
| `mie` | Per-source machine interrupt enables |
| `mhartid` | Hardware-thread identity |

The RISC-V privileged architecture defines machine timer interrupt code 7 and
M-mode environment-call exception code 11.

## 4. Why trap entry requires Assembly

Ordinary C cannot safely express:

- "save every interrupted integer register before the compiler changes it";
- direct CSR entry/return sequencing;
- `mret`;
- a trap-frame layout tied to register offsets.

Therefore `arch/riscv64/trap.S` owns mechanism.

C23 then owns cause decoding and policy in `arch/riscv64/trap.c`.

This is the project's standing rule: use Assembly where the architecture
requires it, then return to readable C23 as early as possible.

## 5. Trap frame

K2 reserves exactly 288 bytes on the interrupted stack.

It stores:

```text
x1..x31 (except immutable x0)
mepc
mstatus
mcause
mtval
padding
```

The frame remains 16-byte aligned before calling C.

C compile-time assertions verify important offsets so an accidental C layout
change cannot silently disagree with the Assembly constants.

## 6. Synchronous ECALL test

K2 executes one deliberate:

```asm
ecall
```

while running in M-mode.

Expected architectural result:

```text
interrupt flag = 0
mcause code    = 11
```

The handler increments its exception counter and advances:

```text
mepc = mepc + 4
```

because ECALL is a 32-bit SYSTEM instruction.

If `mepc` were not advanced, `mret` would execute ECALL again and trap forever.

## 7. Machine timer test

QEMU `virt` is launched explicitly with:

```text
aclint=off
```

so K2 uses the SiFive-compatible CLINT layout.

Current QEMU `virt` mapping:

```text
CLINT base        0x02000000
MSWI area         0x00004000 bytes
MTIMER base       0x02004000
mtimecmp[hart 0]  0x02004000
mtime             0x0200bff8
```

K2:

1. reads `mtime`;
2. adds 100,000 ticks (~10 ms at QEMU's 10 MHz timebase);
3. writes the absolute deadline to `mtimecmp[0]`;
4. enables `mie.MTIE`;
5. enables `mstatus.MIE`;
6. waits with `wfi`;
7. records machine-timer cause 7;
8. moves `mtimecmp` into the far future;
9. disables MTIE/global interrupt delivery;
10. returns through `mret`.

The wait loop checks the timer count before each WFI so a timer that fires
between interrupt-enable and the first WFI cannot create a lost-wakeup hang.

## 8. Expected serial transcript

Addresses and timer values will vary, but the logical records should look like:

```text
UMICOM_KERNEL_BEGIN
name=Umicom Kernel
version=0.2.0
milestone=K2
arch=riscv64
machine=qemu-virt
build=k2-riscv64-traps-timer
hart=0
dtb=0x0000000087e00000
state=booted
trap-vector=0x000000008020....
exception-test=begin
trap.exception.count=1
trap.exception.cause=11
trap.exception.mepc=0x...
trap.exception.mtval=0x...
exception-test=pass
timer-test=begin
timer.now=...
timer.deadline=...
trap.timer.count=1
trap.timer.cause=7
trap.timer.mepc=0x...
timer-test=pass
state=trap-timer-ready
K2_TRAP_TIMER_PASS
UMICOM_KERNEL_END
```

## 9. What K2 still does not claim

K2 does not yet provide:

- page tables;
- physical memory allocation;
- user mode;
- system calls as a user/kernel ABI;
- scheduler or threads;
- nested interrupts;
- external device interrupts;
- SMP;
- a dynamic device-tree parser;
- persistent storage;
- networking;
- Framework integration.

## 10. Validation boundary

A successful compile proves the source forms a valid RV64 ELF.

A successful `kernel.k1.riscv64.qemu_boot` proves the K1 path is still intact.

A successful `kernel.k2.riscv64.trap_timer` additionally proves:

- mtvec points at executable Umicom trap code;
- register/CSR save and restore did not prevent return;
- synchronous ECALL cause 11 was decoded;
- mepc adjustment allowed execution to continue;
- QEMU's real machine timer reached the CPU;
- machine timer interrupt cause 7 was decoded;
- mtimecmp acknowledgement stopped the one-shot event;
- MRET returned successfully.

It still does not prove physical hardware support.

## 11. Next roadmap milestone

K3 is physical/virtual memory foundation:

- boot-time memory boundaries;
- physical page/frame allocator;
- checked range arithmetic;
- reserved kernel/DTB regions;
- allocation/free accounting;
- overlap/double-free rejection;
- no virtual-memory/page-table claim until the physical allocator is qualified.
