/*-----------------------------------------------------------------------------
 * Umicom Kernel
 * File: kernel/fat16_move_plan.inc
 *
 * Read-only metadata preparation for moving short-name files and directories.
 * These helpers run inside the lifecycle planner's existing busy/read budget.
 * They never own a transport and never write a disk.
 * Author: Sammy Hegab, Umicom Foundation. Licence: MIT.
 *---------------------------------------------------------------------------*/

/* Walk only the checked parent graph retained by the whole-volume proof.
 * A finite hop bound remains explicit even though that proof rejects cycles. */
static const UmicomKernelFat16UpdateDirectory *UmicomFatMoveDirectory(
    const UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 cluster)
{
    for (UmicomSize i = 0U; i < workspace->directoryCount; ++i)
        if (workspace->directories[i].firstCluster == cluster) return &workspace->directories[i];
    return 0;
}
static UmicomKernelDiskStatus UmicomFatMoveDescendant(
    const UmicomKernelFat16UpdateWorkspace *workspace, UmicomU16 cluster,
    UmicomU16 ancestor, UmicomBoolean *outDescendant)
{
    *outDescendant = UMICOM_FALSE;
    for (UmicomSize hops = 0U; hops < UMICOM_FAT16_UPDATE_DIRECTORY_LIMIT; ++hops) {
        if (cluster == ancestor) { *outDescendant = UMICOM_TRUE; return UMICOM_DISK_OK; }
        if (!cluster) return UMICOM_DISK_OK;
        const UmicomKernelFat16UpdateDirectory *const item = UmicomFatMoveDirectory(workspace, cluster);
        if (!item) return UMICOM_DISK_CORRUPT;
        cluster = item->parentCluster;
    }
    return UMICOM_DISK_CHAIN_CYCLE;
}

/* Resolve the destination without manufacturing a free entry. A rename in the
 * same parent reuses its old slot, so a full parent can still rename a child. */
static UmicomKernelDiskStatus UmicomFatMoveDestination(UmicomKernelFat16 *volume,
    const char *path, UmicomU16 sourceParent, char *outName, UmicomU16 *outParent)
{
    char names[UMICOM_FAT16_DEPTH_LIMIT][13];
    UmicomFatClear(names, sizeof(names));
    UmicomSize depth = 0U;
    UmicomKernelDiskStatus status = UmicomFatPath(path, names, &depth);
    if (status != UMICOM_DISK_OK) return status;
    if (!depth) return UMICOM_DISK_IS_DIRECTORY;
    UmicomKernelFat16Entry current;
    UmicomFatClear(&current, sizeof(current));
    current.directory = UMICOM_TRUE;
    for (UmicomSize i = 0U; i < depth; ++i) {
        if (!current.directory) return UMICOM_DISK_NOT_DIRECTORY;
        status = UmicomFatDirectoryRead(volume, current.firstCluster);
        if (status != UMICOM_DISK_OK) return status;
        UmicomBoolean found = UMICOM_FALSE;
        UmicomKernelFat16Entry selected;
        UmicomFatClear(&selected, sizeof(selected));
        for (UmicomSize j = 0U; j < volume->directoryStage.count; ++j)
            if (UmicomFatStringEqual(names[i], volume->directoryStage.entries[j].name)) {
                selected = volume->directoryStage.entries[j]; found = UMICOM_TRUE; break;
            }
        if (i + 1U == depth) {
            if (found) return UMICOM_DISK_EXISTS;
            if (current.attributes & 0x01U) return UMICOM_DISK_READ_ONLY;
            if (sourceParent != current.firstCluster &&
                volume->directoryStage.count >= UMICOM_FAT16_ENTRY_LIMIT) return UMICOM_DISK_LIMIT;
            UmicomFatCopy(outName, names[i], 13U);
            *outParent = current.firstCluster;
            return UMICOM_DISK_OK;
        }
        if (!found) return UMICOM_DISK_NOT_FOUND;
        current = selected;
    }
    return UMICOM_DISK_CORRUPT;
}

/* Removing an entry modifies its parent namespace too. Unlike a file content
 * write, a move must therefore respect a read-only source parent. */
