#Requires -Version 5.1
<#
.SYNOPSIS
Tests writable FAT16 through two boots of the normal Umicom Kernel console.

.DESCRIPTION
Makes a fresh, uniquely named copy of the repository's synthetic FAT16 fixture,
then controls the normal firmware through a private loopback serial connection.
The first boot creates /WORK/NOTE.TXT containing "hello world". The second boot
reads the same disk and checks that reading has not changed any image byte.

Input is paced by the Kernel's character echo and command prompt. This avoids
clipboard dependence and UART overruns from submitting an entire command batch.
The script retains the disposable disk, serial logs and result JSON on failure
as well as success. It never takes a user disk path or writes the source fixture.

The runtime uses Windows PowerShell 5.1-compatible .NET APIs. PowerShell 7 on
other hosts can exercise the same protocol with -QemuPath and -BuildDirectory.

.PARAMETER QemuPath
QEMU RISC-V executable. By default, use C:\msys64\ucrt64\bin on Windows when
available, otherwise look for qemu-system-riscv64 on PATH.

.PARAMETER BuildDirectory
Build directory containing bin/umicom-system.elf. Relative paths start at the
repository root, not the caller's current directory.

.PARAMETER TimeoutSeconds
Maximum time for each connection, prompt/command, or shutdown wait (5 to 300).
Every character in a command shares that command's deadline.

.PARAMETER CheckSyntaxDiagnostics
Also check the explicit invalid-partition diagnostic and unused mount state
before mounting. This switch requires firmware with the console diagnostics
update; omit it when testing the earlier writable-VFS firmware.

.EXAMPLE
.\scripts\Test-UmicomKernelWritableConsole.ps1

.EXAMPLE
.\scripts\Test-UmicomKernelWritableConsole.ps1 -QemuPath 'C:\QEMU\qemu-system-riscv64.exe'

