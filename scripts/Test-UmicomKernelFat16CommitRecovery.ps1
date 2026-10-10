#Requires -Version 5.1
<#
.SYNOPSIS
Qualify ordered FAT16 Stage/Finish with the normal Umicom Kernel in QEMU.

.DESCRIPTION
Uses only two NEW copies of the pinned 8-MiB synthetic fixture. A serial
socket bound to loopback sends one character at a time and verifies the guest
response. On the staged image, the script terminates ONLY its own QEMU after
Stage reports accepted and fully flushed. It independently checks all 8 MiB,
reboots read-only to confirm that dirty media are refused, and leaves the
staged image unchanged. On a separate clean copy, it executes Stage/Finish,
verifies the complete byte image, then reboots and mounts read-only.

The script is not a generic disk-writing utility. It accepts no disk-image
input or replacement fixture and deliberately performs no repair or replay.
An intentional QEMU process termination is not a physical power-cut test.

.PARAMETER QemuPath
Explicit executable path, or autodetect MSYS2 UCRT64 and PATH.
.PARAMETER BuildDirectory
Repository-relative directory containing bin/umicom-system.elf.
.PARAMETER AuditBuildDirectory
Repository-relative standalone FAT16 audit build directory.
.PARAMETER TimeoutSeconds
Bound for each boot, command and process shutdown (5..300 seconds).

