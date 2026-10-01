# Umicom Foundation
# PowerShell-first repository creation guide. No feature branches are created.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = "C:\umicom\umicom-kernel"

if (Test-Path $Root) {
    throw "Destination already exists: $Root"
}

New-Item -ItemType Directory -Path $Root | Out-Null
Write-Host "Created $Root"
Write-Host "Extract/copy the supplied foundation-pack files into this folder."
Write-Host ""
Write-Host 'Then run:'
Write-Host 'Set-Location "C:\umicom\umicom-kernel"'
Write-Host 'git init -b main'
Write-Host 'git add -A'
Write-Host 'git commit -m "docs(kernel): establish architecture and native-kernel roadmap"'
Write-Host 'gh repo create umicom-foundation/umicom-kernel --public --source . --remote origin --push --description "Original C23 hybrid-microkernel and native kernel research for Umicom OS."'
Write-Host 'gh repo edit umicom-foundation/umicom-kernel --add-topic umicom --add-topic kernel --add-topic c23 --add-topic riscv --add-topic operating-system --add-topic free-software'
Write-Host 'git status'
Write-Host 'git remote -v'
