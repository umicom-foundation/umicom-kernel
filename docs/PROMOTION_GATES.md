# Umicom Kernel Promotion Gates

## A — Research
May claim only specific implemented/tested features and named boot targets.

## B — Experimental Umicom OS profile
Requires reproducible build, boot, recovery, memory/process/IPC, persistent storage, networking, system manager, update/install story, security/fuzzing evidence and explicit limitations.

At this point `umicom-os` may pin `umicom-kernel` for an explicit experimental profile.

## C — Supported selectable kernel
Requires stable ABI policy, multiple supported targets, reliable storage/network, driver support, performance/stress evidence, upgrade/rollback, vulnerability response and Framework native-adapter qualification.

## D — Candidate production default
Requires a new architecture decision comparing at least security, reliability, recovery, performance, power, hardware support, storage integrity, networking, application compatibility, maintainability, updates, observability and project sustainability.

Linux-libre remains production default until this gate is explicitly passed.

## Evidence rule
Host tests, compile success, QEMU boot, graphical boot and physical hardware are different evidence. Missing evidence is never promoted to PASS.
