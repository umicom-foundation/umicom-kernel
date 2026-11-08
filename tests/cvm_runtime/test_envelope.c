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
                               {"unknown-selector", 13U, 0xc004U},
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
