[CmdletBinding()]
param(
    [string]$Version,
    [string]$BinDir,
    [switch]$NoModifyPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Program = "netft.exe"
$DefaultReleaseBaseUrl = "https://github.com/netft/netft-cli/releases"
$MaxChecksumBytes = 1MB
$MaxArchiveBytes = 64MB
$MaxMemberBytes = 32MB
$MaxExpandedBytes = 48MB
$MaxRedirects = 5
$StableVersionPattern = "^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$"
$AssetPattern = "^netft-cli-(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-((linux-(x86_64|arm64)|macos-(x86_64|arm64))\.tar\.gz|windows-x86_64\.zip)$"
$Temporary = $null
$IsLoopbackFixture = $false
$Destination = $null
$Backup = $null
$ReplacementCompleted = $false
$HadPreviousBinary = $false
$PathUpdateAttempted = $false
$OriginalUserPath = $null
$InstallLock = $null
$VersionWasProvided = $PSBoundParameters.ContainsKey("Version")
$BinDirWasProvided = $PSBoundParameters.ContainsKey("BinDir")

function Normalize-Version {
    param([Parameter(Mandatory = $true)][string]$Value)

    $Normalized = if ($Value.StartsWith("v", [StringComparison]::Ordinal)) {
        $Value.Substring(1)
    } else {
        $Value
    }
    if ($Normalized -cnotmatch $StableVersionPattern) {
        throw "Invalid stable semantic version: $Value"
    }
    return $Normalized
}

function Get-ReleaseTarget {
    if (-not [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform(
            [Runtime.InteropServices.OSPlatform]::Windows
        )) {
        throw "The PowerShell installer only supports Windows."
    }
    $Architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    if ($Architecture -ne [Runtime.InteropServices.Architecture]::X64) {
        throw "Unsupported Windows architecture: $Architecture"
    }
    return "windows-x86_64"
}

function Invoke-ReleaseDownload {
    param(
        [Parameter(Mandatory = $true)][uri]$Uri,
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][long]$MaxBytes
    )

    $Current = $Uri
    $RedirectCount = 0
    $Part = "$Output.part"
    while ($true) {
        Assert-ReleaseUri $Current
        Remove-Item -LiteralPath $Part -Force -ErrorAction SilentlyContinue
        $Response = $null
        $RequestError = $null
        try {
            $Response = Invoke-WebRequest -Uri $Current -OutFile $Part `
                -UseBasicParsing -MaximumRedirection 0 -PassThru
        } catch {
            $RequestError = $_.Exception
            if ($_.Exception.Response) {
                $Response = $_.Exception.Response
            } else {
                throw
            }
        }
        $StatusCode = [int]$Response.StatusCode
        if ($StatusCode -ge 200 -and $StatusCode -lt 300) {
            if ($RequestError) {
                throw $RequestError
            }
            $Length = (Get-Item -LiteralPath $Part).Length
            if ($Length -gt $MaxBytes) {
                throw "Downloaded file exceeds the installer size limit."
            }
            [IO.File]::Move($Part, $Output)
            return
        }
        if ($StatusCode -notin @(301, 302, 303, 307, 308)) {
            throw "Release download returned HTTP $StatusCode."
        }
        if ($IsLoopbackFixture) {
            throw "Loopback release fixtures must not redirect."
        }
        if ($RedirectCount -ge $MaxRedirects) {
            throw "Release download exceeded the redirect limit."
        }
        $Location = $null
        if ($Response.Headers.PSObject.Properties.Name -contains "Location") {
            $Location = [string]$Response.Headers.Location
        } else {
            $Location = [string]$Response.Headers["Location"]
        }
        if ([string]::IsNullOrWhiteSpace($Location)) {
            throw "Release redirect is missing Location."
        }
        $Current = [uri]::new($Current, $Location)
        Assert-ReleaseUri $Current
        $RedirectCount++
    }
}

function Assert-ReleaseUri {
    param([Parameter(Mandatory = $true)][uri]$Uri)

    if (-not $Uri.IsAbsoluteUri -or
        -not [string]::IsNullOrEmpty($Uri.Fragment) -or
        -not [string]::IsNullOrEmpty($Uri.UserInfo)) {
        throw "Release URL is not accepted."
    }
    if ($Uri.OriginalString -match "[\x00-\x20\x7f]") {
        throw "Release URL contains whitespace or control characters."
    }
    if ($IsLoopbackFixture) {
        if ($Uri.Scheme -cne "http" -or
            $Uri.Host -cne "127.0.0.1" -or
            $Uri.IsDefaultPort) {
            throw "Release fixture escaped exact loopback."
        }
        return
    }
    if ($Uri.Scheme -cne "https" -or
        [string]::IsNullOrWhiteSpace($Uri.DnsSafeHost)) {
        throw "Production release downloads must remain on HTTPS."
    }
}

function Read-ChecksumInventory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Entries = [Collections.Generic.List[object]]::new()
    $Seen = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal
    )
    foreach ($Line in [IO.File]::ReadAllLines($Path)) {
        if ($Line -cnotmatch "^([0-9a-f]{64})  ([A-Za-z0-9._-]+)$") {
            throw "SHA256SUMS contains an invalid entry."
        }
        $Digest = $Matches[1]
        $Name = $Matches[2]
        if ($Name -cnotmatch $AssetPattern) {
            throw "SHA256SUMS contains an invalid asset name."
        }
        if (-not $Seen.Add($Name)) {
            throw "SHA256SUMS contains a duplicate entry for $Name."
        }
        $Entries.Add([pscustomobject]@{ Digest = $Digest; Name = $Name })
    }
    if ($Entries.Count -eq 0) {
        throw "SHA256SUMS is empty."
    }
    return $Entries
}

function Assert-ChecksumContract {
    param(
        [Parameter(Mandatory = $true)][object[]]$Entries,
        [Parameter(Mandatory = $true)][string]$ReleaseVersion
    )

    $Expected = @(
        "netft-cli-$ReleaseVersion-linux-x86_64.tar.gz",
        "netft-cli-$ReleaseVersion-linux-arm64.tar.gz",
        "netft-cli-$ReleaseVersion-macos-x86_64.tar.gz",
        "netft-cli-$ReleaseVersion-macos-arm64.tar.gz",
        "netft-cli-$ReleaseVersion-windows-x86_64.zip"
    )
    $Actual = @($Entries | ForEach-Object { $_.Name } | Sort-Object)
    $Expected = @($Expected | Sort-Object)
    if ($Actual.Count -ne $Expected.Count -or
        [string]::Join("`n", $Actual) -cne
        [string]::Join("`n", $Expected)) {
        throw "SHA256SUMS does not match the five-platform release contract."
    }
}

function Assert-ZipPayload {
    param(
        [Parameter(Mandatory = $true)][string]$Archive,
        [Parameter(Mandatory = $true)][string]$ReleaseVersion
    )

    $ArchiveLength = (Get-Item -LiteralPath $Archive).Length
    if ($ArchiveLength -gt $MaxArchiveBytes) {
        throw "Release archive exceeds its size limit."
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $Root = "netft-cli-$ReleaseVersion"
    $Expected = @(
        "$Root/LICENSE",
        "$Root/LICENSES/curl.txt",
        "$Root/LICENSES/netft-cpp.txt",
        "$Root/netft.exe"
    )
    $Release = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $ExpandedLength = [long]0
        $Names = @($Release.Entries | ForEach-Object { $_.FullName })
        if ($Names.Count -ne $Expected.Count) {
            throw "Release archive payload does not match the release contract."
        }
        for ($Index = 0; $Index -lt $Expected.Count; $Index++) {
            if ($Names[$Index] -cne $Expected[$Index]) {
                throw "Release archive payload does not match the release contract."
            }
            $Entry = $Release.Entries[$Index]
            if ($Entry.Length -lt 0 -or $Entry.Length -gt $MaxMemberBytes) {
                throw "Release archive member exceeds its size limit."
            }
            $ExpandedLength += $Entry.Length
            if ($ExpandedLength -gt $MaxExpandedBytes) {
                throw "Release archive exceeds its expanded size limit."
            }
            $UnixType = ($Entry.ExternalAttributes -shr 16) -band 0xF000
            if ($UnixType -ne 0x8000) {
                throw "Release archive members must be regular files."
            }
        }
    } finally {
        $Release.Dispose()
    }
}

function Assert-ExpandedPayload {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$ReleaseVersion
    )

    $Prefix = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $ExpectedFiles = @(
        "netft-cli-$ReleaseVersion/LICENSE",
        "netft-cli-$ReleaseVersion/LICENSES/curl.txt",
        "netft-cli-$ReleaseVersion/LICENSES/netft-cpp.txt",
        "netft-cli-$ReleaseVersion/netft.exe"
    )
    $ExpectedDirectories = @(
        "netft-cli-$ReleaseVersion",
        "netft-cli-$ReleaseVersion/LICENSES"
    )
    $Files = [Collections.Generic.List[string]]::new()
    $Directories = [Collections.Generic.List[string]]::new()
    foreach ($Item in Get-ChildItem -LiteralPath $Root -Recurse -Force) {
        if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Release archive produced a reparse point."
        }
        $Relative = $Item.FullName.Substring($Prefix.Length).TrimStart(
            [char[]]"\/"
        )
        $Relative = $Relative.Replace("\", "/")
        if ($Item.PSIsContainer) {
            $Directories.Add($Relative)
        } else {
            $Files.Add($Relative)
        }
    }
    $ActualFiles = @($Files | Sort-Object)
    $ActualDirectories = @($Directories | Sort-Object)
    $ExpectedFiles = @($ExpectedFiles | Sort-Object)
    $ExpectedDirectories = @($ExpectedDirectories | Sort-Object)
    if ([string]::Join("`n", $ActualFiles) -cne
        [string]::Join("`n", $ExpectedFiles) -or
        [string]::Join("`n", $ActualDirectories) -cne
        [string]::Join("`n", $ExpectedDirectories)) {
        throw "Expanded release payload does not match the release contract."
    }
}

function Get-TestableUserPath {
    if ($IsLoopbackFixture -and $env:NETFT_CLI_TEST_USER_PATH_FILE) {
        if (Test-Path -LiteralPath $env:NETFT_CLI_TEST_USER_PATH_FILE) {
            return [IO.File]::ReadAllText($env:NETFT_CLI_TEST_USER_PATH_FILE)
        }
        return ""
    }
    return [Environment]::GetEnvironmentVariable(
        "Path",
        [EnvironmentVariableTarget]::User
    )
}

function Set-TestableUserPath {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)

    if ($IsLoopbackFixture -and $env:NETFT_CLI_TEST_USER_PATH_FILE) {
        [IO.File]::WriteAllText($env:NETFT_CLI_TEST_USER_PATH_FILE, $Value)
        return
    }
    [Environment]::SetEnvironmentVariable(
        "Path",
        $Value,
        [EnvironmentVariableTarget]::User
    )
}

