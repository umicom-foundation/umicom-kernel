/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: programs/cvm/host_fixture.uc
 *
 * PURPOSE:
 *   Produce an arithmetic artifact with the hosted Umicom C compiler for
 *   execution by the production freestanding runtime inside a Kernel guest.
 *
 * AUTHOR AND ORGANISATION:
 * Sammy Hegab
 * Umicom Foundation
 *
 * LICENCE:
 * MIT
 *---------------------------------------------------------------------------*/

int main(void)
{
    return (3 + 4) * 6;
}
