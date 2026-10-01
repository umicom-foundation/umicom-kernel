# Portability and Umicom System Services Boundary

There are two separate contracts:

1. **Umicom Kernel Native ABI** — low-level kernel/user interface owned by `umicom-kernel`.
2. **Umicom System Services API** — portable user-space contracts owned by Umicom Framework.

Do not merge them.

Candidate Framework service families:

- Process
- Thread
- Clock/Timer
- File/Directory
- Storage
- Device
- Network
- Service/Supervision
- Session
- Identity/Principal
- Package
- Update
- Power
- System Information
- Audit/Diagnostics

Linux-specific implementation belongs in the Linux-libre adapter.

FreeBSD is deliberately used to expose accidental Linux coupling.

Windows remains a supported Framework/application platform but is not part of the Umicom OS free-system distribution.

The Umicom native adapter eventually maps Framework system services onto Umicom Kernel ABI plus native user-space system servers.
