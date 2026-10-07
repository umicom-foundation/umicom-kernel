/*-----------------------------------------------------------------------------
 * Umicom Kernel — explicit writable VirtIO lease qualification.
 * Reuse the original register/reset/allocator model with zero-default hooks.
 * Separate volatile and flushed media make completion and durability distinct.
 * All media are disposable host arrays; no packaged disk is written here.
 * These checks execute the real C driver, not RISC-V instructions or QEMU.
 * Sammy Hegab, Umicom Foundation. MIT licence.
 *---------------------------------------------------------------------------*/
#define main UmicomReadOnlyBlockRegressionEntry
#include "../virtio_block/virtio_block_tests.c"
#undef main

#define MEDIA_BYTES (128U * 512U)
static UmicomU8 visibleMedia[MEDIA_BYTES], durableMedia[MEDIA_BYTES], initialMedia[MEDIA_BYTES];
static UmicomU8 inputBytes[4096], inputSnapshot[4096];
static UmicomKernelBlockMutationOutcome outcome;
static UmicomKernelBlockMutationOutcome *activeOutcome;
static UmicomU32 readRequests, writeRequests, flushRequests, publicationObservations;
static UmicomBoolean changeCallerAtCompletion, partialWrite, earlyCompletion, earlyCompleted;
static UmicomBoolean reenterAllowed;
static UmicomU32 allowedReentryCalls;
static UmicomBoolean finalClockArmed, finalClockRollback, finalClockInjected;
static UmicomU32 finalClockAfterNotification;
static UmicomU64 lastClockReturned, finalClockStart, finalClockAtInjection;

