# ADR-0004: Framework and Kernel Remain Independent

- Status: Accepted
- Date: 1 October 2026
- Owner: Sammy Hegab, Umicom Foundation

`umicom-kernel` is independent.

Do not add it as a submodule or build dependency of:

- `umicom-framework`
- `umicom-applications`
- any application module

`umicom-os` may later pin `umicom-kernel` for an explicit experimental/native-kernel profile after the first integration milestone.

If genuine common low-level code emerges, extract a tiny separately governed freestanding dependency rather than making the kernel depend on Framework.
