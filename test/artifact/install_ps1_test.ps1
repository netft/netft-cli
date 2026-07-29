$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Installer = Join-Path $Root "scripts\install\install.ps1"
$Temporary = Join-Path ([IO.Path]::GetTempPath()) (
    "netft-installer-test-" + [Guid]::NewGuid().ToString("N")
)
$FixtureRoot = Join-Path $Temporary "fixture"
$Server = $null
$PreviousBaseUrl = $env:NETFT_CLI_RELEASE_BASE_URL
$PreviousPathFile = $env:NETFT_CLI_TEST_USER_PATH_FILE
$PreviousLocalAppData = $env:LOCALAPPDATA

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool]$Condition,
        [Parameter(Mandatory = $true)][string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function New-FakeNetft {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Version
    )

    $TypeName = "NetftProgram" + [Guid]::NewGuid().ToString("N")
    $Source = @"
using System;
public static class $TypeName {
    public static int Main(string[] args) {
        if (args.Length == 1 && args[0] == "--version") {
            Console.WriteLine("netft $Version");
            return 0;
        }
        return 2;
    }
}
"@
    Add-Type -TypeDefinition $Source -Language CSharp -OutputAssembly $Path `
        -OutputType ConsoleApplication
}

function Add-ZipFile {
    param(
        [Parameter(Mandatory = $true)]$Zip,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][byte[]]$Content,
        [Parameter(Mandatory = $true)][int]$Attributes
    )

    $Entry = $Zip.CreateEntry(
        $Name,
        [IO.Compression.CompressionLevel]::Optimal
    )
    $Entry.ExternalAttributes = $Attributes
    $Stream = $Entry.Open()
    try {
        $Stream.Write($Content, 0, $Content.Length)
    } finally {
        $Stream.Dispose()
    }
}

function New-ReleaseArchive {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Binary,
        [switch]$Unexpected,
        [switch]$Symlink
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $RootName = "netft-cli-$Version"
    $Release = [IO.Compression.ZipFile]::Open(
        $Path,
        [IO.Compression.ZipArchiveMode]::Create
    )
    try {
        $Regular0644 = -2119958528
        $Regular0755 = -2115174400
        Add-ZipFile $Release "$RootName/LICENSE" (
            [Text.Encoding]::UTF8.GetBytes("license")
        ) $Regular0644
        Add-ZipFile $Release "$RootName/LICENSES/curl.txt" (
            [Text.Encoding]::UTF8.GetBytes("curl")
        ) $Regular0644
        Add-ZipFile $Release "$RootName/LICENSES/netft-cpp.txt" (
            [Text.Encoding]::UTF8.GetBytes("netft-cpp")
        ) $Regular0644
        $BinaryAttributes = if ($Symlink) { -1577123840 } else { $Regular0755 }
        Add-ZipFile $Release "$RootName/netft.exe" (
            [IO.File]::ReadAllBytes($Binary)
        ) $BinaryAttributes
        if ($Unexpected) {
            Add-ZipFile $Release "$RootName/unexpected" (
                [Text.Encoding]::UTF8.GetBytes("unexpected")
            ) $Regular0644
        }
    } finally {
        $Release.Dispose()
    }
}

function Publish-Release {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Binary,
        [switch]$Unexpected,
        [switch]$Symlink
    )

    $Name = "netft-cli-$Version-windows-x86_64.zip"
    $Directories = @(
        (Join-Path $FixtureRoot "releases\download\v$Version"),
        (Join-Path $FixtureRoot "releases\latest\download")
    )
    foreach ($Directory in $Directories) {
        New-Item -ItemType Directory -Path $Directory -Force | Out-Null
        $Archive = Join-Path $Directory $Name
        Remove-Item -LiteralPath $Archive -Force -ErrorAction SilentlyContinue
        New-ReleaseArchive $Archive $Version $Binary `
            -Unexpected:$Unexpected -Symlink:$Symlink
        $Digest = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
        $Inventory = @(
            "$(('0' * 64))  netft-cli-$Version-linux-x86_64.tar.gz",
            "$(('1' * 64))  netft-cli-$Version-linux-arm64.tar.gz",
            "$(('2' * 64))  netft-cli-$Version-macos-x86_64.tar.gz",
            "$(('3' * 64))  netft-cli-$Version-macos-arm64.tar.gz",
            "$Digest  $Name"
        )
        [IO.File]::WriteAllText(
            (Join-Path $Directory "SHA256SUMS"),
            [string]::Join("`n", $Inventory) + "`n"
        )
    }
}

function Invoke-InstallerProcess {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)

    $PowerShell = (Get-Process -Id $PID).Path
    $Output = @(& $PowerShell -NoProfile -File $Installer @Arguments 2>&1)
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = [string]::Join("`n", $Output)
    }
}

