# Apex chess engine: hardware / toolchain report for Windows (milestone M0).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage (PowerShell, from the repository root):
#   powershell -ExecutionPolicy Bypass -File tools\hwinfo.ps1 [-Engine build\Release\apex.exe] > hwinfo-windows.txt
# Paste or commit the output; it becomes the machine record referenced by BENCHMARKS.md.
param([string]$Engine = "")

function Try-Run([string]$exe, [string[]]$argv) {
    try {
        $cmd = Get-Command $exe -ErrorAction Stop
        $out = & $cmd.Source @argv 2>&1 | Select-Object -First 3
        return ($out -join " | ")
    } catch { return "not found" }
}

"=== Apex hardware report (Windows) ==="
"date: $(Get-Date -Format o)"

$os = Get-CimInstance Win32_OperatingSystem
"os: $($os.Caption) $($os.Version) build $($os.BuildNumber) ($($os.OSArchitecture))"

foreach ($cpu in Get-CimInstance Win32_Processor) {
    "cpu: $($cpu.Name.Trim())"
    "cores: $($cpu.NumberOfCores) physical, $($cpu.NumberOfLogicalProcessors) logical"
    "max clock: $($cpu.MaxClockSpeed) MHz; L2: $($cpu.L2CacheSize) KB; L3: $($cpu.L3CacheSize) KB"
}

$cs = Get-CimInstance Win32_ComputerSystem
"ram: {0:N1} GB" -f ($cs.TotalPhysicalMemory / 1GB)
$dimms = Get-CimInstance Win32_PhysicalMemory
foreach ($d in $dimms) { "  dimm: {0:N0} GB @ {1} MT/s" -f ($d.Capacity / 1GB), $d.ConfiguredClockSpeed }

"--- gpu ---"
foreach ($g in Get-CimInstance Win32_VideoController) { "gpu: $($g.Name) driver $($g.DriverVersion)" }
"nvidia-smi: " + (Try-Run "nvidia-smi" @("--query-gpu=name,memory.total,driver_version,compute_cap", "--format=csv,noheader"))
"nvcc: " + (Try-Run "nvcc" @("--version"))
"CUDA_PATH: $env:CUDA_PATH"

"--- toolchain ---"
"cmake: " + (Try-Run "cmake" @("--version"))
"cl: " + (Try-Run "cl" @())
"clang-cl: " + (Try-Run "clang-cl" @("--version"))
"clang++: " + (Try-Run "clang++" @("--version"))
"g++: " + (Try-Run "g++" @("--version"))
"ninja: " + (Try-Run "ninja" @("--version"))
"python: " + (Try-Run "python" @("--version"))
"git: " + (Try-Run "git" @("--version"))
"cargo (for bullet trainer): " + (Try-Run "cargo" @("--version"))

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswhere) { "visual studio: " + (& $vswhere -latest -property displayName) + " " + (& $vswhere -latest -property installationVersion) }

if ($Engine -and (Test-Path $Engine)) {
    "--- engine cpuid report ---"
    & $Engine cpuinfo
}