.AUTHOR
Sammy Hegab, Umicom Foundation. Licence: MIT.
#>
[CmdletBinding()]
param(
    [string]$QemuPath,
    [string]$BuildDirectory = 'build/riscv64-clang-debug',
    [string]$AuditBuildDirectory = 'build/fat16-recovery-audit',
    [ValidateRange(5, 300)][int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-UmicomPath {
    param([string]$Value, [string]$Base)
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -match '[\x00-\x1f]') {
        throw 'The requested path is empty or contains control characters.'
    }
    if (-not [System.IO.Path]::IsPathRooted($Value)) { $Value = Join-Path $Base $Value }
    return [System.IO.Path]::GetFullPath($Value)
}

function Quote-UmicomArgument {
    param([AllowEmptyString()][string]$Value)
    # ProcessStartInfo.Arguments needs Windows-compatible escaping. Quoting is
    # explicit; never interpret an input as a command or invoke a shell parser.
    $quoted = [System.Text.StringBuilder]::new()
    [void]$quoted.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { ++$backslashes; continue }
        if ($character -eq '"') {
            [void]$quoted.Append(('\' * (2 * $backslashes + 1)))
            [void]$quoted.Append('"')
        } else {
            [void]$quoted.Append(('\' * $backslashes))
            [void]$quoted.Append($character)
        }
        $backslashes = 0
    }
    [void]$quoted.Append(('\' * (2 * $backslashes)))
    [void]$quoted.Append('"')
    return $quoted.ToString()
}

function Receive-UmicomSerial {
    param($Session, [string]$Ending, [int]$AtLeast, [DateTime]$Deadline)
    while ($true) {
        $current = $Session.SerialText.ToString()
        if ($Session.SerialText.Length -ge $AtLeast -and
            $current.EndsWith($Ending, [System.StringComparison]::Ordinal)) { return }
        if ([DateTime]::UtcNow -ge $Deadline) {
            throw "Timed out waiting for '$Ending' in $($Session.Label)."
        }
        try {
            $byteValue = $Session.Stream.ReadByte()
            if ($byteValue -lt 0) { throw 'Serial socket closed.' }
            if ($Session.SerialText.Length -ge 1048576) {
                throw 'Serial output exceeded the one-MiB evidence limit.'
            }
            [void]$Session.SerialText.Append([char]$byteValue)
        } catch [System.IO.IOException] {
            if ($Session.Process.HasExited) {
                throw "QEMU exited unexpectedly while waiting for '$Ending'."
            }
            # A 250-ms socket read timeout is not evidence of a guest fault.
        }
    }
}

function Send-UmicomCommand {
    param($Session, [string]$Command, [switch]$PowerOff)
    if ($Command.Length -eq 0 -or $Command.Length -ge 512 -or
        $Command -match '[^\x20-\x7e]') {
        throw 'Only bounded printable ASCII console commands are supported.'
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $start = $Session.SerialText.Length
    Write-Host ('  {0}: {1}' -f $Session.Label, $Command)
    foreach ($character in $Command.ToCharArray()) {
        $position = $Session.SerialText.Length
        $Session.Stream.WriteByte([byte][char]$character)
        Receive-UmicomSerial -Session $Session -Ending ([string]$character) `
            -AtLeast ($position + 1) -Deadline $deadline
        if ($Session.SerialText.ToString($position, 1) -cne [string]$character) {
            throw "Serial character echo mismatch in $($Session.Label)."
        }
    }
    $Session.Stream.WriteByte(13)
    if ($PowerOff) {
        if (-not $Session.Process.WaitForExit($TimeoutSeconds * 1000)) {
            throw "The guest did not exit after poweroff in $($Session.Label)."
        }
        while ($Session.Stream.DataAvailable) {
            [void]$Session.SerialText.Append([char]$Session.Stream.ReadByte())
        }
        if ($Session.Process.ExitCode -ne 0) {
            throw "QEMU returned $($Session.Process.ExitCode) after poweroff."
        }
    } else {
        Receive-UmicomSerial -Session $Session -Ending 'umicom> ' `
            -AtLeast ($start + $Command.Length + 8) -Deadline $deadline
    }
    $raw = $Session.SerialText.ToString($start, $Session.SerialText.Length - $start)
    $normalised = $raw.Replace("`r`n", "`n").Replace("`r", "`n")
    if (-not $normalised.StartsWith($Command + "`n", [System.StringComparison]::Ordinal)) {
        throw "The serial transcript does not begin with the echoed command '$Command'."
    }
    $body = $normalised.Substring($Command.Length + 1)
    if (-not $PowerOff) { $body = $body.Substring(0, $body.Length - 8) }
    [void]$Session.Commands.Add([pscustomobject]@{ command = $Command; response = $body })
    return $body
}

function Assert-UmicomConsoleLine {
    param([string]$Response, [string]$Prefix)
    $matches = @($Response -split "`n" | Where-Object {
        $_.StartsWith($Prefix, [System.StringComparison]::Ordinal)
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one '$Prefix' result. Inspect the serial log."
    }
    return $matches[0]
}

function Start-UmicomSession {
    param([string]$Label, [string]$Executable, [string]$Firmware,
        [string]$Disk, [bool]$ReadOnly, [string]$RunDirectory)
    $session = [pscustomobject]@{
        Label = $Label
        Listener = $null
        Client = $null
        Stream = $null
        Process = $null
        Started = $false
        StdoutTask = $null
        StderrTask = $null
        SerialText = [System.Text.StringBuilder]::new()
        Commands = [System.Collections.Generic.List[object]]::new()
        IntentionalTermination = $false
        ExitCode = $null
        Slot = $null
        Directory = $RunDirectory
    }
    try {
        $session.Listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Loopback, 0)
        $session.Listener.Start(1)
        $port = $session.Listener.LocalEndpoint.Port
        $drive = if ($ReadOnly) {
            "file=$Disk,if=none,format=raw,id=umicomdisk,readonly=on,cache=writeback,rerror=report"
        } else {
            "file=$Disk,if=none,format=raw,id=umicomdisk,readonly=off,cache=writeback,rerror=report,werror=report"
        }
        $device = if ($ReadOnly) {
            'virtio-blk-device,drive=umicomdisk,logical_block_size=512,physical_block_size=512,num-queues=1'
        } else {
            'virtio-blk-device,drive=umicomdisk,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1'
        }
        $arguments = @(
            '-machine', 'virt,aclint=off', '-bios', $Firmware,
            '-display', 'none', '-monitor', 'none',
            '-chardev', "socket,id=console,host=127.0.0.1,port=$port,server=off",
            '-serial', 'chardev:console', '-m', '128M', '-smp', '1',
            '-no-reboot', '-global', 'virtio-mmio.force-legacy=false',
            '-drive', $drive, '-device', $device
        )
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = $Executable
        $psi.Arguments = (@($arguments | ForEach-Object { Quote-UmicomArgument $_ }) -join ' ')
        $psi.WorkingDirectory = $RunDirectory
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardInput = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $session.Process = [System.Diagnostics.Process]::new()
        $session.Process.StartInfo = $psi
        if (-not $session.Process.Start()) { throw 'QEMU did not start.' }
        $session.Started = $true
        $session.Process.StandardInput.Close()
        $session.StdoutTask = $session.Process.StandardOutput.ReadToEndAsync()
        $session.StderrTask = $session.Process.StandardError.ReadToEndAsync()
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while (-not $session.Listener.Pending()) {
            if ($session.Process.HasExited) {
                throw "QEMU exited before connecting serial: $($session.Process.ExitCode)."
            }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'QEMU did not connect before the deadline.' }
            Start-Sleep -Milliseconds 10
        }
        $session.Client = $session.Listener.AcceptTcpClient()
        $session.Client.NoDelay = $true
        $session.Listener.Stop()
        $session.Listener = $null
        $session.Stream = $session.Client.GetStream()
        $session.Stream.ReadTimeout = 250
        $session.Stream.WriteTimeout = 1000
        Receive-UmicomSerial -Session $session -Ending 'umicom> ' `
            -AtLeast 8 -Deadline ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds))
        return $session
    } catch {
        try { Close-UmicomSession -Session $session } catch { }
        throw
    }
}

function Close-UmicomSession {
    param($Session, [switch]$IntentionalKill)
    if ($null -eq $Session) { return }
    if ($null -ne $Session.Process -and $Session.Started -and -not $Session.Process.HasExited) {
        $Session.Process.Kill()  # ONLY the Process instance started by us.
        if (-not $Session.Process.WaitForExit(15000)) {
            throw "Owned QEMU did not exit: $($Session.Label)"
        }
        if ($IntentionalKill) { $Session.IntentionalTermination = $true }
    }
    if ($null -ne $Session.Process -and $Session.Started -and $Session.Process.HasExited) {
        $Session.ExitCode = $Session.Process.ExitCode
    }
    $outputBase = Join-Path $Session.Directory $Session.Label
    [System.IO.File]::WriteAllText($outputBase + '.serial.log',
        $Session.SerialText.ToString(), [System.Text.UTF8Encoding]::new($false))
    if ($null -ne $Session.StdoutTask) {
        [System.IO.File]::WriteAllText($outputBase + '.stdout.log',
            $Session.StdoutTask.GetAwaiter().GetResult(), [System.Text.UTF8Encoding]::new($false))
    }
    if ($null -ne $Session.StderrTask) {
        [System.IO.File]::WriteAllText($outputBase + '.stderr.log',
            $Session.StderrTask.GetAwaiter().GetResult(), [System.Text.UTF8Encoding]::new($false))
    }
    if ($null -ne $Session.Stream) { $Session.Stream.Dispose() }
    if ($null -ne $Session.Client) { $Session.Client.Close() }
    if ($null -ne $Session.Listener) { $Session.Listener.Stop() }
    if ($null -ne $Session.Process) { $Session.Process.Dispose() }
}

function Get-UmicomSlot {
    param($Session)
    $response = Send-UmicomCommand -Session $Session -Command 'disks'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'block.domain=ok')
    $found = [regex]::Matches($response,
        '(?m)^block\.slot=([0-7]) base=0x[0-9a-fA-F]+ probe=ok device-id=2 held-frames=0$')
    if ($found.Count -ne 1) { throw 'Expected exactly one qualified VirtIO block slot.' }
    $Session.Slot = [int]$found[0].Groups[1].Value
    return $Session.Slot
}

function Invoke-UmicomAudit {
    param([string]$Executable, [string[]]$Arguments, [int]$ExpectedExit,
        [string]$ExpectedClassification)
    $text = & $Executable @Arguments
    $actualExit = $LASTEXITCODE
    if ($actualExit -ne $ExpectedExit) {
        throw "Audit exited $actualExit, expected $ExpectedExit. Output: $text"
    }
    $result = $text | ConvertFrom-Json
    if ($result.classification -cne $ExpectedClassification -or
        $result.repair_performed -ne $false) {
        throw "Unexpected audit classification or repair evidence: $text"
    }
    return $result
}

$run = $null
$session = $null
$exitCode = 1
$result = [ordered]@{
    schemaVersion = 1
    test = 'qemu-normal-console-ordered-fat16-commit-recovery'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    finishedUtc = $null
    passed = $false
    error = $null
    sourceFixture = $null
    originalSha256 = $null
    finalFixtureSha256 = $null
    firmwareSha256 = $null
    qemu = $null
    evidenceFiles = @{}
    stagedSha256 = $null
    stagedAfterReadonlySha256 = $null
    finishedSha256 = $null
    finishedAfterReadonlySha256 = $null
    boots = [System.Collections.Generic.List[object]]::new()
    audits = [System.Collections.Generic.List[object]]::new()
}
try {
    $root = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
    $kernelBuild = Resolve-UmicomPath -Value $BuildDirectory -Base $root
    $auditBuild = Resolve-UmicomPath -Value $AuditBuildDirectory -Base $root
    $firmware = Join-Path $kernelBuild 'bin/umicom-system.elf'
    $fixture = Join-Path $root 'tests/disk_inspection/fixture.raw'
    $expectedFixtureHash = 'bd866d6ae337527f3a8b4609f31969d60185e025f8525726f573d905a2f50938'
    $verifier = Join-Path $auditBuild 'umicom-fat16-guest-commit-evidence.exe'
    $headerAudit = Join-Path $auditBuild 'umicom-fat16-recovery-audit.exe'
    $graphAudit = Join-Path $auditBuild 'umicom-fat16-integrity-audit.exe'
    foreach ($file in @($firmware, $fixture, $verifier, $headerAudit, $graphAudit)) {
        if (-not [System.IO.File]::Exists($file)) { throw "Required built file not found: $file" }
    }
    if ([string]::IsNullOrWhiteSpace($QemuPath)) {
        $msysQemu = 'C:\msys64\ucrt64\bin\qemu-system-riscv64.exe'
        if ([System.IO.File]::Exists($msysQemu)) { $QemuPath = $msysQemu }
        else {
            $command = Get-Command qemu-system-riscv64 -CommandType Application `
                -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -eq $command) { throw 'QEMU RV64 executable not found; use -QemuPath.' }
            $QemuPath = $command.Source
        }
    }
    $qemu = Resolve-UmicomPath -Value $QemuPath -Base $root
    if (-not [System.IO.File]::Exists($qemu)) { throw "QEMU not found: $qemu" }
    if ($kernelBuild.Contains(',') -or $auditBuild.Contains(',')) {
        throw 'A build directory path contains a comma, unsafe for QEMU drive syntax.'
    }
    $fixtureSha = (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($fixtureSha -cne $expectedFixtureHash) {
        throw 'The pinned synthetic fixture does not match the expected SHA-256. No disk copy is created.'
    }
    $result.sourceFixture = $fixture
    $result.originalSha256 = $fixtureSha
    $result.firmwareSha256 = (Get-FileHash -LiteralPath $firmware -Algorithm SHA256).Hash.ToLowerInvariant()
    $result.qemu = $qemu
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
    $run = Join-Path (Join-Path $kernelBuild 'fat16-recovery-qualification') $runId
    if ($run.Contains(',')) { throw 'QEMU raw-disk path contains an unsafe comma.' }
    [void][System.IO.Directory]::CreateDirectory($run)
    $stagedDisk = Join-Path $run 'staged-after-kill.raw'
    $finishedDisk = Join-Path $run 'finished-after-poweroff.raw'
    [System.IO.File]::Copy($fixture, $stagedDisk, $false)
    [System.IO.File]::Copy($fixture, $finishedDisk, $false)
    $result.evidenceFiles['staged'] = $stagedDisk
    $result.evidenceFiles['finished'] = $finishedDisk
    Write-Host "Created two disposable disk copies. Evidence: $run"

    # Stage and kill: only after the Kernel confirms its ordered dirty guards,
    # data flush and readback. A kill at this point is NOT a random mid-write
    # power cut and MUST NOT be represented as one.
    $session = Start-UmicomSession -Label 'stage-writer' -Executable $qemu `
        -Firmware $firmware -Disk $stagedDisk -ReadOnly $false -RunDirectory $run
    $slot = Get-UmicomSlot -Session $session
    $response = Send-UmicomCommand -Session $session -Command "fatcommitopen $slot 0"
    $line = Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.open=ok '
    $response = Send-UmicomCommand -Session $session `
        -Command 'fatstage /FRAG.BIN 511 "Umicom ordered update"'
    $line = Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.stage=ok '
    if ($response -notmatch [regex]::Escape('The volume remains dirty until fatcommit succeeds.')) {
        throw 'Kernel did not confirm the staged dirty-state rule.'
    }
    [void]$result.boots.Add([pscustomobject]@{scenario='stage-writer'; slot=$slot;
        method='terminate-owned-qemu-after-confirmed-stage'; committed=$false})
    Close-UmicomSession -Session $session -IntentionalKill
    $session = $null
    $result.stagedSha256 = (Get-FileHash -LiteralPath $stagedDisk -Algorithm SHA256).Hash.ToLowerInvariant()
    $stageAudit = Invoke-UmicomAudit -Executable $verifier `
        -Arguments @($fixture,$stagedDisk,'stage') -ExpectedExit 0 -ExpectedClassification 'verified'
    if (-not $stageAudit.full_image_compared -or $stageAudit.expected_changed_bytes -ne 23) {
        throw 'Staged image was not byte-for-byte verified after process termination.'
    }
    [void]$result.audits.Add($stageAudit)
    [void]$result.audits.Add((Invoke-UmicomAudit -Executable $headerAudit `
        -Arguments @($stagedDisk,'0') -ExpectedExit 2 -ExpectedClassification 'dirty-or-io-error'))
    [void]$result.audits.Add((Invoke-UmicomAudit -Executable $graphAudit `
        -Arguments @($stagedDisk,'0') -ExpectedExit 2 -ExpectedClassification 'not-admitted'))

    # Fresh read-only QEMU must refuse the dirty disk; this guest cannot write
    # to the image even on a defective console branch. Its hash must be stable.
    $session = Start-UmicomSession -Label 'stage-recovery-boot' -Executable $qemu `
        -Firmware $firmware -Disk $stagedDisk -ReadOnly $true -RunDirectory $run
    $slot = Get-UmicomSlot -Session $session
    $response = Send-UmicomCommand -Session $session -Command "mountdisk $slot 0"
    $line = Assert-UmicomConsoleLine -Response $response -Prefix 'disk.mount='
    if ($line.StartsWith('disk.mount=ok ',[System.StringComparison]::Ordinal)) {
        throw 'Unsafe: the read-only mount accepted an incomplete staged commit.'
    }
    [void]$result.boots.Add([pscustomobject]@{scenario='stage-recovery-boot';slot=$slot;
        readOnly=$true;mountRefused=$true})
    $response = Send-UmicomCommand -Session $session -Command 'poweroff' -PowerOff
    if ($response -notmatch 'UMICOM_KERNEL_END') { throw 'Dirty-image guest did not complete shutdown.' }
    Close-UmicomSession -Session $session
    $session = $null
    $result.stagedAfterReadonlySha256 = (Get-FileHash -LiteralPath $stagedDisk -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($result.stagedAfterReadonlySha256 -cne $result.stagedSha256) {
        throw 'The dirty image changed during a read-only guest boot.'
    }

    # Separate fresh copy: Finish must restore exactly two original FAT flags;
    # new data remain after both images are closed and reopened independently.
    $session = Start-UmicomSession -Label 'finished-writer' -Executable $qemu `
        -Firmware $firmware -Disk $finishedDisk -ReadOnly $false -RunDirectory $run
    $slot = Get-UmicomSlot -Session $session
    $response = Send-UmicomCommand -Session $session -Command "fatcommitopen $slot 0"
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.open=ok ')
    $response = Send-UmicomCommand -Session $session `
        -Command 'fatstage /FRAG.BIN 511 "Umicom ordered update"'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.stage=ok ')
    $response = Send-UmicomCommand -Session $session -Command 'fatcommit'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.finish=ok ')
    $response = Send-UmicomCommand -Session $session -Command 'fatcommitclose'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'fat.commit.release=ok ')
    [void]$result.boots.Add([pscustomobject]@{scenario='finished-writer';slot=$slot;
        method='stage-finish-clean-shutdown';committed=$true})
    $response = Send-UmicomCommand -Session $session -Command 'poweroff' -PowerOff
    if ($response -notmatch 'UMICOM_KERNEL_END') { throw 'Finished guest did not complete shutdown.' }
    Close-UmicomSession -Session $session
    $session = $null
    $result.finishedSha256 = (Get-FileHash -LiteralPath $finishedDisk -Algorithm SHA256).Hash.ToLowerInvariant()
    $finishedAudit = Invoke-UmicomAudit -Executable $verifier `
        -Arguments @($fixture,$finishedDisk,'finish') -ExpectedExit 0 -ExpectedClassification 'verified'
    if (-not $finishedAudit.full_image_compared -or $finishedAudit.expected_changed_bytes -ne 21) {
        throw 'Finished image did not match the exact independently expected disk.'
    }
    [void]$result.audits.Add($finishedAudit)
    [void]$result.audits.Add((Invoke-UmicomAudit -Executable $headerAudit `
        -Arguments @($finishedDisk,'0') -ExpectedExit 0 -ExpectedClassification 'clean-mirrored-fat'))
    [void]$result.audits.Add((Invoke-UmicomAudit -Executable $graphAudit `
        -Arguments @($finishedDisk,'0') -ExpectedExit 0 -ExpectedClassification 'consistent-allocation-graph'))

    $session = Start-UmicomSession -Label 'finished-readonly-boot' -Executable $qemu `
        -Firmware $firmware -Disk $finishedDisk -ReadOnly $true -RunDirectory $run
    $slot = Get-UmicomSlot -Session $session
    $response = Send-UmicomCommand -Session $session -Command "mountdisk $slot 0"
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'disk.mount=ok ')
    $response = Send-UmicomCommand -Session $session -Command 'diskls /'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'disk.list=ok ')
    $response = Send-UmicomCommand -Session $session -Command 'unmountdisk'
    [void](Assert-UmicomConsoleLine -Response $response -Prefix 'disk.unmount=ok ')
    [void]$result.boots.Add([pscustomobject]@{scenario='finished-readonly-boot';slot=$slot;
        readOnly=$true;mountAccepted=$true})
    $response = Send-UmicomCommand -Session $session -Command 'poweroff' -PowerOff
    if ($response -notmatch 'UMICOM_KERNEL_END') { throw 'Read-only guest did not complete shutdown.' }
    Close-UmicomSession -Session $session
    $session = $null
    $result.finishedAfterReadonlySha256 = (Get-FileHash -LiteralPath $finishedDisk -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($result.finishedAfterReadonlySha256 -cne $result.finishedSha256) {
        throw 'The finished image changed during the read-only guest boot.'
    }
    $result.passed = $true
    $exitCode = 0
} catch {
    $result.error = $_.Exception.Message
    Write-Host ("FAIL: {0}" -f $result.error) -ForegroundColor Red
} finally {
    try { Close-UmicomSession -Session $session } catch {
        $result.passed = $false
        $exitCode = 1
        $result.error = "$($result.error); cleanup: $($_.Exception.Message)"
    }
    if ($null -ne $result.sourceFixture) {
        try {
            $result.finalFixtureSha256 = (Get-FileHash -LiteralPath $result.sourceFixture -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($result.finalFixtureSha256 -cne $result.originalSha256) {
                throw 'Source fixture changed during the qualification run.'
            }
        } catch {
            $result.passed = $false
            $exitCode = 1
            $result.error = "$($result.error); source verification: $($_.Exception.Message)"
        }
    }
    $result.finishedUtc = [DateTime]::UtcNow.ToString('o')
    if ($null -ne $run) {
        $file = Join-Path $run 'result.json'
        [System.IO.File]::WriteAllText($file,
            ($result | ConvertTo-Json -Depth 12) + "`n",
            [System.Text.UTF8Encoding]::new($false))
        Write-Host "Evidence: $file"
    }
}
if ($exitCode -eq 0) {
    Write-Host 'PASS: QEMU Stage survived intentional process termination with exact dirty on-disk guards.' -ForegroundColor Green
    Write-Host 'PASS: a new read-only Kernel boot refused staged dirty media; the image remained unchanged.' -ForegroundColor Green
    Write-Host 'PASS: finished data survived a clean reboot; both auditors and all 16,384 sectors matched.' -ForegroundColor Green
}
exit $exitCode