try {
    New-Item -ItemType Directory -Path $FixtureRoot -Force | Out-Null
    $GoodBinary = Join-Path $Temporary "netft-good.exe"
    New-FakeNetft $GoodBinary "0.1.0"
    Publish-Release "0.1.0" $GoodBinary

    $Listener = [Net.Sockets.TcpListener]::new(
        [Net.IPAddress]::Loopback,
        0
    )
    $Listener.Start()
    $Port = ([Net.IPEndPoint]$Listener.LocalEndpoint).Port
    $Listener.Stop()
    $Python = (Get-Command python).Source
    $ServerOut = Join-Path $Temporary "server.out"
    $ServerErr = Join-Path $Temporary "server.err"
    $Server = Start-Process -FilePath $Python -ArgumentList @(
        "-m",
        "http.server",
        "$Port",
        "--bind",
        "127.0.0.1",
        "--directory",
        "`"$FixtureRoot`""
    ) -RedirectStandardOutput $ServerOut -RedirectStandardError $ServerErr `
        -PassThru
    $BaseUrl = "http://127.0.0.1:$Port/releases"
    $ServerReady = $false
    for ($Attempt = 0; $Attempt -lt 100; $Attempt++) {
        try {
            Invoke-WebRequest -Uri "$BaseUrl/latest/download/SHA256SUMS" `
                -UseBasicParsing | Out-Null
            $ServerReady = $true
            break
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    Assert-True ($ServerReady -and -not $Server.HasExited) `
        "Local release fixture failed to start."

    $env:NETFT_CLI_RELEASE_BASE_URL = $BaseUrl
    $env:NETFT_CLI_TEST_USER_PATH_FILE = Join-Path $Temporary "user-path.txt"
    $env:LOCALAPPDATA = Join-Path $Temporary "local-app-data"

    $CustomBin = Join-Path $Temporary "custom bin"
    $Explicit = Invoke-InstallerProcess -Arguments @(
        "-Version", "v0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($Explicit.ExitCode -eq 0) "Explicit install failed."
    Assert-True (-not (Test-Path -LiteralPath $env:NETFT_CLI_TEST_USER_PATH_FILE)) `
        "-NoModifyPath unexpectedly changed the user PATH fixture."
    $Installed = Join-Path $CustomBin "netft.exe"
    Assert-True (Test-Path -LiteralPath $Installed -PathType Leaf) `
        "Explicit install did not create netft.exe."
    $InstalledVersion = @(& $Installed --version)
    Assert-True (
        $LASTEXITCODE -eq 0 -and
        $InstalledVersion.Count -eq 1 -and
        $InstalledVersion[0] -ceq "netft 0.1.0"
    ) "Installed executable reported the wrong version."

    $PathFile = $env:NETFT_CLI_TEST_USER_PATH_FILE
    [IO.File]::WriteAllText(
        $PathFile,
        "$CustomBin;$($CustomBin.ToUpperInvariant());C:\Tools"
    )
    $Latest = Invoke-InstallerProcess -Arguments @("-BinDir", $CustomBin)
    Assert-True ($Latest.ExitCode -eq 0) "Latest stable install failed."
    $UserPath = [IO.File]::ReadAllText($PathFile).Split(";")
    $BinOccurrences = @(
        $UserPath | Where-Object {
            [IO.Path]::GetFullPath($_) -ieq [IO.Path]::GetFullPath($CustomBin)
        }
    )
    Assert-True ($BinOccurrences.Count -eq 1) `
        "User PATH was not normalized and deduplicated."
    Assert-True ($UserPath -contains "C:\Tools") `
        "An unrelated user PATH entry was changed."

    $DefaultInstall = Invoke-InstallerProcess -Arguments @("-NoModifyPath")
    $DefaultBinary = Join-Path $env:LOCALAPPDATA "netft\bin\netft.exe"
    Assert-True (
        $DefaultInstall.ExitCode -eq 0 -and
        (Test-Path -LiteralPath $DefaultBinary -PathType Leaf)
    ) "The default LOCALAPPDATA destination was not installed."

    [IO.File]::WriteAllText($Installed, "previous")
    $ReleaseDirectory = Join-Path $FixtureRoot "releases\download\v0.1.0"
    $ReleaseArchive = Join-Path $ReleaseDirectory (
        "netft-cli-0.1.0-windows-x86_64.zip"
    )
    [IO.File]::AppendAllText($ReleaseArchive, "corrupt")
    $ChecksumFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($ChecksumFailure.ExitCode -ne 0) `
        "A checksum mismatch was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A failed checksum update replaced the previous executable."

    Publish-Release "0.1.0" $GoodBinary -Unexpected
    $ArchiveFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($ArchiveFailure.ExitCode -ne 0) `
        "An archive with an unexpected payload was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A failed archive update replaced the previous executable."

    Publish-Release "0.1.0" $GoodBinary -Symlink
    $SymlinkFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($SymlinkFailure.ExitCode -ne 0) `
        "A symbolic-link archive member was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A rejected link archive replaced the previous executable."

    $WrongBinary = Join-Path $Temporary "netft-wrong.exe"
    New-FakeNetft $WrongBinary "9.9.9"
    Publish-Release "0.1.0" $WrongBinary
    $VersionFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($VersionFailure.ExitCode -ne 0) `
        "An executable reporting the wrong version was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A failed version check replaced the previous executable."

    Publish-Release "0.1.0" $GoodBinary
    $ChecksumPath = Join-Path $ReleaseDirectory "SHA256SUMS"
    $FirstChecksum = [IO.File]::ReadAllLines($ChecksumPath)[0]
    [IO.File]::AppendAllText($ChecksumPath, "$FirstChecksum`n")
    $DuplicateFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($DuplicateFailure.ExitCode -ne 0) `
        "A duplicate checksum entry was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A rejected checksum inventory replaced the previous executable."

    $UnsafeVersion = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0;invalid", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($UnsafeVersion.ExitCode -ne 0) `
        "An unsafe version was accepted."

    Write-Output "PowerShell installer tests passed."
} finally {
    if ($Server -and -not $Server.HasExited) {
        Stop-Process -Id $Server.Id -Force
        $Server.WaitForExit()
    }
    $env:NETFT_CLI_RELEASE_BASE_URL = $PreviousBaseUrl
    $env:NETFT_CLI_TEST_USER_PATH_FILE = $PreviousPathFile
    $env:LOCALAPPDATA = $PreviousLocalAppData
    if (Test-Path -LiteralPath $Temporary) {
        Remove-Item -LiteralPath $Temporary -Recurse -Force
    }
}