function Get-NormalizedPathKey {
    param([Parameter(Mandatory = $true)][string]$Value)

    $Expanded = [Environment]::ExpandEnvironmentVariables($Value.Trim())
    try {
        $Expanded = [IO.Path]::GetFullPath($Expanded)
    } catch {
        # Preserve a syntactically unusual existing user PATH entry.
    }
    return $Expanded.TrimEnd([char[]]"\/")
}

function Add-UserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Directory)

    $Existing = Get-TestableUserPath
    $Entries = if ([string]::IsNullOrWhiteSpace($Existing)) {
        @()
    } else {
        @($Existing.Split(";"))
    }
    $Seen = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase
    )
    $Result = [Collections.Generic.List[string]]::new()
    foreach ($Entry in $Entries) {
        if ([string]::IsNullOrWhiteSpace($Entry)) {
            continue
        }
        $Key = Get-NormalizedPathKey $Entry
        if ($Seen.Add($Key)) {
            $Result.Add($Entry.Trim())
        }
    }
    $Directory = [IO.Path]::GetFullPath($Directory).TrimEnd([char[]]"\/")
    if ($Seen.Add((Get-NormalizedPathKey $Directory))) {
        $Result.Add($Directory)
    }
    Set-TestableUserPath ([string]::Join(";", $Result))
}