static void BytesEqual(const void *first, const void *second, UmicomSize bytes)
{
    CHECK(memcmp(first, second, (size_t)bytes) == 0);
}
static void ZeroBytes(const void *memory, UmicomSize bytes)
{
    const UmicomU8 *p = memory;
    for (UmicomSize i = 0U; i < bytes; ++i) CHECK(p[i] == 0U);
}
static void WriteCompletion(void)
{
    CHECK(model.desc && model.avail && model.used);
    const UmicomU16 used = *(const UmicomU16 *)(model.used + 2U);
    const UmicomU16 available = *(const UmicomU16 *)(model.avail + 2U);
    if (earlyCompleted && available == used) { model.pending = UMICOM_FALSE; return; }
    CHECK(available == (UmicomU16)(used + 1U));
    CHECK(*(const UmicomU16 *)model.avail == 1U);
    CHECK(*(const UmicomU16 *)(model.avail + 4U + 2U * (used % 8U)) == 0U);
    const UmicomAddress header = (UmicomAddress)*(const UmicomU64 *)model.desc;
    CHECK(header == model.desc + UMICOM_VIRTIO_HEADER_OFFSET);
    CHECK(*(const UmicomU32 *)(model.desc + 8U) == 16U);
    CHECK(*(const UmicomU16 *)(model.desc + 12U) == 1U && *(const UmicomU16 *)(model.desc + 14U) == 1U);
    const UmicomU32 command = *(const UmicomU32 *)header;
    CHECK(*(const UmicomU32 *)(header + 4U) == 0U);
    const UmicomU64 sector = *(const UmicomU64 *)(header + 8U);
    UmicomAddress status;
    UmicomU32 usedBytes = 1U;
    if (command == 4U) {
        CHECK(sector == 0U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        CHECK(*(const UmicomU32 *)(model.desc + 24U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == 2U && *(const UmicomU16 *)(model.desc + 30U) == 0U);
        CHECK(activeOutcome && *activeOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
        ++flushRequests;
        if (model.result == 0U) memcpy(durableMedia, visibleMedia, sizeof(durableMedia));
    } else {
        CHECK(command == 0U || command == 1U);
        const UmicomAddress data = (UmicomAddress)*(const UmicomU64 *)(model.desc + 16U);
        const UmicomU32 bytes = *(const UmicomU32 *)(model.desc + 24U);
        CHECK(bytes && bytes <= 4096U && bytes % 512U == 0U);
        CHECK(sector < model.capacity && bytes / 512U <= model.capacity - sector);
        CHECK(model.capacity == 128U);
        CHECK(data == domain.slots[0].dataFrame && data != model.desc);
        CHECK(data >= (UmicomAddress)ram && data + 4096U <= (UmicomAddress)ram + sizeof(ram));
        CHECK(*(const UmicomU16 *)(model.desc + 28U) == (command == 0U ? 3U : 1U));
        CHECK(*(const UmicomU16 *)(model.desc + 30U) == 2U);
        status = (UmicomAddress)*(const UmicomU64 *)(model.desc + 32U);
        CHECK(*(const UmicomU32 *)(model.desc + 40U) == 1U);
        CHECK(*(const UmicomU16 *)(model.desc + 44U) == 2U && *(const UmicomU16 *)(model.desc + 46U) == 0U);
        if (command == 0U) {
            ++readRequests; usedBytes = bytes + 1U;
            memcpy((void *)data, visibleMedia + sector * 512U, bytes);
        } else {
            CHECK(activeOutcome && *activeOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
            CHECK(domain.slots[0].needsFlush);
            CHECK(data != (UmicomAddress)inputBytes);
            if (changeCallerAtCompletion) {
                memset(inputBytes, 0x5e, sizeof(inputBytes));
                BytesEqual((const void *)data, inputSnapshot, bytes);
            }
            ++writeRequests;
            /* Even a valid error status can follow a partial physical write. */
            memcpy(visibleMedia + sector * 512U, (const void *)data, partialWrite ? bytes / 2U : bytes);
        }
    }
    CHECK(status == model.desc + UMICOM_VIRTIO_RESULT_OFFSET);
    if (!model.omitResult) *(UmicomU8 *)status = (UmicomU8)model.result;
    const UmicomAddress element = model.used + 4U + 8U * (used % 8U);
    *(UmicomU32 *)element = model.usedId;
    *(UmicomU32 *)(element + 4U) = model.usedLength == 0xffffffffU ? usedBytes : model.usedLength;
    atomic_thread_fence(memory_order_seq_cst);
    *(volatile UmicomU16 *)(model.used + 2U) = (UmicomU16)(used + model.usedDelta);
    model.registers[UMICOM_VIRTIO_INTERRUPT_STATUS / 4U] |= 1U;
    model.pending = UMICOM_FALSE;
}
static void PublicationBarrier(void *context)
{
    Barrier(context);
    if (!model.avail || !activeOutcome || domain.slots[0].state != UMICOM_BLOCK_READY) return;
    const UmicomU16 available = *(const UmicomU16 *)(model.avail + 2U);
    /* Compare against the driver's prior consumed index. A forged device used
     * index alone is a preflight fault, not publication of a new request. */
    if (available == domain.slots[0].usedIndex) return;
    CHECK(*activeOutcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    ++publicationObservations;
    if (earlyCompletion && !earlyCompleted) {
        /* The device may consume an exposed ring before receiving a doorbell. */
        CHECK(model.notifications == 0U);
        WriteCompletion(); earlyCompleted = UMICOM_TRUE;
    }
}
static UmicomBoolean GuardedAllowed(void *context)
{
    CHECK(context == &model);
    if (reenterAllowed) {
        CHECK(domain.busy);
        ++allowedReentryCalls;
        UmicomKernelBlockInfo info = {0};
        CHECK(UmicomKernelBlockProbe(&domain, 0U, &info) == UMICOM_BLOCK_BUSY);
    }
    return model.allowed;
}
static UmicomU64 MutationClock(void *context)
{
    const UmicomU64 now = Clock(context);
    if (finalClockArmed && !finalClockInjected && !model.pending &&
        model.notifications > finalClockAfterNotification) {
        finalClockInjected = UMICOM_TRUE;
        if (finalClockRollback) model.now = lastClockReturned ? lastClockReturned - 1U : 0U;
        else model.now += domain.slots[0].timeoutTicks;
        finalClockAtInjection = model.now;
        lastClockReturned = model.now;
        return model.now;
    }
    if (finalClockArmed && model.notifications == finalClockAfterNotification) finalClockStart = now;
    lastClockReturned = now;
    return now;
}
static void WritableStart(void)
{
    Start();
    for (UmicomSize i = 0U; i < sizeof(visibleMedia); ++i)
        initialMedia[i] = UmicomBlockFixtureByte(i / 512U, i % 512U);
    memcpy(visibleMedia, initialMedia, sizeof(visibleMedia));
    memcpy(durableMedia, initialMedia, sizeof(durableMedia));
    for (UmicomSize i = 0U; i < sizeof(inputBytes); ++i) inputBytes[i] = (UmicomU8)(i * 17U + 0x83U);
    memcpy(inputSnapshot, inputBytes, sizeof(inputSnapshot));
    model.featuresLow &= ~UMICOM_VIRTIO_READ_ONLY;
    model.featuresLow |= UMICOM_VIRTIO_FEATURE_FLUSH;
    model.expectedDriverLow = UMICOM_VIRTIO_FEATURE_FLUSH;
    model.completeRequest = WriteCompletion;
    domain.operations.barrier = PublicationBarrier;
    domain.operations.allowed = GuardedAllowed;
    domain.operations.clock = MutationClock;
    outcome = UMICOM_BLOCK_COMPLETED; activeOutcome = 0;
    readRequests = 0U; writeRequests = 0U; flushRequests = 0U; publicationObservations = 0U;
    changeCallerAtCompletion = UMICOM_FALSE; partialWrite = UMICOM_FALSE;
    earlyCompletion = UMICOM_FALSE; earlyCompleted = UMICOM_FALSE;
    reenterAllowed = UMICOM_FALSE; allowedReentryCalls = 0U;
    finalClockArmed = UMICOM_FALSE; finalClockRollback = UMICOM_FALSE; finalClockInjected = UMICOM_FALSE;
    finalClockAfterNotification = 0U;
    lastClockReturned = 0U; finalClockStart = 0U; finalClockAtInjection = 0U;
}
static UmicomKernelBlockHandle WritableOpen(void)
{
    UmicomKernelBlockHandle handle = 0U;
    CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_OK && handle);
    CHECK(Allocated() == 2U && domain.slots[0].writable);
    CHECK(!domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    CHECK(model.driverLow == UMICOM_VIRTIO_FEATURE_FLUSH && model.driverHigh == 1U);
    return handle;
}
static UmicomKernelBlockStatus WriteAt(UmicomKernelBlockHandle handle, UmicomU64 sector, UmicomSize sectors)
{
    outcome = UMICOM_BLOCK_COMPLETED; activeOutcome = &outcome;
    const UmicomKernelBlockStatus status = UmicomKernelBlockWrite(&domain, handle,
        sector, sectors, inputBytes, sizeof(inputBytes), &outcome);
    activeOutcome = 0; return status;
}
static UmicomKernelBlockStatus Flush(UmicomKernelBlockHandle handle)
{
    outcome = UMICOM_BLOCK_NOT_SUBMITTED; activeOutcome = &outcome;
    const UmicomKernelBlockStatus status = UmicomKernelBlockFlush(&domain, handle, &outcome);
    activeOutcome = 0; return status;
}
static void MediaUnchanged(void)
{
    CHECK(model.notifications == 0U);
    CHECK(writeRequests == 0U && flushRequests == 0U);
    BytesEqual(visibleMedia, initialMedia, sizeof(visibleMedia));
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
    BytesEqual(inputBytes, inputSnapshot, sizeof(inputBytes));
}
static void Unsubmitted(void) { CHECK(outcome == UMICOM_BLOCK_NOT_SUBMITTED); MediaUnchanged(); }
static void OwnershipRefused(void) { CHECK(outcome == UMICOM_BLOCK_COMPLETED); MediaUnchanged(); }
static void Admission(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    UmicomKernelBlockInfo info = {0};
    CHECK(UmicomKernelBlockProbe(&domain, 0U, &info) == UMICOM_BLOCK_OK);
    CHECK(info.writable && !info.needsFlush && !info.writeUncertain && info.heldFrames == 2U);
    CHECK(strcmp(UmicomKernelBlockStatusName(UMICOM_BLOCK_READ_ONLY), "unknown-block-status") != 0);
    Close(handle);
}
static void ReadOnlyAdmission(void)
{
    model.featuresLow |= UMICOM_VIRTIO_READ_ONLY;
    UmicomKernelBlockHandle handle = 0U;
    CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_READ_ONLY);
    CHECK(!handle && Allocated() == 0U && model.notifications == 0U);
    model.expectedDriverLow = 0U; model.completeRequest = 0;
    handle = Open();
    CHECK(WriteAt(handle, 1U, 1U) == UMICOM_BLOCK_READ_ONLY); Unsubmitted();
    CHECK(Flush(handle) == UMICOM_BLOCK_READ_ONLY); Unsubmitted(); Close(handle);
    WritableStart(); handle = 0U;
    CHECK(UmicomKernelBlockOpen(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_WRITABLE_DEVICE);
    CHECK(!handle && Allocated() == 0U && model.notifications == 0U);
}
static void OpenFailure(const char *name)
{
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_REQUIRED_FEATURE;
    if (!strcmp(name, "missing_flush")) model.featuresLow &= ~UMICOM_VIRTIO_FEATURE_FLUSH;
    else if (!strcmp(name, "missing_modern")) model.featuresHigh &= ~1U;
    else if (!strcmp(name, "refused_features")) { model.refuseFeatures = UMICOM_TRUE; expected = UMICOM_BLOCK_FEATURE_REFUSED; }
    else if (!strcmp(name, "foreign_driver")) { model.registers[UMICOM_VIRTIO_STATUS / 4U] = 15U; expected = UMICOM_BLOCK_ALREADY_ACTIVE; }
    else CHECK(0);
    UmicomKernelBlockHandle handle = 0U;
    CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &handle) == expected);
    CHECK(!handle && Allocated() == 0U && model.notifications == 0U);
    if (expected == UMICOM_BLOCK_ALREADY_ACTIVE) CHECK(model.writes == 0U && model.resets == 0U);
}
static void FailedOpenRetained(void)
{
    model.refuseDriver = UMICOM_TRUE; model.stickAfterDriver = UMICOM_TRUE;
    UmicomKernelBlockHandle handle = 0U;
    CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_RESET_PENDING);
    CHECK(handle && domain.slots[0].exposed && Allocated() == 2U && model.freeCalls == 0U);
    CHECK(WriteAt(handle, 1U, 1U) == UMICOM_BLOCK_BAD_STATE); Unsubmitted();
    model.stuckReset = UMICOM_FALSE; Close(handle);
}
static void WriteBounds(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    static const struct { UmicomU64 sector; UmicomSize count; } invalid[] = {
        {0U, 0U}, {0U, 9U}, {0U, ~(UmicomSize)0U}, {128U, 1U}, {127U, 2U}, {~(UmicomU64)0U, 1U}
    };
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(WriteAt(handle, invalid[i].sector, invalid[i].count) != UMICOM_BLOCK_OK); Unsubmitted();
    }
    outcome = UMICOM_BLOCK_COMPLETED;
    CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, inputBytes, 511U, &outcome) == UMICOM_BLOCK_RANGE);
    Unsubmitted();
    outcome = UMICOM_BLOCK_COMPLETED;
    CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, 0, 512U, &outcome) == UMICOM_BLOCK_INVALID_ARGUMENT);
    OwnershipRefused();
    outcome = UMICOM_BLOCK_COMPLETED;
    CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U,
        (const void *)(~(UmicomUIntPtr)0U - 255U), 512U, &outcome) != UMICOM_BLOCK_OK);
    OwnershipRefused(); Close(handle);
}
static void SourceOwnership(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    const void *sources[] = {&domain, (const void *)domain.slots[0].queueFrame,
        (const void *)domain.slots[0].dataFrame};
    for (size_t i = 0U; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        outcome = UMICOM_BLOCK_COMPLETED;
        CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, sources[i], 512U, &outcome) == UMICOM_BLOCK_INVALID_ARGUMENT);
        OwnershipRefused();
    }
    UmicomAddress other = 0U;
    CHECK(UmicomKernelPhysicalMemoryAllocateFrame(&other) == UMICOM_KERNEL_MEMORY_OK);
    domain.count = 2U; domain.slots[1].dataFrame = other;
    outcome = UMICOM_BLOCK_COMPLETED;
    CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, (const void *)other, 512U, &outcome) == UMICOM_BLOCK_INVALID_ARGUMENT);
    OwnershipRefused(); domain.slots[1].dataFrame = 0U; domain.count = 1U;
    CHECK(UmicomKernelBlockClose(&domain, handle) == UMICOM_BLOCK_OK);
    memset((void *)other, 0, 4096U);
    CHECK(UmicomKernelPhysicalMemoryFreeFrame(other) == UMICOM_KERNEL_MEMORY_OK && Allocated() == 0U);
}

