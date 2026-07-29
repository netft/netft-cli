$ErrorActionPreference = "Stop"

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ($env:NETFT_PLATFORM_BUILD_DIR) {
    $BuildDirectory = $env:NETFT_PLATFORM_BUILD_DIR
} else {
    $BuildDirectory = Join-Path $RepositoryRoot "build/platform-windows"
}

cmake -S $RepositoryRoot -B $BuildDirectory -DBUILD_TESTING=ON
cmake --build $BuildDirectory --config Release
ctest --test-dir $BuildDirectory -C Release --output-on-failure
