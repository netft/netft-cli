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

function Set-ZipCentralLength {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][uint32]$Length
    )

    $Bytes = [IO.File]::ReadAllBytes($Path)
    $EndOffset = -1
    for ($Offset = $Bytes.Length - 22; $Offset -ge 0; $Offset--) {
        if ($Bytes[$Offset] -eq 0x50 -and
            $Bytes[$Offset + 1] -eq 0x4b -and
            $Bytes[$Offset + 2] -eq 0x05 -and
            $Bytes[$Offset + 3] -eq 0x06) {
            $EndOffset = $Offset
            break
        }
    }
    if ($EndOffset -lt 0) {
        throw "ZIP fixture end record was not found."
    }
    $EntryCount = [BitConverter]::ToUInt16($Bytes, $EndOffset + 10)
    $Offset = [int][BitConverter]::ToUInt32($Bytes, $EndOffset + 16)
    $Found = $false
    for ($Index = 0; $Index -lt $EntryCount; $Index++) {
        if ($Offset + 46 -gt $Bytes.Length -or
            $Bytes[$Offset] -ne 0x50 -or
            $Bytes[$Offset + 1] -ne 0x4b -or
            $Bytes[$Offset + 2] -ne 0x01 -or
            $Bytes[$Offset + 3] -ne 0x02) {
            throw "ZIP fixture central directory is invalid."
        }
        $NameLength = [BitConverter]::ToUInt16($Bytes, $Offset + 28)
        $ExtraLength = [BitConverter]::ToUInt16($Bytes, $Offset + 30)
        $CommentLength = [BitConverter]::ToUInt16($Bytes, $Offset + 32)
        $EntryName = [Text.Encoding]::UTF8.GetString(
            $Bytes,
            $Offset + 46,
            $NameLength
        )
        if ($EntryName -ceq $Name) {
            $EncodedLength = [BitConverter]::GetBytes($Length)
            [Array]::Copy($EncodedLength, 0, $Bytes, $Offset + 24, 4)
            $Found = $true
        }
        $Offset += 46 + $NameLength + $ExtraLength + $CommentLength
    }
    if (-not $Found) {
        throw "ZIP fixture entry was not found."
    }
    [IO.File]::WriteAllBytes($Path, $Bytes)
}

function New-ReleaseArchive {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Binary,
        [switch]$Unexpected,
        [switch]$Symlink,
        [switch]$Oversized,
        [switch]$ForgedLength
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
        $License = if ($Oversized) {
            [byte[]]::new((32 * 1024 * 1024 + 1))
        } else {
            [Text.Encoding]::UTF8.GetBytes("license")
        }
        Add-ZipFile $Release "$RootName/LICENSE" $License $Regular0644
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
    if ($ForgedLength) {
        Set-ZipCentralLength $Path "$RootName/LICENSE" 7
    }
}