static void OutcomeOwnership(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    UmicomKernelBlockDomain savedDomain = domain;
    UmicomU8 savedQueue[4096], savedData[4096];
    memcpy(savedQueue, (const void *)domain.slots[0].queueFrame, sizeof(savedQueue));
    memcpy(savedData, (const void *)domain.slots[0].dataFrame, sizeof(savedData));
    UmicomKernelBlockMutationOutcome *outputs[] = {0,
        (UmicomKernelBlockMutationOutcome *)(void *)&domain,
        (UmicomKernelBlockMutationOutcome *)domain.slots[0].queueFrame,
        (UmicomKernelBlockMutationOutcome *)domain.slots[0].dataFrame,
        (UmicomKernelBlockMutationOutcome *)(void *)inputBytes,
        (UmicomKernelBlockMutationOutcome *)(void *)(inputBytes + 1U),
        (UmicomKernelBlockMutationOutcome *)(~(UmicomUIntPtr)0U - 3U)};
    for (size_t i = 0U; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, inputBytes,
            sizeof(inputBytes), outputs[i]) == UMICOM_BLOCK_INVALID_ARGUMENT);
        BytesEqual(&domain, &savedDomain, sizeof(domain));
        BytesEqual((const void *)domain.slots[0].queueFrame, savedQueue, sizeof(savedQueue));
        BytesEqual((const void *)domain.slots[0].dataFrame, savedData, sizeof(savedData));
        MediaUnchanged();
    }
    /* Flush has no input, so only owner/DMA/null/misalignment/wrap apply. */
    for (size_t i = 0U; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        if (i == 4U) continue;
        CHECK(UmicomKernelBlockFlush(&domain, handle, outputs[i]) == UMICOM_BLOCK_INVALID_ARGUMENT);
        BytesEqual(&domain, &savedDomain, sizeof(domain)); MediaUnchanged();
    }
    /* Even the unused tail of the declared source extent cannot contain output. */
    _Alignas(UmicomKernelBlockMutationOutcome) UmicomU8 combined[1024];
    memset(combined, 0x71, sizeof(combined));
    CHECK(UmicomKernelBlockWrite(&domain, handle, 1U, 1U, combined, sizeof(combined),
        (UmicomKernelBlockMutationOutcome *)(void *)(combined + 768U)) == UMICOM_BLOCK_INVALID_ARGUMENT);
    for (UmicomSize i = 0U; i < sizeof(combined); ++i) CHECK(combined[i] == 0x71U);
    MediaUnchanged(); Close(handle);
}
static void WriteUnsafeAndReentry(void)
{
    reenterAllowed = UMICOM_TRUE;
    const UmicomKernelBlockHandle handle = WritableOpen();
    model.allowed = UMICOM_FALSE;
    CHECK(WriteAt(handle, 1U, 1U) == UMICOM_BLOCK_UNSAFE_CONTEXT);
    CHECK(outcome == UMICOM_BLOCK_COMPLETED); MediaUnchanged();
    CHECK(!domain.busy && domain.slots[0].state == UMICOM_BLOCK_READY);
    model.allowed = UMICOM_TRUE; model.reenter = UMICOM_TRUE;
    CHECK(WriteAt(handle, 1U, 1U) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(model.reentryResult == UMICOM_BLOCK_BUSY);
    CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    Close(handle); CHECK(allowedReentryCalls >= 4U);
}
static void WriteStaleAndExclusive(void)
{
    const UmicomKernelBlockHandle first = WritableOpen();
    UmicomKernelBlockHandle other = 0U;
    const UmicomU32 writes = model.writes;
    CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &other) == UMICOM_BLOCK_BUSY);
    CHECK(UmicomKernelBlockOpen(&domain, 0U, 32U, &other) == UMICOM_BLOCK_BUSY);
    CHECK(!other && model.writes == writes); Close(first);
    const UmicomKernelBlockHandle second = WritableOpen(); CHECK(first != second);
    CHECK(WriteAt(first, 1U, 1U) == UMICOM_BLOCK_INVALID_HANDLE); Unsubmitted();
    CHECK(Flush(first) == UMICOM_BLOCK_INVALID_HANDLE); Unsubmitted(); Close(second);
}
static void WriteSuccess(const char *name)
{
    if (!strcmp(name, "noncontiguous_dma"))
        CHECK(UmicomKernelPhysicalMemoryReserveRange((UmicomAddress)ram + 4096U, 4096U) == UMICOM_KERNEL_MEMORY_OK);
    const UmicomKernelBlockHandle handle = WritableOpen();
    UmicomU64 sector = 3U; UmicomSize count = 1U;
    if (!strcmp(name, "last_sector")) sector = 127U;
    if (!strcmp(name, "multi_sector") || !strcmp(name, "noncontiguous_dma")) count = 8U;
    if (!strcmp(name, "owned_input_snapshot")) changeCallerAtCompletion = UMICOM_TRUE;
    if (!strcmp(name, "noncontiguous_dma")) CHECK(domain.slots[0].dataFrame != domain.slots[0].queueFrame + 4096U);
    CHECK(WriteAt(handle, sector, count) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(writeRequests == 1U && !flushRequests && publicationObservations);
    BytesEqual(visibleMedia + sector * 512U, inputSnapshot, count * 512U);
    BytesEqual(visibleMedia, initialMedia, sector * 512U);
    BytesEqual(visibleMedia + (sector + count) * 512U, initialMedia + (sector + count) * 512U,
        sizeof(visibleMedia) - (sector + count) * 512U);
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
    ZeroBytes((const void *)domain.slots[0].dataFrame, 4096U);
    CHECK(domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    UmicomKernelBlockInfo info = {0}; CHECK(UmicomKernelBlockProbe(&domain, 0U, &info) == UMICOM_BLOCK_OK);
    CHECK(info.writable && info.needsFlush && !info.writeUncertain && info.requests == 1U);
    if (!changeCallerAtCompletion) BytesEqual(inputBytes, inputSnapshot, sizeof(inputBytes));
    Close(handle); CHECK(flushRequests == 0U);
}
static void ReadbackAndFlush(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    CHECK(WriteAt(handle, 7U, 8U) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(UmicomKernelBlockRead(&domain, handle, 7U, 8U, resultBytes, sizeof(resultBytes)) == UMICOM_BLOCK_OK);
    BytesEqual(resultBytes, inputSnapshot, sizeof(resultBytes));
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
    CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(!domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    BytesEqual(durableMedia, visibleMedia, sizeof(durableMedia));
    CHECK(writeRequests == 1U && readRequests == 1U && flushRequests == 1U);
    Close(handle); CHECK(flushRequests == 1U);
}
static void BeforeNotify(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen(); earlyCompletion = UMICOM_TRUE;
    CHECK(WriteAt(handle, 2U, 1U) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(earlyCompleted && publicationObservations && model.notifications == 1U && writeRequests == 1U);
    BytesEqual(visibleMedia + 1024U, inputSnapshot, 512U); Close(handle);
}
static void PartialError(UmicomBoolean unsupported)
{
    const UmicomKernelBlockHandle handle = WritableOpen(); partialWrite = UMICOM_TRUE;
    model.result = unsupported ? 2U : 1U;
    CHECK(WriteAt(handle, 4U, 1U) == (unsupported ? UMICOM_BLOCK_UNSUPPORTED_REQUEST : UMICOM_BLOCK_IO_ERROR));
    CHECK(outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED && domain.slots[0].state == UMICOM_BLOCK_READY);
    CHECK(domain.slots[0].needsFlush && domain.slots[0].writeUncertain);
    BytesEqual(visibleMedia + 2048U, inputSnapshot, 256U);
    BytesEqual(visibleMedia + 2304U, initialMedia + 2304U, 256U);
    model.result = 0U; partialWrite = UMICOM_FALSE;
    CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(!domain.slots[0].needsFlush && domain.slots[0].writeUncertain);
    BytesEqual(durableMedia, visibleMedia, sizeof(durableMedia));
    CHECK(WriteAt(handle, 8U, 1U) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(domain.slots[0].needsFlush && domain.slots[0].writeUncertain);
    Close(handle); CHECK(domain.slots[0].writeUncertain);
}
static void SubmittedFault(const char *name, UmicomBoolean flush)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    if (flush) CHECK(WriteAt(handle, 2U, 1U) == UMICOM_BLOCK_OK);
    UmicomKernelBlockStatus expected = UMICOM_BLOCK_MALFORMED_COMPLETION;
    if (!strcmp(name, "timeout")) { model.noCompletion = UMICOM_TRUE; expected = UMICOM_BLOCK_TIMEOUT; }
    else if (!strcmp(name, "stopped_clock")) { model.noCompletion = UMICOM_TRUE; model.step = 0U; expected = UMICOM_BLOCK_TIMEOUT; }
    else if (!strcmp(name, "backward_clock")) { model.noCompletion = UMICOM_TRUE; model.backwardClock = UMICOM_TRUE; model.now = 1000U; expected = UMICOM_BLOCK_CLOCK_ERROR; }
    else if (!strcmp(name, "used_id")) model.usedId = 1U;
    else if (!strcmp(name, "used_zero")) model.usedLength = 0U;
    else if (!strcmp(name, "used_long")) model.usedLength = 2U;
    else if (!strcmp(name, "used_jump")) model.usedDelta = 2U;
    else if (!strcmp(name, "status_unwritten")) model.omitResult = UMICOM_TRUE;
    else if (!strcmp(name, "status_unknown")) model.result = 3U;
    else if (!strcmp(name, "configuration_change")) { model.changeConfig = UMICOM_TRUE; expected = UMICOM_BLOCK_CONFIG_CHANGED; }
    else if (!strcmp(name, "device_reset")) { model.needsReset = UMICOM_TRUE; expected = UMICOM_BLOCK_DEVICE_ERROR; }
    else CHECK(0);
    CHECK((flush ? Flush(handle) : WriteAt(handle, 2U, 1U)) == expected);
    CHECK(outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
    CHECK(domain.slots[0].state == UMICOM_BLOCK_FAULTED && !domain.slots[0].exposed);
    CHECK(domain.slots[0].needsFlush);
    if (!flush) CHECK(domain.slots[0].writeUncertain);
    const UmicomU32 notifications = model.notifications;
    CHECK(WriteAt(handle, 4U, 1U) == UMICOM_BLOCK_BAD_STATE && outcome == UMICOM_BLOCK_NOT_SUBMITTED);
    CHECK(model.notifications == notifications); Close(handle);
}
static void PreflightFaults(void)
{
    for (unsigned kind = 0U; kind < 3U; ++kind) {
        WritableStart(); const UmicomKernelBlockHandle handle = WritableOpen();
        UmicomKernelBlockStatus expected;
        if (kind == 0U) { model.registers[UMICOM_VIRTIO_STATUS / 4U] |= 64U; expected = UMICOM_BLOCK_DEVICE_ERROR; }
        else if (kind == 1U) { ++model.registers[UMICOM_VIRTIO_CONFIG_GENERATION / 4U]; expected = UMICOM_BLOCK_CONFIG_CHANGED; }
        else { *(UmicomU16 *)(model.used + 2U) = 1U; expected = UMICOM_BLOCK_MALFORMED_COMPLETION; }
        CHECK(WriteAt(handle, 1U, 1U) == expected); Unsubmitted();
        CHECK(!domain.slots[0].needsFlush && !domain.slots[0].writeUncertain); Close(handle);
    }
}
static void FlushErrors(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    CHECK(WriteAt(handle, 3U, 1U) == UMICOM_BLOCK_OK);
    for (UmicomU32 result = 1U; result <= 2U; ++result) {
        model.result = result;
        CHECK(Flush(handle) == (result == 1U ? UMICOM_BLOCK_IO_ERROR : UMICOM_BLOCK_UNSUPPORTED_REQUEST));
        CHECK(outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED && domain.slots[0].needsFlush);
        CHECK(domain.slots[0].state == UMICOM_BLOCK_READY && !domain.slots[0].writeUncertain);
        BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
    }
    model.result = 0U;
    CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(!domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    BytesEqual(durableMedia, visibleMedia, sizeof(durableMedia)); Close(handle);
}
static void LateCompletionAndReset(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    model.stuckReset = UMICOM_TRUE; model.noCompletion = UMICOM_TRUE;
    CHECK(WriteAt(handle, 5U, 1U) == UMICOM_BLOCK_RESET_PENDING);
    CHECK(outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED && domain.slots[0].lastError == UMICOM_BLOCK_TIMEOUT);
    CHECK(domain.slots[0].exposed && domain.slots[0].writeUncertain && domain.slots[0].needsFlush);
    CHECK(Allocated() == 2U && model.freeCalls == 0U);
    BytesEqual((const void *)domain.slots[0].dataFrame, inputSnapshot, 512U);
    UmicomU8 savedQueue[4096]; memcpy(savedQueue, (const void *)domain.slots[0].queueFrame, sizeof(savedQueue));
    CHECK(UmicomKernelBlockClose(&domain, handle) == UMICOM_BLOCK_RESET_PENDING);
    BytesEqual(savedQueue, (const void *)domain.slots[0].queueFrame, sizeof(savedQueue));
    BytesEqual((const void *)domain.slots[0].dataFrame, inputSnapshot, 512U);
    /* A completion after the caller's timeout is still allowed to touch owned DMA. */
    activeOutcome = &outcome; Complete(); activeOutcome = 0;
    BytesEqual(visibleMedia + 2560U, inputSnapshot, 512U);
    CHECK(outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED && model.freeCalls == 0U);
    model.stuckReset = UMICOM_FALSE; model.resetDelay = 3U; Close(handle);
    CHECK(domain.slots[0].writeUncertain && domain.slots[0].needsFlush);
}
static void WriteReleaseRetries(void)
{
    for (UmicomU32 point = 1U; point <= 2U; ++point) {
        WritableStart(); const UmicomKernelBlockHandle handle = WritableOpen();
        CHECK(WriteAt(handle, 1U, 1U) == UMICOM_BLOCK_OK); model.failFree = point;
        CHECK(UmicomKernelBlockClose(&domain, handle) == UMICOM_BLOCK_RELEASE_FAILED);
        CHECK(Allocated() == 3U - point && !domain.slots[0].exposed);
        CHECK(domain.slots[0].needsFlush && flushRequests == 0U);
        Close(handle); CHECK(flushRequests == 0U);
    }
}
static void CleanupDoesNotFlush(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    CHECK(WriteAt(handle, 2U, 1U) == UMICOM_BLOCK_OK);
    Close(handle); CHECK(flushRequests == 0U && domain.slots[0].needsFlush);
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
    const UmicomKernelBlockHandle next = WritableOpen();
    CHECK(!domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    /* A new lease's clean diagnostics do not assert that earlier data was flushed. */
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia)); Close(next);
}
static void EmptyFlush(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    CHECK(flushRequests == 1U && !domain.slots[0].needsFlush && !domain.slots[0].writeUncertain);
    BytesEqual(durableMedia, initialMedia, sizeof(durableMedia)); Close(handle);
}
static void MixedRingWrap(void)
{
    const UmicomKernelBlockHandle handle = WritableOpen();
    /* Drive the true 16-bit wrap with bounded requests and mixed chain shapes. */
    for (UmicomU32 i = 0U; i < 21848U; ++i) {
        CHECK(WriteAt(handle, i % 128U, 1U) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
        CHECK(UmicomKernelBlockRead(&domain, handle, i % 128U, 1U, resultBytes, sizeof(resultBytes)) == UMICOM_BLOCK_OK);
        BytesEqual(resultBytes, inputSnapshot, 512U);
        CHECK(Flush(handle) == UMICOM_BLOCK_OK && outcome == UMICOM_BLOCK_COMPLETED);
    }
    CHECK(domain.slots[0].usedIndex == 8U && domain.slots[0].availableIndex == 8U);
    CHECK(domain.slots[0].requests == 65544U); Close(handle);
}
static void WritableAllocationFailures(void)
{
    for (UmicomU32 point = 1U; point <= 2U; ++point) {
        WritableStart(); model.failAllocate = point; UmicomKernelBlockHandle handle = 0U;
        CHECK(UmicomKernelBlockOpenWritable(&domain, 0U, 32U, &handle) == UMICOM_BLOCK_NO_MEMORY);
        CHECK(!handle && Allocated() == 0U && model.notifications == 0U);
    }
}
static void FinalCompletionClock(UmicomBoolean backward)
{
    for (unsigned flush = 0U; flush < 2U; ++flush) {
      for (unsigned delayed = 0U; delayed < 2U; ++delayed) {
       for (UmicomU32 result = 0U; result < 2U; ++result) {
        WritableStart(); const UmicomKernelBlockHandle handle = WritableOpen();
        if (flush) CHECK(WriteAt(handle, 6U, 1U) == UMICOM_BLOCK_OK);
        model.delay = delayed ? 3U : 0U; model.result = result;
        finalClockAfterNotification = model.notifications;
        finalClockRollback = backward; finalClockArmed = UMICOM_TRUE;
        const UmicomKernelBlockStatus expected = backward ? UMICOM_BLOCK_CLOCK_ERROR : UMICOM_BLOCK_TIMEOUT;
        CHECK((flush ? Flush(handle) : WriteAt(handle, 6U, 1U)) == expected);
        CHECK(finalClockInjected && outcome == UMICOM_BLOCK_SUBMITTED_UNCONFIRMED);
        if (backward && delayed) CHECK(finalClockAtInjection >= finalClockStart);
        CHECK(domain.slots[0].state == UMICOM_BLOCK_FAULTED && !domain.slots[0].exposed);
        CHECK(domain.slots[0].needsFlush);
        CHECK(domain.slots[0].writeUncertain == (flush ? UMICOM_FALSE : UMICOM_TRUE));
        /* A successful device response can precede failure of the operation's
         * own time bound. The API must not erase that distinction. */
        BytesEqual(visibleMedia + 3072U, inputSnapshot, 512U);
        if (flush && !result) BytesEqual(durableMedia, visibleMedia, sizeof(durableMedia));
        else BytesEqual(durableMedia, initialMedia, sizeof(durableMedia));
        Close(handle);
       }
      }
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    WritableStart(); const char *name = argv[1];
    if (!strcmp(name, "admission")) Admission();
    else if (!strcmp(name, "readonly_boundary")) ReadOnlyAdmission();
    else if (!strcmp(name, "missing_flush") || !strcmp(name, "missing_modern") ||
        !strcmp(name, "refused_features") || !strcmp(name, "foreign_driver")) OpenFailure(name);
    else if (!strcmp(name, "failed_open_retained")) FailedOpenRetained();
    else if (!strcmp(name, "bounds")) WriteBounds();
    else if (!strcmp(name, "source_ownership")) SourceOwnership();
    else if (!strcmp(name, "outcome_ownership")) OutcomeOwnership();
    else if (!strcmp(name, "unsafe_and_reentry")) WriteUnsafeAndReentry();
    else if (!strcmp(name, "stale_and_exclusive")) WriteStaleAndExclusive();
    else if (!strcmp(name, "single_sector") || !strcmp(name, "last_sector") || !strcmp(name, "multi_sector") ||
        !strcmp(name, "noncontiguous_dma") || !strcmp(name, "owned_input_snapshot")) WriteSuccess(name);
    else if (!strcmp(name, "readback_and_flush")) ReadbackAndFlush();
    else if (!strcmp(name, "publication_before_notify")) BeforeNotify();
    else if (!strcmp(name, "partial_io_error")) PartialError(UMICOM_FALSE);
    else if (!strcmp(name, "partial_unsupported")) PartialError(UMICOM_TRUE);
    else if (!strncmp(name, "write_fault_", 12U)) SubmittedFault(name + 12U, UMICOM_FALSE);
    else if (!strncmp(name, "flush_fault_", 12U)) SubmittedFault(name + 12U, UMICOM_TRUE);
    else if (!strcmp(name, "preflight_faults")) PreflightFaults();
    else if (!strcmp(name, "flush_errors")) FlushErrors();
    else if (!strcmp(name, "late_completion_and_reset")) LateCompletionAndReset();
    else if (!strcmp(name, "release_retries")) WriteReleaseRetries();
    else if (!strcmp(name, "cleanup_does_not_flush")) CleanupDoesNotFlush();
    else if (!strcmp(name, "empty_flush")) EmptyFlush();
    else if (!strcmp(name, "mixed_ring_wrap")) MixedRingWrap();
    else if (!strcmp(name, "allocation_failures")) WritableAllocationFailures();
    else if (!strcmp(name, "final_completion_deadline")) FinalCompletionClock(UMICOM_FALSE);
    else if (!strcmp(name, "final_completion_clock_rollback")) FinalCompletionClock(UMICOM_TRUE);
    else { fprintf(stderr, "Unknown writable-block case: %s\n", name); return 2; }
    return 0;
}
