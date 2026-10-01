# ADR-0003: Native ABI with POSIX Compatibility Above It

- Status: Accepted
- Date: 1 October 2026
- Owner: Sammy Hegab, Umicom Foundation

Umicom Kernel uses a small native ABI based on handles, objects, channels, memory objects, processes, threads, events, timers and rights.

POSIX compatibility is implemented above the native ABI.

This preserves a coherent native design while enabling conventional free software to be ported.
