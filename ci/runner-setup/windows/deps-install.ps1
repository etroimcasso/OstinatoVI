# Windows runner dependency installer (WINDOWS-BVDA56O — x64).
# Run once in PowerShell as Administrator. Installs VS 2022 Build Tools (MSVC + Windows SDK +
# the LLVM/clang-cl toolset), CMake, Ninja, and Git via winget.
# After running, open a NEW PowerShell window so PATH reflects new installs, then verify.
#
# CI configures with `-T ClangCL`, so the "C++ Clang tools for Windows" components are required
# in addition to the base VC toolchain.
#Requires -RunAsAdministrator

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Write-Host "Installing Visual Studio 2022 Build Tools (MSVC + Windows SDK + clang-cl toolset)..."
winget install --id Microsoft.VisualStudio.2022.BuildTools `
    --silent --accept-source-agreements --accept-package-agreements `
    --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset --includeRecommended"

Write-Host "Installing CMake..."
winget install --id Kitware.CMake --silent --accept-source-agreements --accept-package-agreements

Write-Host "Installing Ninja..."
winget install --id Ninja-build.Ninja --silent --accept-source-agreements --accept-package-agreements

Write-Host "Installing Git..."
winget install --id Git.Git --silent --accept-source-agreements --accept-package-agreements

Write-Host ""
Write-Host "Done. Open a NEW PowerShell window and verify:"
Write-Host "  cmake --version"
Write-Host "  ninja --version"
Write-Host "  clang-cl --version  (run from a Developer PowerShell for VS 2022)"