static UmicomKernelDiskStatus UmicomFatMoveSourceParent(UmicomKernelFat16 *volume, const char *path)
{
    char parentPath[UMICOM_FAT16_PATH_BYTES];
    UmicomFatClear(parentPath, sizeof(parentPath));
    UmicomSize slash = 0U;
    for (UmicomSize i = 0U; path[i]; ++i) {
        parentPath[i] = path[i];
        if (path[i] == '/') slash = i;
    }
    if (!slash) return UMICOM_DISK_OK; /* The fixed root has no entry attributes. */
    parentPath[slash] = 0;
    UmicomKernelFat16Entry parent;
    UmicomU16 ignored = 0U;
    UmicomKernelDiskStatus status = UmicomFatFileLookup(volume, parentPath, &parent, &ignored);
    if (status != UMICOM_DISK_OK) return status;
    if (!parent.directory) return UMICOM_DISK_NOT_DIRECTORY;
    return parent.attributes & 0x01U ? UMICOM_DISK_READ_ONLY : UMICOM_DISK_OK;
}

/* Check the resulting subtree, not just the destination spelling. The deepest
 * empty directory can occupy the path limit; any child needs one more level. */
static UmicomKernelDiskStatus UmicomFatMoveDepth(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, const UmicomKernelFat16Entry *source,
    UmicomU16 destinationParent)
{
    const UmicomKernelFat16UpdateDirectory *const parent = UmicomFatMoveDirectory(workspace, destinationParent);
    if (!parent) return UMICOM_DISK_CORRUPT;
    if (parent->depth >= UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_DISK_LIMIT;
    if (!source->directory) return UMICOM_DISK_OK;
    UmicomBoolean inside = UMICOM_FALSE;
    UmicomKernelDiskStatus status = UmicomFatMoveDescendant(workspace, destinationParent, source->firstCluster, &inside);
    if (status != UMICOM_DISK_OK) return status;
    if (inside) return UMICOM_DISK_CHAIN_CYCLE;
    const UmicomKernelFat16UpdateDirectory *const origin = UmicomFatMoveDirectory(workspace, source->firstCluster);
    if (!origin) return UMICOM_DISK_CORRUPT;
    for (UmicomSize i = 0U; i < workspace->directoryCount; ++i) {
        const UmicomKernelFat16UpdateDirectory *const item = &workspace->directories[i];
        status = UmicomFatMoveDescendant(workspace, item->firstCluster, source->firstCluster, &inside);
        if (status != UMICOM_DISK_OK) return status;
        if (!inside) continue;
        if (item->depth < origin->depth) return UMICOM_DISK_CORRUPT;
        const UmicomSize depth = item->depth - origin->depth + parent->depth + 1U;
        if (depth > UMICOM_FAT16_DEPTH_LIMIT) return UMICOM_DISK_LIMIT;
        if (depth == UMICOM_FAT16_DEPTH_LIMIT) {
            status = UmicomFatDirectoryRead(volume, item->firstCluster);
            if (status != UMICOM_DISK_OK) return status;
            if (volume->directoryStage.count) return UMICOM_DISK_LIMIT;
        }
    }
    return UMICOM_DISK_OK;
}

/* Only alias bytes and the two supported display flags change in a live entry.
 * Raw creation/access/write fields and attributes are retained byte for byte. */
static void UmicomFatMoveAlias(UmicomU8 *record, const char *name)
{
    for (UmicomSize i = 0U; i < 11U; ++i) record[i] = ' ';
    UmicomSize position = 0U;
    for (UmicomSize i = 0U; name[i]; ++i) {
        if (name[i] == '.') position = 8U;
        else record[position++] = (UmicomU8)name[i];
    }
    record[12] &= (UmicomU8)~0x18U;
}

static UmicomKernelDiskStatus UmicomFatLifecycleMove(UmicomKernelFat16 *volume,
    UmicomKernelFat16UpdateWorkspace *workspace, UmicomKernelFat16LifecycleWorkspace *scratch,
    UmicomU16 sourceParent)
{
    UmicomKernelFat16LifecyclePlan *const stage = &scratch->stage;
    UmicomKernelDiskStatus status = UmicomFatMoveSourceParent(volume, scratch->path);
    UmicomU16 destinationParent = 0U;
    if (status == UMICOM_DISK_OK)
        status = UmicomFatMoveDestination(volume, scratch->destination, sourceParent,
            stage->updatedEntry.name, &destinationParent);
    if (status == UMICOM_DISK_OK)
        status = UmicomFatMoveDepth(volume, workspace, &stage->originalEntry, destinationParent);
    if (status != UMICOM_DISK_OK) return status;

    /* Retain the clean header image even when allocation never changes: the
     * transport still needs its ordinary dirty/clean publication barriers. */
    UmicomSize header = 0U;
    status = UmicomFatLifecycleFatSector(volume, stage, 0U, &header);
    if (status != UMICOM_DISK_OK) return status;
    status = UmicomFatLifecycleDirectory(volume, workspace, sourceParent, stage);
    if (status != UMICOM_DISK_OK) return status;
    if (sourceParent == destinationParent) {
        UmicomFatMoveAlias(stage->directorySectors[0].data + stage->entryOffset, stage->updatedEntry.name);
        return UMICOM_DISK_OK;
    }

    /* Keep the source snapshot while the existing free-slot finder prepares the
     * destination and, if necessary, its following end marker. No parent grows
     * in this metadata-only operation. Callers may create capacity separately. */
    /* Explicit Kernel copies avoid a compiler-generated hosted memcpy call
     * for this large aggregate. The earlier expression is retained for review. */
#if 0
    const UmicomKernelFat16LifecycleDirectorySector source = stage->directorySectors[0];
#endif
    UmicomKernelFat16LifecycleDirectorySector source;
    UmicomFatCopy(&source, &stage->directorySectors[0], sizeof(source));
    const UmicomSize sourceOffset = stage->entryOffset;
    stage->directorySectorCount = 0U;
    UmicomFatClear(stage->directorySectors, sizeof(stage->directorySectors));
    stage->operation = UMICOM_FAT16_LIFECYCLE_CREATE;
    status = UmicomFatLifecycleDirectory(volume, workspace, destinationParent, stage);
    stage->operation = UMICOM_FAT16_LIFECYCLE_MOVE;
    if (status != UMICOM_DISK_OK) return status;
    UmicomU8 *const destination = stage->directorySectors[0].data + stage->entryOffset;
    UmicomFatCopy(destination, source.original + sourceOffset, 32U);
    UmicomFatMoveAlias(destination, stage->updatedEntry.name);
    if (stage->directorySectorCount >= UMICOM_FAT16_LIFECYCLE_DIRECTORY_SECTORS)
        return UMICOM_DISK_LIMIT;
    /* Use the freestanding copy primitive for the second aggregate too. */
#if 0
    stage->directorySectors[stage->directorySectorCount] = source;
#endif
    UmicomFatCopy(&stage->directorySectors[stage->directorySectorCount], &source, sizeof(source));
    stage->directorySectors[stage->directorySectorCount].data[sourceOffset] = 0xe5U;
    ++stage->directorySectorCount;

    if (stage->originalEntry.directory) {
        if (stage->directorySectorCount >= UMICOM_FAT16_LIFECYCLE_DIRECTORY_SECTORS)
            return UMICOM_DISK_LIMIT;
        UmicomKernelFat16LifecycleDirectorySector *const dot = &stage->directorySectors[stage->directorySectorCount];
        const UmicomU64 relative = volume->dataStart +
            ((UmicomU64)stage->originalEntry.firstCluster - 2U) * volume->info.sectorsPerCluster;
        status = UmicomFatReadSector(volume, relative, dot->original);
        if (status != UMICOM_DISK_OK) return status;
        dot->sector = volume->info.firstSector + relative;
        UmicomFatCopy(dot->data, dot->original, sizeof(dot->data));
        /* The complete namespace proof already established canonical dots.
         * Preserve every other byte, including their explicit calendars. */
        UmicomFatLifecyclePut16(dot->data + 32U + 26U, destinationParent);
        ++stage->directorySectorCount;
    }
    return UMICOM_DISK_OK;
}