function Publish-Release {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Binary,
        [switch]$Unexpected,
        [switch]$Symlink,
        [switch]$Oversized,
        [switch]$ForgedLength
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
            -Unexpected:$Unexpected -Symlink:$Symlink -Oversized:$Oversized `
            -ForgedLength:$ForgedLength
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
    if ([string]::IsNullOrWhiteSpace($env:NETFT_EXECUTABLE)) {
        throw "NETFT_EXECUTABLE must name the production netft.exe under test."
    }
    $GoodBinary = (Resolve-Path $env:NETFT_EXECUTABLE).Path
    $ProductionVersion = @(& $GoodBinary --version)
    Assert-True (
        $LASTEXITCODE -eq 0 -and
        $ProductionVersion.Count -eq 1 -and
        $ProductionVersion[0] -ceq "netft 0.1.0"
    ) "NETFT_EXECUTABLE is not the production 0.1.0 executable."
    Publish-Release "0.1.0" $GoodBinary

    $Listener = [Net.Sockets.TcpListener]::new(
        [Net.IPAddress]::Loopback,
        0
    )
    $Listener.Start()
    $Port = ([Net.IPEndPoint]$Listener.LocalEndpoint).Port
    $Listener.Stop()
    $Python = (Get-Command python).Source
    $FixtureServer = Join-Path $Temporary "fixture_server.py"
    [IO.File]::WriteAllText(
        $FixtureServer,
@'
import http.server
import pathlib
import sys
import urllib.parse

port = int(sys.argv[1])
root = pathlib.Path(sys.argv[2]).resolve()
redirect = root / "redirect.txt"
request_log = root / "requests.log"
stream = root / "stream.txt"
stream_count = root / "stream-count.txt"

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        with request_log.open("a", encoding="utf-8") as stream:
            stream.write(self.path + "\n")
        if (
            self.path == "/releases/download/v0.1.0/SHA256SUMS"
            and redirect.exists()
        ):
            self.send_response(302)
            self.send_header("Location", redirect.read_text(encoding="utf-8"))
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if (
            self.path == "/releases/download/v0.1.0/SHA256SUMS"
            and stream.exists()
        ):
            self.send_response(200)
            self.send_header("Connection", "close")
            self.end_headers()
            sent = 0
            try:
                while sent < 8 * 1024 * 1024:
                    data = b"x" * 65536
                    self.wfile.write(data)
                    self.wfile.flush()
                    sent += len(data)
                    stream_count.write_text(str(sent), encoding="utf-8")
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True
            return
        relative = urllib.parse.unquote(self.path.split("?", 1)[0]).lstrip("/")
        path = (root / relative).resolve()
        if root not in path.parents or not path.is_file():
            self.send_error(404)
            return
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)
        self.close_connection = True

    def log_message(self, *_args):
        pass

http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
'@
    )
    $ServerOut = Join-Path $Temporary "server.out"
    $ServerErr = Join-Path $Temporary "server.err"
    $Server = Start-Process -FilePath $Python -ArgumentList @(
        "`"$FixtureServer`"",
        "$Port",
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
    $RedirectFile = Join-Path $FixtureRoot "redirect.txt"
    $RequestLog = Join-Path $FixtureRoot "requests.log"

    $CustomBin = Join-Path $Temporary "custom bin"
    $Explicit = Invoke-InstallerProcess -Arguments @(
        "-Version", "v0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    $RequestDiagnostics = if (Test-Path -LiteralPath $RequestLog) {
        "requests: " + [string]::Join(
            ", ",
            [IO.File]::ReadAllLines($RequestLog)
        )
    } else {
        ""
    }
    $ExplicitDiagnostics = @(
        $Explicit.Output,
        $RequestDiagnostics
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    Assert-True ($Explicit.ExitCode -eq 0) (
        "Explicit install failed: " + [string]::Join(" | ", $ExplicitDiagnostics)
    )
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

    $RedirectedDirectory = Join-Path $FixtureRoot "releases\redirected"
    New-Item -ItemType Directory -Path $RedirectedDirectory -Force |
        Out-Null
    Copy-Item -LiteralPath (
        Join-Path $FixtureRoot "releases\download\v0.1.0\SHA256SUMS"
    ) -Destination (Join-Path $RedirectedDirectory "SHA256SUMS")
    [IO.File]::WriteAllText(
        $RedirectFile,
        "$BaseUrl/redirected/SHA256SUMS"
    )
    [IO.File]::WriteAllText($RequestLog, "")
    $SameOriginRedirect = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    $SameOriginRequests = @(
        [IO.File]::ReadAllLines($RequestLog) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
    Assert-True (
        $SameOriginRedirect.ExitCode -eq 0 -and
        $SameOriginRequests.Count -ge 2 -and
        $SameOriginRequests[0] -ceq
            "/releases/download/v0.1.0/SHA256SUMS" -and
        $SameOriginRequests[1] -ceq "/releases/redirected/SHA256SUMS"
    ) "An exact same-origin loopback redirect was not followed."
    Remove-Item -LiteralPath $RedirectFile -Force

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

    Publish-Release "0.1.0" $GoodBinary
    $StreamFlag = Join-Path $FixtureRoot "stream.txt"
    $StreamCount = Join-Path $FixtureRoot "stream-count.txt"
    [IO.File]::WriteAllText($StreamFlag, "")
    Remove-Item -LiteralPath $StreamCount -Force -ErrorAction SilentlyContinue
    $StreamFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Remove-Item -LiteralPath $StreamFlag -Force
    $Transferred = [long][IO.File]::ReadAllText($StreamCount)
    Assert-True (
        $StreamFailure.ExitCode -ne 0 -and
        $Transferred -lt 8 * 1024 * 1024
    ) "An unbounded checksum response was consumed in full."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "An oversized checksum response replaced the previous executable."

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

    Publish-Release "9.9.9" $GoodBinary
    $VersionFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "9.9.9", "-BinDir", $CustomBin, "-NoModifyPath"
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

    Publish-Release "0.1.0" $GoodBinary -Oversized
    $OversizedFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($OversizedFailure.ExitCode -ne 0) `
        "An oversized ZIP member was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "An oversized archive replaced the previous executable."

    Publish-Release "0.1.0" $GoodBinary -Oversized -ForgedLength
    $ForgedLengthFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($ForgedLengthFailure.ExitCode -ne 0) `
        "A ZIP member with forged length metadata was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A forged archive replaced the previous executable."

    Publish-Release "0.1.0" $GoodBinary
    $LockPath = Join-Path $CustomBin ".netft-install.lock"
    $HeldLock = [IO.FileStream]::new(
        $LockPath,
        [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None
    )
    try {
        $LockFailure = Invoke-InstallerProcess -Arguments @(
            "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
        )
    } finally {
        $HeldLock.Dispose()
    }
    Assert-True ($LockFailure.ExitCode -ne 0) `
        "Concurrent installation lock contention was accepted."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "Lock contention changed the previous executable."

    $BrokenPathStore = Join-Path $Temporary "broken-path-store"
    New-Item -ItemType Directory -Path $BrokenPathStore | Out-Null
    $env:NETFT_CLI_TEST_USER_PATH_FILE = $BrokenPathStore
    $PathFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin
    )
    Assert-True ($PathFailure.ExitCode -ne 0) `
        "A failed user PATH update was reported as success."
    Assert-True ([IO.File]::ReadAllText($Installed) -ceq "previous") `
        "A failed user PATH update did not roll back the executable."
    $env:NETFT_CLI_TEST_USER_PATH_FILE = $PathFile

    Remove-Item -LiteralPath $Installed -Force
    New-Item -ItemType Directory -Path $Installed | Out-Null
    $DirectoryFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True (
        $DirectoryFailure.ExitCode -ne 0 -and
        (Test-Path -LiteralPath $Installed -PathType Container)
    ) "An existing directory destination was replaced."
    Remove-Item -LiteralPath $Installed -Recurse -Force

    $JunctionTarget = Join-Path $Temporary "junction-target"
    New-Item -ItemType Directory -Path $JunctionTarget | Out-Null
    New-Item -ItemType Junction -Path $Installed -Target $JunctionTarget |
        Out-Null
    $ReparseFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
    )
    Assert-True ($ReparseFailure.ExitCode -ne 0) `
        "A reparse-point destination was replaced."
    Assert-True (
        ((Get-Item -LiteralPath $Installed).Attributes -band
        [IO.FileAttributes]::ReparsePoint) -ne 0
    ) "The destination reparse point was changed."
    Remove-Item -LiteralPath $Installed -Force
    [IO.File]::WriteAllText($Installed, "previous")

    $RealBin = Join-Path $Temporary "real-bin"
    New-Item -ItemType Directory -Path $RealBin | Out-Null
    $JunctionBin = Join-Path $Temporary "junction-bin"
    New-Item -ItemType Junction -Path $JunctionBin -Target $RealBin |
        Out-Null
    $BinReparseFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $JunctionBin, "-NoModifyPath"
    )
    Assert-True (
        $BinReparseFailure.ExitCode -ne 0 -and
        (Get-ChildItem -LiteralPath $RealBin -Force).Count -eq 0
    ) "A reparse-point installation directory was accepted."

    $OverrideDestination = Join-Path $Temporary "override-destination"
    $env:NETFT_CLI_RELEASE_BASE_URL = "https://github.com/netft/netft-cli/releases"
    $OverrideFailure = Invoke-InstallerProcess -Arguments @(
        "-Version", "0.1.0", "-BinDir", $OverrideDestination,
        "-NoModifyPath"
    )
    Assert-True (
        $OverrideFailure.ExitCode -ne 0 -and
        -not (Test-Path -LiteralPath $OverrideDestination)
    ) "A non-loopback release-base override was accepted."
    $env:NETFT_CLI_RELEASE_BASE_URL = $BaseUrl

    foreach ($RedirectLocation in @(
        "http://localhost:$Port/escaped",
        "http://127.0.0.2:$Port/escaped",
        "http://[::1]:$Port/escaped",
        "http://127.0.0.1:$($Port + 1)/escaped",
        "https://example.com/escaped"
    )) {
        [IO.File]::WriteAllText($RedirectFile, $RedirectLocation)
        [IO.File]::WriteAllText($RequestLog, "")
        $RedirectFailure = Invoke-InstallerProcess -Arguments @(
            "-Version", "0.1.0", "-BinDir", $CustomBin, "-NoModifyPath"
        )
        $Requests = @(
            [IO.File]::ReadAllLines($RequestLog) |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )
        Assert-True (
            $RedirectFailure.ExitCode -ne 0 -and
            $Requests.Count -eq 1 -and
            $Requests[0] -ceq "/releases/download/v0.1.0/SHA256SUMS"
        ) "A loopback release redirect escaped its origin."
    }
    Remove-Item -LiteralPath $RedirectFile -Force

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
