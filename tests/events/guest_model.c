/* Reuse the established host-only console/timer/CSR model and real scheduler
 * adapter to execute the new C acceptance orchestration. This models hardware
 * observations; it is not a RISC-V boot or a real hardware register test.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#define UmicomKernelThreadsValidateExecution UmicomKernelEventsValidateExecution
#include "../threads/guest_model.c"
