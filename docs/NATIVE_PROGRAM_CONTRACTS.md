# Discover the Kernel contract from a native program

A separately loaded program should check the native interface it is about to
use. `UmicomUserContractDiscover` in `programs/support/native_contract.c`
uses the program's environment-call bridge to obtain a complete local record.
Its declaration is in `include/umicom/user/native_contract.h`.

## Read the version before the features

Operation 3, selector 0, asks for VERSION. A monitor predating this operation
returns UNKNOWN_CALL, whose value is 1. Feature bit 1 is meaningful in a newer
monitor, so interpreting that old error as a feature mask would be incorrect.
The reader therefore stops after the first query when version discovery is
unavailable.

It supports interface major 1 with any minor version, 64-bit words,
little-endian values and 4096-byte pages. It preserves unknown feature bits but
does not interpret them as authority. File routes can be compiled in while a
particular program still has no file handle.

## Follow the actual example

In `programs/diagnostic/main.c`, argument 4 selects this extra check:

```c
UmicomUserContract contract;
if (UmicomUserContractDiscover(UmicomProgramSystemCall, &contract)
        != UMICOM_USER_CONTRACT_OK)
    return 0xe007U;
```

This is an excerpt, not a complete standalone program. The Assembly bridge in
`programs/diagnostic/entry.S` places the operation in a7 and arguments in
a0-a2. The reader supplies zero for the reserved arguments. It allocates no
memory and requires no host C library.

The seven successful discovery calls consume the same invocation budget as
other calls. The diagnostic case makes ten calls in total: identity, copy,
seven discovery queries and exit. The Kernel independently checks its exit
value, call count, copied bytes and restored physical-frame accounting.

## Know what a failure preserves

The reader stages replies locally. It writes the caller's output only after all
fields pass. Missing discovery returns UNAVAILABLE; unsupported width,
byte order, page size or major version returns INCOMPATIBLE; malformed version
bits or impossible limits return INVALID_REPLY. A null bridge or output returns
INVALID_ARGUMENT without making a call. Every failure leaves output untouched.

These are typed, trusted pointers within the user program. The reader does not
make arbitrary invalid pointers safe. It does not authenticate the Kernel,
create service handles, negotiate a raw structure layout or grant permissions.

## Qualify the two different boundaries

Host checks substitute observed replies and exercise the production reader:

```powershell
Set-Location "C:\umicom\umicom-kernel"
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -S ".\tests\native_contract" -B ".\build\native-contract-host" -G Ninja -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe
cmake --build ".\build\native-contract-host" --parallel 2
ctest --test-dir ".\build\native-contract-host" --output-on-failure --no-tests=error
```

Stop on any error. These checks do not execute ECALL or simulate a processor.
After configuring and building the RISC-V preset, run the actual guest:

```powershell
ctest --preset riscv64-clang-debug -R "^kernel\.riscv64\.native_contract_client$" --output-on-failure --no-tests=error
```

The guest must emit `UMICOM_KERNEL_NATIVE_CONTRACT_CLIENT_READY`. CTest also
rejects failure markers. Follow the [user guide](USER_GUIDE.md) to retain logs.
Keep host and guest evidence separate; neither is a substitute for the other.

## Check the program's requirements before its next operation

Discovery says what the image offers. `UmicomUserContractCheck` compares that
record with what one program intends to use. It makes no environment call and
does not modify the contract or requirements.

The diagnostic program uses this local requirement record after discovery:

```c
static const UmicomUserRequirements requirements = {
    sizeof(UmicomUserRequirements), 0U, 0U, 16U, 9U, 1U
};
if (UmicomUserContractCheck(&contract, &requirements) != UMICOM_USER_READY)
    return 0xe008U;
```

Read the six fields in order:

1. `structureBytes` is the size of the known C structure.
2. `minimumMinorVersion` is zero: this program needs no newer major-1 additions.
3. `requiredFeatures` is zero: its identity and copy calls are base operations,
   not optional file or messaging routes.
4. `minimumCopyBytes` is 16, the largest individual copy it needs.
5. `callsAlreadyUsed` is nine: identity, copy and seven discovery queries.
6. `callsNeeded` is one, for the final exit. Exit must be included.

The immutable record has static storage. A large automatic aggregate can make
a freestanding compiler emit a hidden library copy during initialization.
Static storage also states that this policy does not change from call to call.

The checker distinguishes an invalid requirement, malformed contract, too-old
minor version, missing feature, insufficient copy limit and insufficient call
budget. It checks `used <= limit` before subtracting and comparing remaining
calls; adding two large unsigned counts could overflow.

A feature mask is a set of bits. Requiring FILES asks whether that route exists,
not whether the caller owns a file handle. Unknown bits are preserved and can be
compared, but the old client does not know their semantics. The actual service
must still enforce permissions and validate each request.

The structure-size rule applies only inside this native C program. A prefix
smaller than the known structure is refused; a larger size permits ignored
optional trailing fields. All known fields must be readable, aligned and
initialized. Unknown fields cannot impose requirements on an older checker.
This is not serialization, a foreign-process memory check or a new Kernel wire
protocol.

A readiness result does not reserve calls. Keep the used-call count accurate
and recheck after other work. It also does not authenticate the source of the
discovery record. The host test project covers boundaries and unchanged input;
the real loaded diagnostic checks the same implementation across an ECALL path.
