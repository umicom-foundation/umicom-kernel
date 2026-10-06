/*-----------------------------------------------------------------------------
 * Umicom Kernel native service-report contract
 * File: include/umicom/kernel/service_report_abi.h
 *
 * A report is an explicit statement made by the managed process itself. Merely
 * loading an ELF or printing text does not make that process ready. The Kernel
 * chooses the health interval; the process cannot buy an unlimited lease.
 *
 * a7 selects SERVICE_REPORT, a0 is READY or HEARTBEAT, and a1 is a sequence
 * starting at one. a2 and a3 are reserved and must be zero. a0 returns status.
 * A successful report parks this invocation until the controller's next report
 * opportunity. The same ECALL returns after resumption; no user polling is needed.
 * This is a native Umicom contract, not a POSIX interface.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/
#ifndef UMICOM_KERNEL_SERVICE_REPORT_ABI_H
#define UMICOM_KERNEL_SERVICE_REPORT_ABI_H
#define UMICOM_USER_CALL_SERVICE_REPORT 80
#define UMICOM_SERVICE_REPORT_READY 1
#define UMICOM_SERVICE_REPORT_HEARTBEAT 2
#define UMICOM_SERVICE_REPORT_OK 0
#define UMICOM_SERVICE_REPORT_UNBOUND 1
#define UMICOM_SERVICE_REPORT_BAD_OPERATION 2
#define UMICOM_SERVICE_REPORT_BAD_SEQUENCE 3
#define UMICOM_SERVICE_REPORT_BAD_STATE 4
#define UMICOM_SERVICE_REPORT_TOO_LATE 5
#endif /* UMICOM_KERNEL_SERVICE_REPORT_ABI_H */