.NOTES
Author: Sammy Hegab, Umicom Foundation
Licence: MIT
#>
[CmdletBinding()]
param(
    [string]$QemuPath,
    [string]$BuildDirectory = 'build/riscv64-clang-debug',
    [ValidateRange(5, 300)]
    [int]$TimeoutSeconds = 60,
    [switch]$CheckSyntaxDiagnostics
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-UmicomKernelAbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -match '[\x00-\x1f]') {
        throw 'An empty path or a path containing control characters is not supported.'
    }
    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $Path = Join-Path -Path $BaseDirectory -ChildPath $Path
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function ConvertTo-UmicomKernelProcessArgument {
    param([AllowEmptyString()][string]$Value)
    # ProcessStartInfo does not use a shell. Its Arguments string still needs
    # the platform's argv quoting rules, including backslashes before quotes
    # and a trailing backslash before the closing quote. Never use Invoke-Expression.
    $quoted = [System.Text.StringBuilder]::new()
    [void]$quoted.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
        } elseif ($character -eq '"') {
            [void]$quoted.Append(('\' * (2 * $backslashes + 1)))
            [void]$quoted.Append('"')
            $backslashes = 0
        } else {
            [void]$quoted.Append(('\' * $backslashes))
            [void]$quoted.Append($character)
            $backslashes = 0
        }
    }
    [void]$quoted.Append(('\' * (2 * $backslashes)))
    [void]$quoted.Append('"')
    return $quoted.ToString()
}

function New-UmicomKernelReadChannel {
    param([string]$Name, [System.IO.Stream]$Stream, [string]$LogPath)
    $channel = [pscustomobject]@{
        Name = $Name
        Stream = $Stream
        Buffer = [byte[]]::new(8192)
        Task = $null
        Log = [System.IO.File]::Open($LogPath, [System.IO.FileMode]::CreateNew,
            [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        Bytes = 0
        Closed = $false
    }
    try {
        $channel.Task = $Stream.ReadAsync($channel.Buffer, 0, $channel.Buffer.Length)
        return $channel
    } catch {
        $channel.Log.Dispose()
        throw
    }
}

function Read-UmicomKernelAvailableOutput {
    param($Session)
    foreach ($channel in $Session.Channels) {
        if ($channel.Closed -or -not $channel.Task.IsCompleted) { continue }
        $count = $channel.Task.GetAwaiter().GetResult()
        if ($count -eq 0) {
            $channel.Closed = $true
            $channel.Log.Flush()
            continue
        }
        # Each stream has its own fixed one-MiB bound. A broken guest or QEMU
        # cannot fill memory or disk indefinitely while a prompt is missing.
        $remaining = 1048576 - $channel.Bytes
        $stored = [Math]::Min($count, $remaining)
        if ($stored -gt 0) {
            $channel.Log.Write($channel.Buffer, 0, $stored)
            $channel.Log.Flush()
            $channel.Bytes += $stored
            if ($channel.Name -eq 'serial') {
                [void]$Session.SerialText.Append(
                    [System.Text.Encoding]::ASCII.GetString($channel.Buffer, 0, $stored))
            }
        }
        if ($count -gt $remaining) {
            throw "QEMU $($channel.Name) output exceeded the one-MiB limit; partial logs are retained."
        }
        $channel.Task = $channel.Stream.ReadAsync($channel.Buffer, 0, $channel.Buffer.Length)
    }
}

function Wait-UmicomKernelSerial {
    param($Session, [int]$Start, [string]$Expected, [long]$Deadline, [switch]$Echo)
    while ($true) {
        Read-UmicomKernelAvailableOutput -Session $Session
        if ($Session.SerialText.Length -ge ($Start + $Expected.Length)) {
            $received = $Session.SerialText.ToString($Start, $Session.SerialText.Length - $Start)
            if ($Echo) {
                if (-not $received.StartsWith($Expected, [System.StringComparison]::Ordinal)) {
                    throw "Unexpected serial input echo during boot $($Session.Number). See the serial log."
                }
                return
            }
            if ($received.EndsWith($Expected, [System.StringComparison]::Ordinal)) { return }
        }
        if ($Session.SerialChannel.Closed) {
            throw "The serial connection closed before the expected response in boot $($Session.Number)."
        }
        if ($Session.Clock.ElapsedMilliseconds -ge $Deadline) {
            $waitingFor = $(if ($Echo) { 'echo' } else { 'prompt' })
            throw "Timed out waiting for serial $waitingFor in boot $($Session.Number)."
        }
        Start-Sleep -Milliseconds 2
    }
}

function Send-UmicomKernelCommand {
    param($Session, [string]$Command, [switch]$PowerOff)
    if ($Command.Length -eq 0 -or $Command.Length -ge 512 -or $Command -match '[^\x20-\x7e]') {
        throw 'The test protocol only sends bounded printable ASCII commands.'
    }
    $deadline = $Session.Clock.ElapsedMilliseconds + [long]$TimeoutSeconds * 1000
    $start = $Session.SerialText.Length
    Write-Host ("  boot {0}: {1}" -f $Session.Number, $Command)
    foreach ($character in $Command.ToCharArray()) {
        $position = $Session.SerialText.Length
        $oneByte = [byte[]]@([byte][char]$character)
        $Session.SerialChannel.Stream.Write($oneByte, 0, 1)
        Wait-UmicomKernelSerial -Session $Session -Start $position -Expected ([string]$character) `
            -Deadline $deadline -Echo
    }
    # A single CR submits the command. Sending CRLF would risk treating LF as
    # another line if a future line editor changes its CRLF coalescing policy.
    $Session.SerialChannel.Stream.Write([byte[]]@(13), 0, 1)
    if ($PowerOff) {
        while ($true) {
            Read-UmicomKernelAvailableOutput -Session $Session
            $allClosed = $true
            foreach ($channel in $Session.Channels) {
                if (-not $channel.Closed) { $allClosed = $false }
            }
            if ($Session.Process.HasExited -and $allClosed) { break }
            if ($Session.Clock.ElapsedMilliseconds -ge $deadline) {
                throw "QEMU did not close cleanly after poweroff in boot $($Session.Number)."
            }
            Start-Sleep -Milliseconds 2
        }
        if ($Session.Process.ExitCode -ne 0) {
            throw "QEMU exited with code $($Session.Process.ExitCode) in boot $($Session.Number)."
        }
    } else {
        Wait-UmicomKernelSerial -Session $Session -Start ($start + $Command.Length) `
            -Expected 'umicom> ' -Deadline $deadline
    }
    $response = $Session.SerialText.ToString($start, $Session.SerialText.Length - $start)
    $normalised = $response.Replace("`r`n", "`n").Replace("`r", "`n")
    if (-not $normalised.StartsWith($Command + "`n", [System.StringComparison]::Ordinal)) {
        throw "The complete command echo was not preserved in boot $($Session.Number)."
    }
    $body = $normalised.Substring($Command.Length + 1)
    if (-not $PowerOff) { $body = $body.Substring(0, $body.Length - 'umicom> '.Length) }
    [void]$Session.Record.commands.Add([pscustomobject]@{
        command = $Command
        response = $body
    })
    return $body
}

function Assert-UmicomKernelLine {
    param([string]$Text, [string]$Expected)
    $lines = @($Text -split "`n")
    if (@($lines | Where-Object { $_ -ceq $Expected }).Count -ne 1) {
        throw "Expected exactly one console line '$Expected'. See the retained serial log."
    }
}

function Assert-UmicomKernelOperation {
    param([string]$Text, [string]$Operation, [int]$CommittedOperations)
    Assert-UmicomKernelLine -Text $Text `
        -Expected ("{0}=ok committed-operations={1}" -f $Operation, $CommittedOperations)
}

function Stop-UmicomKernelSession {
    param($Session)
    if ($null -eq $Session) { return }
    # Only this invocation's Process and listener are owned here. In particular,
    # never kill by name: another QEMU may be doing unrelated work for the user.
    $cleanupErrors = [System.Collections.Generic.List[string]]::new()
    try {
        if ($null -ne $Session.Process) {
            if ($Session.Started -and -not $Session.Process.HasExited) {
                $Session.Process.Kill()
                if (-not $Session.Process.WaitForExit(5000)) {
                    throw 'The owned QEMU process did not exit after termination.'
                }
                $Session.Record.terminatedAfterFailure = $true
            }
        }
    } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    # Drain completed reads before disposing any channel, so a completed serial
    # read is not hidden by an already-disposed stdout/stderr stream.
    try { Read-UmicomKernelAvailableOutput -Session $Session } catch { }
    try {
        if ($null -ne $Session.Listener) { $Session.Listener.Stop() }
    } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    try {
        if ($null -ne $Session.Client) { $Session.Client.Close() }
    } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    foreach ($channel in $Session.Channels) {
        try { $channel.Log.Dispose() } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
        try { $channel.Stream.Dispose() } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    if ($null -ne $Session.Process) {
        try {
            if ($Session.Started -and $Session.Process.HasExited) {
                $Session.Record.exitCode = $Session.Process.ExitCode
            }
        } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
        try { $Session.Process.Dispose() } catch { [void]$cleanupErrors.Add($_.Exception.Message) }
    }
    if ($cleanupErrors.Count -ne 0) { throw ($cleanupErrors -join '; ') }
}

function Start-UmicomKernelSession {
    param([int]$Number, [string]$Executable, [string]$Firmware,
        [string]$Disk, [string]$Directory, $Result)
    $record = [pscustomobject][ordered]@{
        boot = $Number
        slot = $null
        processId = $null
        arguments = @()
        commands = [System.Collections.Generic.List[object]]::new()
        serialLog = Join-Path $Directory ("boot-{0}.serial.log" -f $Number)
        standardOutputLog = Join-Path $Directory ("boot-{0}.qemu.stdout.log" -f $Number)
        standardErrorLog = Join-Path $Directory ("boot-{0}.qemu.stderr.log" -f $Number)
        exitCode = $null
        terminatedAfterFailure = $false
    }
    [void]$Result.boots.Add($record)
    $session = [pscustomobject]@{
        Number = $Number
        Record = $record
        Process = $null
        Started = $false
        Listener = $null
        Client = $null
        SerialChannel = $null
        SerialText = [System.Text.StringBuilder]::new()
        Channels = [System.Collections.Generic.List[object]]::new()
        Clock = [System.Diagnostics.Stopwatch]::StartNew()
    }
    try {
        # Binding before starting QEMU reserves the endpoint without a gap in
        # which another process could acquire a supposedly free port. QEMU is
        # the client, and only 127.0.0.1 is listened on.
        $session.Listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Loopback, 0)
        $session.Listener.Start(1)
        $port = $session.Listener.LocalEndpoint.Port
        $arguments = @(
            '-machine', 'virt,aclint=off',
            '-bios', $Firmware,
            '-display', 'none',
            '-monitor', 'none',
            '-chardev', "socket,id=console,host=127.0.0.1,port=$port,server=off",
            '-serial', 'chardev:console',
            '-m', '128M',
            '-smp', '1',
            '-no-reboot',
            '-global', 'virtio-mmio.force-legacy=false',
            '-drive', "file=$Disk,if=none,format=raw,id=umicomdisk,readonly=off,cache=writeback,rerror=report,werror=report",
            '-device', 'virtio-blk-device,drive=umicomdisk,write-cache=on,logical_block_size=512,physical_block_size=512,num-queues=1'
        )
        $record.arguments = $arguments
        $start = [System.Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $Executable
        $start.Arguments = (@($arguments | ForEach-Object {
            ConvertTo-UmicomKernelProcessArgument -Value $_
        }) -join ' ')
        $start.WorkingDirectory = $Directory
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardInput = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $session.Process = [System.Diagnostics.Process]::new()
        $session.Process.StartInfo = $start
        if (-not $session.Process.Start()) { throw 'QEMU did not start.' }
        $session.Started = $true
        $record.processId = $session.Process.Id
        $session.Process.StandardInput.Close()
        [void]$session.Channels.Add((New-UmicomKernelReadChannel -Name 'stdout' `
            -Stream $session.Process.StandardOutput.BaseStream -LogPath $record.standardOutputLog))
        [void]$session.Channels.Add((New-UmicomKernelReadChannel -Name 'stderr' `
            -Stream $session.Process.StandardError.BaseStream -LogPath $record.standardErrorLog))
        $deadline = $session.Clock.ElapsedMilliseconds + [long]$TimeoutSeconds * 1000
        while (-not $session.Listener.Pending()) {
            Read-UmicomKernelAvailableOutput -Session $session
            if ($session.Process.HasExited) {
                throw "QEMU exited before connecting serial (code $($session.Process.ExitCode))."
            }
            if ($session.Clock.ElapsedMilliseconds -ge $deadline) {
                throw 'QEMU did not connect to the private serial listener before the deadline.'
            }
            Start-Sleep -Milliseconds 2
        }
        $session.Client = $session.Listener.AcceptTcpClient()
        $session.Client.NoDelay = $true
        $session.Listener.Stop()
        $serialStream = $session.Client.GetStream()
        $serialStream.WriteTimeout = 1000
        $session.SerialChannel = New-UmicomKernelReadChannel -Name 'serial' `
            -Stream $serialStream -LogPath $record.serialLog
        [void]$session.Channels.Add($session.SerialChannel)
        Wait-UmicomKernelSerial -Session $session -Start 0 -Expected 'umicom> ' `
            -Deadline ($session.Clock.ElapsedMilliseconds + [long]$TimeoutSeconds * 1000)
        return $session
    } catch {
        Stop-UmicomKernelSession -Session $session
        throw
    }
}

function Get-UmicomKernelBlockSlot {
    param($Session)
    $response = Send-UmicomKernelCommand -Session $Session -Command 'disks'
    Assert-UmicomKernelLine -Text $response -Expected 'block.domain=ok'
    $found = [regex]::Matches($response,
        '(?m)^block\.slot=([0-7]) base=0x[0-9a-fA-F]+ probe=ok device-id=2 held-frames=0$')
    $present = [regex]::Matches($response, '(?m)^block\.slot=.* probe=ok .*device-id=[1-9][0-9]* ')
    if ($found.Count -ne 1 -or $present.Count -ne 1) {
        throw 'Expected exactly one successfully probed block device in the disposable QEMU machine.'
    }
    $slot = [int]$found[0].Groups[1].Value
    $Session.Record.slot = $slot
    return $slot
}

function Test-UmicomKernelMountedFile {
    param($Session, [int]$CommittedOperations)
    $response = Send-UmicomKernelCommand -Session $Session -Command 'diskrwcat /WORK/NOTE.TXT'
    Assert-UmicomKernelLine -Text $response -Expected 'hello world'
    Assert-UmicomKernelOperation -Text $response -Operation 'diskrwcat' `
        -CommittedOperations $CommittedOperations
    $response = Send-UmicomKernelCommand -Session $Session -Command 'diskrwls /WORK'
    Assert-UmicomKernelLine -Text $response -Expected 'file NOTE.TXT bytes=11'
    Assert-UmicomKernelOperation -Text $response -Operation 'diskrwls' `
        -CommittedOperations $CommittedOperations
    $response = Send-UmicomKernelCommand -Session $Session -Command 'diskrwinfo'
    $accepted = $(if ($CommittedOperations -eq 0) { 0 } else { 1 })
    Assert-UmicomKernelLine -Text $response `
        -Expected ("diskrw.state=mounted clients=1 last-commit-accepted={0}" -f $accepted)
    Assert-UmicomKernelOperation -Text $response -Operation 'diskrwinfo' `
        -CommittedOperations $CommittedOperations
}

$umicomKernelSession = $null
$umicomKernelRunDirectory = $null
$umicomKernelResultPath = $null
$umicomKernelExitCode = 1
$umicomKernelExpectedFixtureHash = '538d4c1def74b0241a91a350362038a85934a9a5d522f86bd7c5347b44c3e1b5'
$umicomKernelResult = [pscustomobject][ordered]@{
    schemaVersion = 1
    test = 'normal-firmware-writable-console-persistence'
    startedUtc = [DateTime]::UtcNow.ToString('o')
    finishedUtc = $null
    passed = $false
    error = $null
    powerShellVersion = $PSVersionTable.PSVersion.ToString()
    operatingSystem = [System.Environment]::OSVersion.ToString()
    qemu = $null
    firmware = $null
    firmwareSha256 = $null
    fixture = $null
    fixtureExpectedSha256 = $umicomKernelExpectedFixtureHash
    fixtureInitialSha256 = $null
    fixtureFinalSha256 = $null
    disk = $null
    diskInitialSha256 = $null
    diskAfterWriteSha256 = $null
    diskAfterReadbackSha256 = $null
    syntaxDiagnostics = [bool]$CheckSyntaxDiagnostics
    timeoutSeconds = $TimeoutSeconds
    boots = [System.Collections.Generic.List[object]]::new()
}

try {
    $umicomKernelRepository = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
    $umicomKernelBuild = Get-UmicomKernelAbsolutePath -Path $BuildDirectory `
        -BaseDirectory $umicomKernelRepository
    if ($umicomKernelBuild.Contains(',')) {
        throw 'The build directory contains a comma, which conflicts with QEMU drive options. Choose a build path without commas.'
    }
    $umicomKernelFirmware = Join-Path $umicomKernelBuild 'bin/umicom-system.elf'
    if (-not [System.IO.File]::Exists($umicomKernelFirmware)) {
        throw "Normal firmware not found: $umicomKernelFirmware. Build riscv64-clang-debug first, or set -BuildDirectory."
    }
    if ([string]::IsNullOrWhiteSpace($QemuPath)) {
        $umicomKernelWindowsQemu = 'C:\msys64\ucrt64\bin\qemu-system-riscv64.exe'
        if ([System.IO.File]::Exists($umicomKernelWindowsQemu)) {
            $QemuPath = $umicomKernelWindowsQemu
        } else {
            $umicomKernelQemuCommand = Get-Command -Name 'qemu-system-riscv64' `
                -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -eq $umicomKernelQemuCommand) {
                throw 'qemu-system-riscv64 was not found. Supply the executable with -QemuPath.'
            }
            $QemuPath = $umicomKernelQemuCommand.Source
        }
    }
    $umicomKernelQemu = Get-UmicomKernelAbsolutePath -Path $QemuPath `
        -BaseDirectory $umicomKernelRepository
    if (-not [System.IO.File]::Exists($umicomKernelQemu)) {
        throw "QEMU executable not found: $umicomKernelQemu"
    }
    $umicomKernelFixture = Join-Path $umicomKernelRepository 'tests/fat16_file_commit/fixture.raw'
    if (-not [System.IO.File]::Exists($umicomKernelFixture)) {
        throw "The repository's synthetic FAT16 fixture is missing: $umicomKernelFixture"
    }
    $umicomKernelResult.qemu = $umicomKernelQemu
    $umicomKernelResult.firmware = $umicomKernelFirmware
    $umicomKernelResult.firmwareSha256 = (Get-FileHash -LiteralPath $umicomKernelFirmware -Algorithm SHA256).Hash.ToLowerInvariant()
    $umicomKernelResult.fixture = $umicomKernelFixture
    $umicomKernelResult.fixtureInitialSha256 = (Get-FileHash -LiteralPath $umicomKernelFixture -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($umicomKernelResult.fixtureInitialSha256 -cne $umicomKernelExpectedFixtureHash) {
        throw 'The immutable source fixture hash differs from the qualified synthetic image. No writable test disk was opened.'
    }
    $umicomKernelRunName = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
    $umicomKernelRunDirectory = Join-Path (Join-Path $umicomKernelBuild 'console-persistence') $umicomKernelRunName
    if ([System.IO.Directory]::Exists($umicomKernelRunDirectory)) {
        throw 'The unique test output directory already exists; refusing to reuse it.'
    }
    [void][System.IO.Directory]::CreateDirectory($umicomKernelRunDirectory)
    $umicomKernelResultPath = Join-Path $umicomKernelRunDirectory 'result.json'
    $umicomKernelDisk = Join-Path $umicomKernelRunDirectory 'writable-console.raw'
    [System.IO.File]::Copy($umicomKernelFixture, $umicomKernelDisk, $false)
    $umicomKernelResult.disk = $umicomKernelDisk
    $umicomKernelResult.diskInitialSha256 = (Get-FileHash -LiteralPath $umicomKernelDisk -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($umicomKernelResult.diskInitialSha256 -cne $umicomKernelExpectedFixtureHash) {
        throw 'The disposable copy did not match the source fixture; QEMU was not started.'
    }
    Write-Host "Testing normal Umicom firmware. Evidence: $umicomKernelRunDirectory"
    Write-Host 'This run uses a fresh disposable disk; no commands need to be pasted into QEMU.'

    for ($umicomKernelBoot = 1; $umicomKernelBoot -le 2; ++$umicomKernelBoot) {
        $umicomKernelSession = Start-UmicomKernelSession -Number $umicomKernelBoot `
            -Executable $umicomKernelQemu -Firmware $umicomKernelFirmware -Disk $umicomKernelDisk `
            -Directory $umicomKernelRunDirectory -Result $umicomKernelResult
        $umicomKernelSlot = Get-UmicomKernelBlockSlot -Session $umicomKernelSession
        if ($CheckSyntaxDiagnostics -and $umicomKernelBoot -eq 1) {
            $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession `
                -Command ("mountdiskrw {0} @ 2044-02-29T23:58:56" -f $umicomKernelSlot)
            Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'mountdiskrw=invalid-argument field=partition'
            Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'PARTITION must be decimal 0..3; use 0 for the first partition.'
            Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'usage: mountdiskrw SLOT PARTITION YYYY-MM-DDTHH:MM:SS'
            $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession -Command 'diskrwinfo'
            Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'diskrw.state=unused clients=0 last-commit-accepted=0'
            Assert-UmicomKernelOperation -Text $umicomKernelResponse -Operation 'diskrwinfo' -CommittedOperations 0
        }
        $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession `
            -Command ("mountdiskrw {0} 0 2044-02-29T23:58:56" -f $umicomKernelSlot)
        Assert-UmicomKernelOperation -Text $umicomKernelResponse -Operation 'mountdiskrw' -CommittedOperations 0
        if ($umicomKernelBoot -eq 1) {
            $umicomKernelCommands = @(
                @('diskmkdir /WORK', 'diskmkdir'),
                @('diskcreate /WORK/NOTE.TXT', 'diskcreate'),
                @('diskwrite /WORK/NOTE.TXT 0 "hello"', 'diskwrite'),
                @('diskappend /WORK/NOTE.TXT " world"', 'diskappend')
            )
            $umicomKernelCount = 0
            foreach ($umicomKernelOperation in $umicomKernelCommands) {
                $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession `
                    -Command $umicomKernelOperation[0]
                ++$umicomKernelCount
                Assert-UmicomKernelOperation -Text $umicomKernelResponse `
                    -Operation $umicomKernelOperation[1] -CommittedOperations $umicomKernelCount
            }
        } else { $umicomKernelCount = 0 }
        Test-UmicomKernelMountedFile -Session $umicomKernelSession -CommittedOperations $umicomKernelCount
        $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession -Command 'unmountdiskrw'
        Assert-UmicomKernelOperation -Text $umicomKernelResponse -Operation 'unmountdiskrw' `
            -CommittedOperations $umicomKernelCount
        $umicomKernelResponse = Send-UmicomKernelCommand -Session $umicomKernelSession -Command 'poweroff' -PowerOff
        Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'RAM filesystem closed. Powering off.'
        Assert-UmicomKernelLine -Text $umicomKernelResponse -Expected 'UMICOM_KERNEL_END'
        Stop-UmicomKernelSession -Session $umicomKernelSession
        $umicomKernelSession = $null
        $umicomKernelHash = (Get-FileHash -LiteralPath $umicomKernelDisk -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($umicomKernelBoot -eq 1) {
            $umicomKernelResult.diskAfterWriteSha256 = $umicomKernelHash
            if ($umicomKernelHash -ceq $umicomKernelExpectedFixtureHash) {
                throw 'The writer boot reported acceptance without changing the disposable disk image.'
            }
        } else {
            $umicomKernelResult.diskAfterReadbackSha256 = $umicomKernelHash
            if ($umicomKernelHash -cne $umicomKernelResult.diskAfterWriteSha256) {
                throw 'The second boot changed disk bytes during mount/read/list/unmount.'
            }
        }
    }
    $umicomKernelResult.passed = $true
    $umicomKernelExitCode = 0
} catch {
    $umicomKernelResult.error = $_.Exception.Message
    Write-Host ("FAIL: {0}" -f $umicomKernelResult.error) -ForegroundColor Red
} finally {
    try { Stop-UmicomKernelSession -Session $umicomKernelSession } catch {
        $umicomKernelResult.passed = $false
        $umicomKernelExitCode = 1
        $umicomKernelResult.error = "{0}; cleanup: {1}" -f $umicomKernelResult.error, $_.Exception.Message
        Write-Host ("FAIL: cleanup: {0}" -f $_.Exception.Message) -ForegroundColor Red
    }
    if ($null -ne $umicomKernelResult.fixture) {
        try {
            $umicomKernelResult.fixtureFinalSha256 = (Get-FileHash -LiteralPath $umicomKernelResult.fixture -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($umicomKernelResult.fixtureFinalSha256 -cne $umicomKernelExpectedFixtureHash) {
                throw 'The immutable source fixture hash does not match after the run.'
            }
        } catch {
            $umicomKernelResult.passed = $false
            $umicomKernelExitCode = 1
            $umicomKernelResult.error = "{0}; fixture verification: {1}" -f $umicomKernelResult.error, $_.Exception.Message
            Write-Host ("FAIL: fixture verification: {0}" -f $_.Exception.Message) -ForegroundColor Red
        }
    }
    $umicomKernelResult.finishedUtc = [DateTime]::UtcNow.ToString('o')
    if ($null -ne $umicomKernelResultPath) {
        try {
            $umicomKernelJson = $umicomKernelResult | ConvertTo-Json -Depth 8
            [System.IO.File]::WriteAllText($umicomKernelResultPath, $umicomKernelJson + "`n",
                [System.Text.UTF8Encoding]::new($false))
        } catch {
            $umicomKernelResult.passed = $false
            $umicomKernelExitCode = 1
            Write-Host ("FAIL: Could not write result.json: {0}" -f $_.Exception.Message) -ForegroundColor Red
        }
        Write-Host "Results: $umicomKernelResultPath"
        Write-Host "Disk and logs retained: $umicomKernelRunDirectory"
    }
}
if ($umicomKernelExitCode -eq 0) {
    Write-Host 'PASS: hello world (11 bytes) survived a clean reboot; four mutations were accepted.' -ForegroundColor Green
    Write-Host 'PASS: the readback boot left the complete image unchanged, and the source fixture is unchanged.' -ForegroundColor Green
}
exit $umicomKernelExitCode
