/* Umicom Kernel mapped-space architecture boundary.
 * Reuse the established allocation-context check rather than grow a second
 * interrupt policy. These owners require exactly the same single-hart, Bare,
 * interrupts-disabled context as the existing object caches. The gate is not
 * a lock: callers must not yield or mutate the same owner concurrently.
 * Sammy Hegab, Umicom Foundation. MIT licence. */
#include "umicom/kernel/mapped_regions.h"
#include "umicom/kernel/object_cache.h"

void UmicomRiscvExecutableSynchronize(void);
UmicomBoolean UmicomKernelMappedSpaceAccessAllowed(void)
{
    /* The existing gate also refuses outstanding managed sections/timer leases. */
    return UmicomKernelObjectCacheAccessAllowed();
}
void UmicomKernelMappedSpaceInstructionsPublish(void)
{
    /* The old loader's FENCE.I primitive is the authority for local instruction
     * visibility. Page-table construction alone does not publish written code. */
    UmicomRiscvExecutableSynchronize();
}
