$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$curlVersion = "8.21.0"
$curlArchiveSha256 = "aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6"
$curlSourceUri = [Uri] "https://curl.se/download/curl-$curlVersion.tar.xz"
if (-not $env:NETFT_CLI_CURL_PREFIX) {
    throw "NETFT_CLI_CURL_PREFIX is required"
}
$prefix = $env:NETFT_CLI_CURL_PREFIX
$buildJobs = if ($env:NETFT_CLI_BUILD_JOBS) {
    [int] $env:NETFT_CLI_BUILD_JOBS
} else {
    2
}
if ($buildJobs -lt 1) {
    throw "NETFT_CLI_BUILD_JOBS must be a positive integer"
}
if ($curlSourceUri.Scheme -ne "https") {
    throw "curl source URL must use HTTPS"
}

$buildRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("netft-cli-curl-" + [guid]::NewGuid())
$archive = if ($env:NETFT_CLI_CURL_ARCHIVE_CACHE) {
    $env:NETFT_CLI_CURL_ARCHIVE_CACHE
} else {
    Join-Path $buildRoot "curl-$curlVersion.tar.xz"
}
$sourceRoot = Join-Path $buildRoot "source"
$binaryRoot = Join-Path $buildRoot "build"

function Get-Sha256([string] $path) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($path)
    try {
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "unable to locate vswhere.exe"
}
$visualStudioVersion = (
    & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationVersion
).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudioVersion) {
    throw "unable to locate a Visual Studio C++ toolchain"
}
$visualStudioGenerator = if ($visualStudioVersion.StartsWith("18.")) {
    "Visual Studio 18 2026"
} elseif ($visualStudioVersion.StartsWith("17.")) {
    "Visual Studio 17 2022"
} else {
    throw "unsupported Visual Studio version: $visualStudioVersion"
}

try {
    New-Item -ItemType Directory -Force -Path $buildRoot, $sourceRoot | Out-Null
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        $archiveParent = Split-Path -Parent $archive
        if ($archiveParent) {
            New-Item -ItemType Directory -Force -Path $archiveParent | Out-Null
        }
        Invoke-WebRequest $curlSourceUri -OutFile $archive -MaximumRedirection 0
    }
    if ((Get-Sha256 $archive) -ne $curlArchiveSha256) {
        throw "curl source archive checksum mismatch"
    }

    tar -xJf $archive --strip-components=1 -C $sourceRoot
    if ($LASTEXITCODE -ne 0) {
        throw "unable to extract curl source archive"
    }

    cmake -S $sourceRoot -B $binaryRoot -G $visualStudioGenerator -A x64 `
        -DCMAKE_INSTALL_PREFIX="$prefix" `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
        -DBUILD_CURL_EXE=OFF `
        -DBUILD_SHARED_LIBS=OFF `
        -DBUILD_STATIC_LIBS=ON `
        -DBUILD_TESTING=OFF `
        -DHTTP_ONLY=ON `
        -DCURL_USE_SCHANNEL=OFF `
        -DCURL_USE_LIBPSL=OFF `
        -DCURL_ZLIB=OFF `
        -DCURL_BROTLI=OFF `
        -DCURL_ZSTD=OFF `
        -DUSE_LIBIDN2=OFF `
        -DUSE_NGHTTP2=OFF
    if ($LASTEXITCODE -ne 0) {
        throw "unable to configure curl"
    }
    cmake --build $binaryRoot --config Release --parallel $buildJobs
    if ($LASTEXITCODE -ne 0) {
        throw "unable to build curl"
    }
    cmake --install $binaryRoot --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "unable to install curl"
    }

    $installedLibrary = Join-Path $prefix "lib\libcurl.lib"
    $staticLibrary = Join-Path $prefix "lib\libcurl_a.lib"
    if ((Test-Path -LiteralPath $installedLibrary -PathType Leaf) -and
        (Test-Path -LiteralPath $staticLibrary -PathType Leaf)) {
        throw "curl installed ambiguous static and import-style libraries"
    }
    if (Test-Path -LiteralPath $installedLibrary -PathType Leaf) {
        Move-Item -LiteralPath $installedLibrary -Destination $staticLibrary
    }
    if (-not (Test-Path -LiteralPath $staticLibrary -PathType Leaf)) {
        throw "curl static library was not installed"
    }
    if (Test-Path -LiteralPath $installedLibrary) {
        throw "curl import-style library was unexpectedly retained"
    }
    if (Get-ChildItem -LiteralPath $prefix -Recurse -Filter "libcurl.dll") {
        throw "curl shared library was unexpectedly installed"
    }
}
finally {
    if (Test-Path -LiteralPath $buildRoot) {
        Remove-Item -LiteralPath $buildRoot -Recurse -Force
    }
}
