/*-----------------------------------------------------------------------------
 * Umicom native service-health example
 * File: programs/health_service/main.c
 *
 * READY is a real syscall, not a line of output the controller guesses at.
 * Successful reports suspend this exact continuation until the controller's
 * next report opportunity. The local sentinel and start counter detect a
 * restart masquerading as resumption. Only an intentional manager restart gets
 * fresh data pages and a new process identity.
 *
 * The modes silent, unready, fault and exit exercise the corresponding policy.
 * They are diagnostic workloads, not claimed disk or networking daemons.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#include "umicom/kernel/launch_abi.h"
#include "umicom/kernel/service_report_abi.h"
UmicomU64 UmicomHealthProgramReport(UmicomU64 operation, UmicomU64 sequence);
static volatile UmicomU64 umicomHealthStarts;
static volatile UmicomU64 umicomHealthWork;

static UmicomBoolean UmicomHealthEqual(const char *a, const char *b)
{
    for (UmicomSize i = 0U; i < 32U; ++i) {
        if (a[i] != b[i]) return UMICOM_FALSE;
        if (!a[i]) return UMICOM_TRUE;
    }
    return UMICOM_FALSE;
}
static _Noreturn void UmicomHealthSpin(void)
{
    /* No user yield: only the existing timer pre-emption returns control. */
    for (;;) ++umicomHealthWork;
}
UmicomU64 UmicomHealthProgramMain(UmicomU64 argc, const char *const *argv,
    const char *const *envp, const UmicomProgramLaunchInfo *info)
{
    ++umicomHealthStarts;
    /* Legacy numeric launch has no argv and is deliberately not accepted. */
    if (argc != 2U || !argv || !envp || !info || info->cookie != UMICOM_LAUNCH_COOKIE ||
        info->argumentCount != argc || !argv[0] || !argv[1] || argv[2] != 0) return 0xed01U;
    const char *const mode = argv[1];
    volatile UmicomU64 local = 0x164832U;
    if (UmicomHealthEqual(mode, "unready")) UmicomHealthSpin();
    if (UmicomHealthEqual(mode, "exit")) return 0U; /* Exit zero is not readiness. */
    if (UmicomHealthProgramReport(UMICOM_SERVICE_REPORT_READY, 1U) != UMICOM_SERVICE_REPORT_OK) return 0xed02U;
    if (UmicomHealthEqual(mode, "silent")) UmicomHealthSpin();
    if (UmicomHealthEqual(mode, "fault")) __asm__ volatile("unimp");
    UmicomU64 sequence = 2U;
    for (;;) {
        /* The Kernel does not renew slice/syscall budgets. This bounded native
         * service will eventually exhaust them even if every heartbeat succeeds. */
        if (local != 0x164832U || umicomHealthStarts != 1U) return 0xed03U;
        ++umicomHealthWork;
        if (UmicomHealthProgramReport(UMICOM_SERVICE_REPORT_HEARTBEAT, sequence) != UMICOM_SERVICE_REPORT_OK)
            return 0xed04U;
        ++sequence;
    }
}
