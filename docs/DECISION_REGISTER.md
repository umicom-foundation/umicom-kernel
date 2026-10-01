# Umicom Kernel and Umicom OS Decision Register

Date: 1 October 2026

| ID | Decision | Status |
|---|---|---|
| K-001 | Umicom OS is kernel-independent. | Accepted |
| K-002 | GNU Linux-libre is the current production kernel for the free-system distribution track. | Accepted |
| K-003 | FreeBSD is the main non-Linux portability/server-appliance proof target. | Accepted |
| K-004 | Umicom Kernel is an independent original native-kernel repository. | Accepted |
| K-005 | Umicom Kernel uses a hybrid-microkernel direction. | Accepted |
| K-006 | Umicom Kernel does not depend on the full Umicom Framework. | Accepted |
| K-007 | Framework/applications consume portable system-service contracts. | Accepted |
| K-008 | Native kernel ABI is small and handle/object/channel/capability oriented. | Accepted |
| K-009 | POSIX compatibility sits above the native ABI. | Accepted |
| K-010 | QEMU RISC-V 64 `virt` is the first kernel execution target. | Accepted |
| K-011 | x86-64 is second; ARM64 follows after common abstractions mature. | Accepted |
| K-012 | Production Linux-libre work continues while the native kernel develops in parallel. | Accepted |
| K-013 | Inspiration from other operating systems means learning from concepts/public documentation; original Umicom implementation remains the default. | Accepted |
| K-014 | Custom code is prioritised where it adds architecture, control, integration or educational value. | Accepted |
| K-015 | Mature free components are reused when rewriting mainly adds risk. | Accepted |
| K-016 | Independent recovery remains a required failure boundary. | Accepted |
| K-017 | `umicom-kernel` remains independent from Framework and Applications. | Accepted |
| K-018 | `umicom-os` may later pin the kernel only for an explicit experimental/native-kernel profile. | Accepted |
| K-019 | Shared freestanding helpers are extracted only after proven duplication. | Accepted |
| K-020 | Existing code/comments/history are preserved unless the owner approves deletion. | Accepted |

## Earlier ADR

The 2 September 2026 Umicom OS architecture decision remains historical evidence.

The new direction extends it from "Linux production + kernel research" to "kernel-independent Umicom OS, with Linux-libre as current production implementation".

Do not delete the earlier ADR. Add a note that it is extended/superseded in part.
