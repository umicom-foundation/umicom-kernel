# Umicom Kernel Architecture

Status: Accepted direction  
Date: 1 October 2026  
Owner: Sammy Hegab, Umicom Foundation

## Product position

Umicom OS is the operating-system product. It is kernel-independent.

Current kernel tracks:

1. GNU Linux-libre — production free-system kernel.
2. FreeBSD — portability/server/appliance evaluation target.
3. Umicom Kernel — original native-kernel implementation.

## Layered architecture

```text
+--------------------------------------------------------------+
|                    Umicom Applications                       |
+--------------------------------------------------------------+
|                      Umicom Framework                        |
+--------------------------------------------------------------+
|                 Umicom System Services API                   |
+--------------------------------------------------------------+
|                  Platform Adapter Boundary                   |
| Linux-libre | FreeBSD | Windows dev/runtime | Umicom Native |
+--------------------------------------------------------------+
|                       Umicom Kernel                          |
| VM | Scheduler | IPC | Objects | Capabilities | HAL | ...   |
+--------------------------------------------------------------+
|                 x86-64 | ARM64 | RISC-V                     |
+--------------------------------------------------------------+
```

Framework depends downward on abstract system services. The kernel never depends upward on the full Framework.

## Kernel model

Umicom Kernel follows a hybrid-microkernel direction.

Privileged kernel mechanisms initially include:

- architecture/HAL;
- exception and interrupt entry;
- address-space and memory-object management;
- thread scheduling/context switching;
- kernel objects/handles;
- capability/right enforcement;
- IPC/channels;
- clocks/timers;
- syscall dispatch.

Policy-rich and restartable services should live in user space where practical.

Candidate user-space services:

- device service;
- storage service;
- filesystem service;
- network service;
- identity/security service;
- session service;
- package/update service;
- diagnostics service.

## Master Controller / Slave Controllers

The kernel provides mechanisms. It is not the application/system Master Controller.

A privileged user-space Umicom System Manager may coordinate bounded services using the established Master Controller / Slave Controller architecture.

## Native ABI and compatibility

The native kernel ABI is small and centres on handles, objects, rights, channels, events, memory objects, processes, threads, timers and devices.

POSIX compatibility is implemented above the native ABI so existing free software can be ported without forcing the native ABI to clone Unix.

## Framework relationship

Umicom Framework owns portable user-space system-service contracts and platform adapters.

Applications should use Framework/System Services contracts rather than calling kernel-native interfaces directly.

## Freestanding support

The kernel must not add `umicom-framework` as a dependency.

If proven duplication later justifies shared low-level code, extract a tiny separately governed freestanding dependency. Do not create this shared layer prematurely.

## Architecture order

1. QEMU RISC-V 64 `virt`
2. x86-64
3. ARM64 after common contracts mature

## Recovery

The independent Umicom OS recovery boundary remains mandatory. Native-kernel promotion requires its own minimal recovery path.

## Security direction

Use explicit handles/rights, least privilege, address-space separation, bounded IPC, checked user pointers and reviewable privilege transitions.

## Filesystem direction

Do not begin with a production filesystem.

Progress through RAMFS/initramfs, VFS, a simple persistent filesystem integration and crash/error semantics before considering an original UmicomFS.

## Promotion

Umicom Kernel is never silently promoted because it compiles or boots. Promotion requires the evidence in `PROMOTION_GATES.md`.
