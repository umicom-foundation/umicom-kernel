/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/cvm_runtime/test_envelope.c
 *
 * PURPOSE:
 *   Check the actual guest entry function against the canonical runtime before
 * separate user-mode qualification.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

#include "umicom/kernel/types.h"
#include <stdio.h>
#include <string.h>
UmicomU64 UmicomProgramMain(UmicomU64 argument);
typedef struct Case {
  const char *name;
  UmicomU64 selector, expected;
} Case;
int main(int argc, char **argv) {
  static const Case cases[] = {{"arithmetic", 0U, 42U},
                               {"fuel", 1U, 0xc001U},
                               {"fault", 2U, 0xc002U},
                               {"reserved-flags", 3U, 0xc003U},
                               {"reserved-selector", 4U, 0xc004U},
                               {"trace", 5U, 0xc005U},
                               {"trace-fuel", 6U, 0xc006U},
                               {"trace-fault", 7U, 0xc007U},
                               {"trace-capacity", 8U, 0xc008U},
                               {"comparison-true", 9U, 0xc009U},
                               {"comparison-false", 10U, 0xc00aU},
                               {"comparison-version", 11U, 0xc00bU},
                               {"comparison-fuel", 12U, 0xc00cU},
                               /* Selector 13 became profile discovery. Retain
                                * the former unknown-selector expectation for
                                * review; 31 is outside the current range. */
#if 0
                               {"unknown-selector", 13U, 0xc004U},
#endif
                               {"profile", 13U, 0xc00dU},
                               {"locals", 14U, 0xc00eU},
                               {"locals-isolation", 15U, 0xc00fU},
                               {"locals-fuel", 16U, 0xc010U},
                               {"locals-version", 17U, 0xc011U},
                               {"locals-index", 18U, 0xc012U},
                               {"branch-skip", 19U, 0xc013U},
                               {"branch-fault", 20U, 0xc014U},
                               {"branch-fuel", 21U, 0xc015U},
                               {"branch-version", 22U, 0xc016U},
                               {"branch-initialization", 23U, 0xc017U},
                               {"branch-backward", 24U, 0xc018U},
                               {"branch-stack", 25U, 0xc019U},
                               {"conditional-false", 26U, 0xc01aU},
                               {"conditional-true", 27U, 0xc01bU},
                               {"conditional-fuel", 28U, 0xc01cU},
                               {"conditional-join", 29U, 0xc01dU},
                               {"conditional-version", 30U, 0xc01eU},
                               /* Preserve the prior unknown selector; 31 now has a defined contract. */
#if 0
                               {"unknown-selector", 31U, 0xc004U},
#endif
                               {"remainder-positive",31U,0xc01fU},
                               {"remainder-negative",32U,0xc020U},
                               {"remainder-zero",33U,0xc021U},
                               {"remainder-overflow",34U,0xc022U},
                               {"remainder-version",35U,0xc023U},
                               {"remainder-fuel",36U,0xc024U},
                               {"remainder-operand",37U,0xc025U},

                               /* Preserve the former unknown selector; it now checks an endpoint. */
#if 0
                               {"unknown-selector",38U,0xc004U},
#endif
                               {"complement-minimum",38U,0xc026U},
                               {"complement-maximum",39U,0xc027U},
                               {"complement-zero",40U,0xc028U},
                               {"complement-fuel",41U,0xc029U},
                               {"complement-wire",42U,0xc02aU},
                               /* Preserve the old unknown selector; it now checks AND. */
#if 0
                               {"unknown-selector",43U,0xc004U},
#endif
                               {"bitwise-and",43U,0xc02bU},
                               {"bitwise-xor",44U,0xc02cU},
                               {"bitwise-or",45U,0xc02dU},
                               {"bitwise-fuel",46U,0xc02eU},
                               {"bitwise-old-version",47U,0xc02fU},
                               {"bitwise-operand",48U,0xc030U},
                               {"bitwise-profile",49U,0xc031U},
                               {"bitwise-minimum",50U,0xc032U},
                               {"bitwise-canonical",51U,0xc033U},
                               {"unknown-selector",52U,0xc004U},

                               {"maximum-selector", (UmicomU64)-1, 0xc004U}};
  if (argc != 2)
    return 2;
  for (size_t i = 0U; i < sizeof cases / sizeof cases[0]; ++i) {
    if (strcmp(argv[1], cases[i].name) != 0)
      continue;
    const UmicomU64 result = UmicomProgramMain(cases[i].selector);
    if (result != cases[i].expected) {
      fprintf(stderr, "%s: expected %llu, received %llu\n", cases[i].name,
              (unsigned long long)cases[i].expected,
              (unsigned long long)result);
      return 1;
    }
    return 0;
  }
  return 2;
}
