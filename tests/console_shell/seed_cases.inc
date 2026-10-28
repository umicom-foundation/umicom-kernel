/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: tests/console_shell/seed_cases.inc
 *
 * PURPOSE:
 *   Verify bounded optional program admission and ownership rollback.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

/* These cases exercise real RAMFS and scheduler owners around synthetic ELF
 * bytes. The separate serial session qualifies the native CVM envelope. */
static void SeedCase(const char *name)
{
    UmicomModelSetup();
    UmicomModelPut(64U + 2U * 56U + 40U, 8192U, 8U);
    static const char *const names[UMICOM_SHELL_SEED_LIMIT] = {
        "/bin/one.elf", "/bin/two.elf", "/bin/three.elf",
        "/bin/four.elf", "/bin/five.elf"
    };
    UmicomKernelShellImage seeds[UMICOM_SHELL_SEED_LIMIT];
    for (UmicomSize i = 0U; i < UMICOM_SHELL_SEED_LIMIT; ++i) {
        seeds[i].path = names[i];
        seeds[i].bytes = umicomModelElf;
        seeds[i].count = sizeof(umicomModelElf);
    }
    if (!strcmp(name, "seed-limit")) {
        /* The count check must run before the caller's shorter array is read. */
        CHECK(UmicomKernelConsoleShellInitialize(&shell, Output, NULL, seeds,
            UMICOM_SHELL_SEED_LIMIT + 1U) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(shell.self == NULL);
        UmicomModelNoLeaks();
        return;
    }
    if (!strcmp(name, "seed-invalid-last")) {
        seeds[UMICOM_SHELL_SEED_LIMIT - 1U].bytes = NULL;
        CHECK(UmicomKernelConsoleShellInitialize(&shell, Output, NULL, seeds,
            UMICOM_SHELL_SEED_LIMIT) == UMICOM_SHELL_INVALID_ARGUMENT);
        CHECK(shell.self == NULL);
        UmicomModelNoLeaks();
        return;
    }
    if (!strcmp(name, "seed-rollback")) {
        /* Failure at any allocation point leaves a closeable partial owner.
         * Each iteration starts with a new owner; live generations are never
         * reset to make cleanup appear successful. */
        for (int budget = 0; budget < 80; ++budget) {
            UmicomKernelConsoleShell *local = calloc(1U, sizeof(*local));
            CHECK(local != NULL);
            allocationBudget = budget;
            (void)UmicomKernelConsoleShellInitialize(local, Output, NULL, seeds,
                UMICOM_SHELL_SEED_LIMIT);
            allocationBudget = -1;
            CHECK(UmicomKernelConsoleShellClose(local) == UMICOM_SHELL_OK);
            free(local);
            UmicomModelNoLeaks();
        }
        return;
    }
    if (!strcmp(name, "seed-duplicate-last")) {
        seeds[UMICOM_SHELL_SEED_LIMIT - 1U].path = names[0];
        CHECK(UmicomKernelConsoleShellInitialize(&shell, Output, NULL, seeds,
            UMICOM_SHELL_SEED_LIMIT) != UMICOM_SHELL_OK);
        Close();
        return;
    }
    CHECK(UmicomKernelConsoleShellInitialize(&shell, Output, NULL, seeds,
        UMICOM_SHELL_SEED_LIMIT) == UMICOM_SHELL_OK);
    CHECK(Command("ls /bin") == UMICOM_SHELL_OK);
    for (UmicomSize i = 0U; i < UMICOM_SHELL_SEED_LIMIT; ++i)
        CHECK(strstr(transcript, names[i] + 5U) != NULL);
    if (!strcmp(name, "seed-run-fifth")) {
        umicomModelMode = UMICOM_MODEL_EXIT;
        CHECK(Command("run /bin/five.elf 0") == UMICOM_SHELL_OK);
        RunToPrompt();
        CHECK(shell.hasCompletion && shell.lastCompletion.state == UMICOM_USER_TASK_EXITED);
        CHECK(strstr(transcript, "state=exited") != NULL);
        CHECK(shell.descriptor == 0U);
        CHECK(Command("run /bin/one.elf 0") == UMICOM_SHELL_OK);
        RunToPrompt();
    } else {
        CHECK(!strcmp(name, "seed-five"));
    }
    Close();
}
