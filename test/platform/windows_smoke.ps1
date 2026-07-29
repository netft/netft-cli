$ErrorActionPreference = "Stop"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        $ExitCode = $LASTEXITCODE
        [Console]::Error.WriteLine("$FilePath exited with status $ExitCode")
        exit $ExitCode
    }
}

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ($env:NETFT_PLATFORM_BUILD_DIR) {
    $BuildDirectory = $env:NETFT_PLATFORM_BUILD_DIR
} else {
    $BuildDirectory = Join-Path $RepositoryRoot "build/platform-windows"
}

Invoke-NativeChecked -FilePath "cmake" -ArgumentList @(
    "-S", $RepositoryRoot,
    "-B", $BuildDirectory,
    "-DBUILD_TESTING=ON"
)
Invoke-NativeChecked -FilePath "cmake" -ArgumentList @(
    "--build", $BuildDirectory,
    "--config", "Release"
)
Invoke-NativeChecked -FilePath "ctest" -ArgumentList @(
    "--test-dir", $BuildDirectory,
    "-C", "Release",
    "--output-on-failure"
)
