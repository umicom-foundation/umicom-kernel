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

## Discover the current native contract

Native programs can query the experimental Kernel ABI version and compiled
service routes through the register-only discovery operation. See
[Native ABI discovery](NATIVE_ABI_DISCOVERY.md) for the exact selectors, refusal
rules, examples and host/guest checks. Service presence does not grant access.

The Framework system-services adapter remains a separate responsibility.
Discovery does not supply that adapter or change the existing fixed-size native
message, file and launch records.
