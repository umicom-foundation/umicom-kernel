# ADR-0002: Hybrid-Microkernel Direction

- Status: Accepted
- Date: 1 October 2026
- Owner: Sammy Hegab, Umicom Foundation

Umicom Kernel follows a hybrid-microkernel direction.

Keep privileged mechanisms small and move policy-rich/restartable services to user space where practical.

Borderline components are decided by measured security, reliability and performance rather than ideology.