try {
    $Target = Get-ReleaseTarget
    if (-not $BinDirWasProvided) {
        if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
            throw "LOCALAPPDATA is not set; use -BinDir."
        }
        $BinDir = Join-Path $env:LOCALAPPDATA "netft\bin"
    } elseif ([string]::IsNullOrWhiteSpace($BinDir)) {
        throw "Installation directory must not be empty."
    }
    $BinDir = [IO.Path]::GetFullPath($BinDir)

    $ReleaseBaseOverride = Test-Path Env:NETFT_CLI_RELEASE_BASE_URL
    $BaseUrlText = if ($ReleaseBaseOverride) {
        $Override = $env:NETFT_CLI_RELEASE_BASE_URL
        if ($Override -cnotmatch
            "^http://127\.0\.0\.1:[0-9]+(/[^\s#]*)?$") {
            throw "Release-base override must be an exact loopback test URL."
        }
        $Override.TrimEnd("/")
    } else {
        $DefaultReleaseBaseUrl
    }
    $BaseUri = [uri]$BaseUrlText
    $IsLoopbackFixture = $ReleaseBaseOverride
    Assert-ReleaseUri $BaseUri

    if (Test-Path -LiteralPath $BinDir) {
        $BinItem = Get-Item -LiteralPath $BinDir -Force
        if (-not $BinItem.PSIsContainer -or
            ($BinItem.Attributes -band
            [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Installation directory must be a real directory."
        }
    } else {
        New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    }
    $LockPath = Join-Path $BinDir ".netft-install.lock"
    if (Test-Path -LiteralPath $LockPath) {
        $LockItem = Get-Item -LiteralPath $LockPath -Force
        if (($LockItem.Attributes -band
            [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Installer lock path must not be a reparse point."
        }
    }
    try {
        $InstallLock = [IO.FileStream]::new(
            $LockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None
        )
    } catch {
        throw "Another installer is active for this destination."
    }
    $Temporary = Join-Path $BinDir (
        ".netft-install." + [Guid]::NewGuid().ToString("N")
    )
    New-Item -ItemType Directory -Path $Temporary | Out-Null
    $Checksums = Join-Path $Temporary "SHA256SUMS"

    if (-not $VersionWasProvided) {
        $AssetBase = "$BaseUrlText/latest/download"
        Invoke-ReleaseDownload ([uri]"$AssetBase/SHA256SUMS") $Checksums `
            $MaxChecksumBytes
        $Entries = @(Read-ChecksumInventory $Checksums)
        $Candidates = @(
            $Entries | Where-Object {
                $_.Name -clike "netft-cli-*-windows-x86_64.zip"
            }
        )
        if ($Candidates.Count -ne 1) {
            throw "Latest release does not contain exactly one Windows asset."
        }
        $Selected = $Candidates[0]
        $VersionMatch = [regex]::Match(
            $Selected.Name,
            "^netft-cli-((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))-windows-x86_64\.zip$",
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
        )
        if (-not $VersionMatch.Success) {
            throw "Latest release has an invalid stable version."
        }
        $SelectedVersion = $VersionMatch.Groups[1].Value
        $SelectedName = $Selected.Name
        Assert-ChecksumContract $Entries $SelectedVersion
    } else {
        $SelectedVersion = Normalize-Version $Version
        $SelectedName = "netft-cli-$SelectedVersion-$Target.zip"
        $AssetBase = "$BaseUrlText/download/v$SelectedVersion"
        Invoke-ReleaseDownload ([uri]"$AssetBase/SHA256SUMS") $Checksums `
            $MaxChecksumBytes
        $Entries = @(Read-ChecksumInventory $Checksums)
        Assert-ChecksumContract $Entries $SelectedVersion
        $Candidates = @($Entries | Where-Object { $_.Name -ceq $SelectedName })
        if ($Candidates.Count -ne 1) {
            throw "SHA256SUMS does not contain exactly one entry for $SelectedName."
        }
        $Selected = $Candidates[0]
    }

    $Archive = Join-Path $Temporary $SelectedName
    Invoke-ReleaseDownload ([uri]"$AssetBase/$SelectedName") $Archive `
        $MaxArchiveBytes
    $ActualDigest = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash
    if ($ActualDigest -cne $Selected.Digest.ToUpperInvariant()) {
        throw "Checksum mismatch for $SelectedName."
    }

    Assert-ZipPayload $Archive $SelectedVersion
    $Extracted = Join-Path $Temporary "extracted"
    Expand-Archive -LiteralPath $Archive -DestinationPath $Extracted
    Assert-ExpandedPayload $Extracted $SelectedVersion
    $Candidate = Join-Path $Extracted "netft-cli-$SelectedVersion\netft.exe"
    if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
        throw "Release archive did not produce netft.exe."
    }

    $VersionError = Join-Path $Temporary "version-error.txt"
    $VersionOutput = @(& $Candidate --version 2>$VersionError)
    if ($LASTEXITCODE -ne 0 -or $VersionOutput.Count -ne 1 -or
        $VersionOutput[0] -cne "netft $SelectedVersion") {
        throw "Downloaded netft executable failed its version check."
    }

    $Destination = Join-Path $BinDir $Program
    if (Test-Path -LiteralPath $Destination) {
        $DestinationItem = Get-Item -LiteralPath $Destination -Force
        if (($DestinationItem.Attributes -band
            [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to replace a reparse-point destination."
        }
    }
    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $Backup = Join-Path $Temporary "netft.exe.backup"
        [IO.File]::Replace($Candidate, $Destination, $Backup, $true)
        $HadPreviousBinary = $true
    } else {
        if (Test-Path -LiteralPath $Destination) {
            throw "Refusing to replace a non-file destination."
        }
        [IO.File]::Move($Candidate, $Destination)
    }
    $ReplacementCompleted = $true

    if (-not $NoModifyPath) {
        $OriginalUserPath = [string](Get-TestableUserPath)
        $PathUpdateAttempted = $true
        Add-UserPathEntry $BinDir
    }
    if ($Backup -and (Test-Path -LiteralPath $Backup)) {
        Remove-Item -LiteralPath $Backup -Force
    }
    $ReplacementCompleted = $false
    $PathUpdateAttempted = $false
    try {
        [Console]::Out.WriteLine(
            "Installed netft $SelectedVersion to $Destination"
        )
    } catch {
        # Installation is already committed; status output is best effort.
    }
} catch {
    $InstallError = $_.Exception
    if ($ReplacementCompleted -and $Destination) {
        try {
            if ($HadPreviousBinary -and $Backup -and
                (Test-Path -LiteralPath $Backup -PathType Leaf)) {
                [IO.File]::Replace($Backup, $Destination, $null, $true)
            } elseif (-not $HadPreviousBinary -and
                (Test-Path -LiteralPath $Destination -PathType Leaf)) {
                Remove-Item -LiteralPath $Destination -Force
            }
        } catch {
            [Console]::Error.WriteLine(
                "netft installer: failed to roll back the previous executable: " +
                $_.Exception.Message
            )
        }
    }
    if ($PathUpdateAttempted) {
        try {
            Set-TestableUserPath $OriginalUserPath
        } catch {
            [Console]::Error.WriteLine(
                "netft installer: failed to roll back the user PATH: " +
                $_.Exception.Message
            )
        }
    }
    [Console]::Error.WriteLine("netft installer: " + $InstallError.Message)
    exit 1
} finally {
    try {
        if ($Temporary -and (Test-Path -LiteralPath $Temporary)) {
            Remove-Item -LiteralPath $Temporary -Recurse -Force
        }
    } finally {
        if ($InstallLock) {
            $InstallLock.Dispose()
        }
    }
}
