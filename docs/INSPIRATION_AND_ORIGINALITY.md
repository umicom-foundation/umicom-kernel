# Inspiration, Originality and Reuse Policy

Study architecture, public interfaces, standards, papers, failure modes, security lessons, performance techniques and UX patterns from other operating systems.

Implement the Umicom design in original source unless an external component is deliberately selected under a compatible free licence with exact provenance.

Useful references include:

- Windows NT — HAL, object/handle model, security tokens, job/process grouping, service boundaries.
- Unix/FreeBSD — process/file/socket semantics, VFS, networking, privilege separation, jails/Capsicum concepts.
- GNU/Linux — VM, scheduling, drivers, standards compatibility and hardware engineering.
- illumos/Solaris — observability, service management and storage-integrity concepts.
- QNX/seL4 — microkernel isolation, IPC and capabilities.
- Fuchsia/Zircon — handles, channels, rights and object-based interfaces.
- GrapheneOS — attack-surface reduction, sandboxing and hardening.

Prefer original Umicom code where it creates architecture, control, integration, security, education or long-term independence.

Reuse mature free software when rewriting it mainly adds risk, especially cryptography, Unicode data/algorithms, standard codecs and compiler/toolchain infrastructure.

Every imported component records upstream, version/commit, licence, modifications, source location and reason for inclusion.
