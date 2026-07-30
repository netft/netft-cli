#!/bin/sh

set -eu

PROGRAM=netft
DEFAULT_RELEASE_BASE_URL=https://github.com/netft/netft-cli/releases
MAX_CHECKSUM_BYTES=1048576
MAX_ARCHIVE_BYTES=67108864
MAX_MEMBER_BYTES=33554432
MAX_TOTAL_BYTES=50331648
MAX_REDIRECTS=5

die() {
  printf 'netft installer: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Usage: install.sh [--version VERSION] [--bin-dir PATH]

Install the netft CLI. VERSION must be a stable semantic version such as 0.1.0.
EOF
}

is_stable_version() {
  printf '%s\n' "$1" |
    grep -Eq '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
}

normalize_version() {
  value=$1
  case "$value" in
    v*) value=${value#v} ;;
  esac
  is_stable_version "$value" || die "invalid stable semantic version: $1"
  printf '%s\n' "$value"
}

detect_target() {
  system=$(uname -s) || die "unable to detect operating system"
  machine=$(uname -m) || die "unable to detect architecture"
  case "$system:$machine" in
    Linux:x86_64 | Linux:amd64) printf '%s\n' linux-x86_64 ;;
    Linux:aarch64 | Linux:arm64) printf '%s\n' linux-arm64 ;;
    Darwin:x86_64 | Darwin:amd64) printf '%s\n' macos-x86_64 ;;
    Darwin:arm64 | Darwin:aarch64) printf '%s\n' macos-arm64 ;;
    *) die "unsupported platform: $system/$machine" ;;
  esac
}

is_release_asset_name() {
  printf '%s\n' "$1" |
    grep -Eq '^netft-cli-(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-((linux-(x86_64|arm64)|macos-(x86_64|arm64))\.tar\.gz|windows-x86_64\.zip)$'
}

download() {
  url=$1
  output=$2
  max_bytes=$3
  validate_download_url "$url"
  if command -v curl >/dev/null 2>&1; then
    part_output=$output.part
    rm -f "$part_output"
    file_block_limit=$(((max_bytes + 511) / 512))
    if [ "$release_transport" = production ]; then
      if ! (
        ulimit -c 0 2>/dev/null || :
        ulimit -f "$file_block_limit" 2>/dev/null || exit 1
        curl --fail --location --silent --show-error \
          --proto '=https' --proto-redir '=https' \
          --output "$part_output" "$url"
      ); then
        die "curl failed while downloading a release file"
      fi
    else
      if ! (
        ulimit -c 0 2>/dev/null || :
        ulimit -f "$file_block_limit" 2>/dev/null || exit 1
        curl --fail --location --max-redirs 0 --silent --show-error \
          --proto '=http' --proto-redir '=http' \
          --output "$part_output" "$url"
      ); then
        die "curl failed while downloading a release file"
      fi
    fi
    assert_file_within_limit "$part_output" "$max_bytes"
    mv -f "$part_output" "$output" ||
      die "unable to finalize downloaded file"
    return
  fi
  if command -v wget >/dev/null 2>&1; then
    download_with_wget "$url" "$output" "$max_bytes"
    assert_file_within_limit "$output" "$max_bytes"
    return
  fi
  die "curl or wget is required"
}

validate_download_url() {
  candidate_url=$1
  if printf '%s' "$candidate_url" |
    LC_ALL=C grep -q '[[:cntrl:][:space:]]'; then
    die "release URL contains whitespace or control characters"
  fi
  case "$candidate_url" in
    *'#'*) die "release URLs must not contain fragments" ;;
  esac
  if [ "$release_transport" = production ]; then
    case "$candidate_url" in
      https://*) ;;
      *) die "release redirect must be an absolute HTTPS URL" ;;
    esac
    authority=${candidate_url#https://}
    authority=${authority%%/*}
    authority=${authority%%\?*}
    case "$authority" in
      '' | *@*) die "release URL authority is not accepted" ;;
    esac
    host_name=${authority%%:*}
    if [ "$host_name" != "$authority" ]; then
      port_number=${authority##*:}
      case "$port_number" in
        '' | *[!0-9]*) die "release URL port is not accepted" ;;
      esac
      [ "${#port_number}" -le 5 ] &&
        [ "$port_number" -ge 1 ] &&
        [ "$port_number" -le 65535 ] ||
        die "release URL port is not accepted"
    fi
    printf '%s\n' "$host_name" |
      grep -Eq '^([A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)(\.([A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?))*$' ||
      die "release URL host is not accepted"
    return
  fi
  printf '%s\n' "$candidate_url" |
    grep -Eq '^http://127\.0\.0\.1:[0-9]+(/[^[:space:]]*)?$' ||
    die "release fixture redirect escaped exact loopback"
  loopback_authority=${candidate_url#http://}
  loopback_authority=${loopback_authority%%/*}
  [ "$loopback_authority" = "$fixture_authority" ] ||
    die "release fixture redirect changed authority"
  loopback_port=${loopback_authority##*:}
  [ "${#loopback_port}" -le 5 ] &&
    [ "$loopback_port" -ge 1 ] &&
    [ "$loopback_port" -le 65535 ] ||
    die "release fixture port is not accepted"
}

measure_file_bytes() {
  wc -c <"$1" |
    awk '
      {
        if (seen || NF != 1 || $1 !~ /^[0-9]+$/) {
          exit 1
        }
        value = $1
        seen = 1
      }
      END {
        if (!seen) {
          exit 1
        }
        print value
      }
    '
}

assert_file_within_limit() {
  bounded_path=$1
  bounded_limit=$2
  [ -f "$bounded_path" ] || die "download did not produce a regular file"
  bounded_size=$(measure_file_bytes "$bounded_path") ||
    die "unable to measure downloaded file"
  [ "$bounded_size" -le "$bounded_limit" ] ||
    die "download exceeds the installer size limit"
}

download_with_wget() {
  current_url=$1
  final_output=$2
  max_bytes=$3
  redirect_count=0
  file_block_limit=$(((max_bytes + 511) / 512))
  part_output=$final_output.part
  header_output=$final_output.headers
  while :; do
    rm -f "$part_output" "$header_output"
    if (
      ulimit -c 0 2>/dev/null || :
      ulimit -f "$file_block_limit" 2>/dev/null || exit 1
      wget --server-response --max-redirect=0 \
        --output-document="$part_output" "$current_url" 2>"$header_output"
    ); then
      wget_result=0
    else
      wget_result=$?
    fi
    response_status=$(
      awk '
        tolower($0) ~ /^[[:space:]]*http\/[0-9.]+ [0-9][0-9][0-9]/ {
          status = $2
          sub(/\r$/, "", status)
        }
        END { print status }
      ' "$header_output"
    )
    case "$response_status" in
      2??)
        [ "$wget_result" -eq 0 ] ||
          die "wget failed while downloading a release file"
        assert_file_within_limit "$part_output" "$max_bytes"
        mv -f "$part_output" "$final_output" ||
          die "unable to finalize downloaded file"
        rm -f "$header_output"
        return
        ;;
      301 | 302 | 303 | 307 | 308)
        [ "$redirect_count" -lt "$MAX_REDIRECTS" ] ||
          die "release download exceeded the redirect limit"
        if ! redirect_url=$(
          awk '
            tolower($0) ~ /^[[:space:]]+http\/[0-9.]+ [0-9][0-9][0-9]/ {
              location = ""
              count = 0
              next
            }
            tolower($0) ~ /^[[:space:]]+location:[[:space:]]*/ {
              value = $0
              sub(/^[[:space:]]+[^:]*:[[:space:]]*/, "", value)
              sub(/[[:space:]]+\[following\]\r?$/, "", value)
              sub(/\r$/, "", value)
              location = value
              count++
            }
            END {
              if (count != 1) {
                exit 2
              }
              print location
            }
          ' "$header_output"
        ); then
          die "release redirect must contain exactly one Location"
        fi
        case "$release_transport:$redirect_url" in
          production:https://* | loopback:http://*) ;;
          *) die "relative or downgraded release redirects are not accepted" ;;
        esac
        validate_download_url "$redirect_url"
        current_url=$redirect_url
        redirect_count=$((redirect_count + 1))
        ;;
      *)
        die "wget failed while downloading a release file"
        ;;
    esac
  done
}

validate_checksum_inventory() {
  checksum_path=$1
  seen_path=$2
  : >"$seen_path"
  entry_count=0
  while IFS= read -r line || [ -n "$line" ]; do
    digest=${line%%  *}
    filename=${line#*  }
    [ "$line" = "$digest  $filename" ] ||
      die "SHA256SUMS contains an invalid entry"
    [ "${#digest}" -eq 64 ] ||
      die "SHA256SUMS contains an invalid digest"
    case "$digest" in
      *[!0-9a-f]*) die "SHA256SUMS contains an invalid digest" ;;
    esac
    is_release_asset_name "$filename" ||
      die "SHA256SUMS contains an invalid asset name"
    if grep -Fqx -- "$filename" "$seen_path"; then
      die "SHA256SUMS contains a duplicate entry for $filename"
    fi
    printf '%s\n' "$filename" >>"$seen_path"
    entry_count=$((entry_count + 1))
  done <"$checksum_path"
  [ "$entry_count" -gt 0 ] || die "SHA256SUMS is empty"
}

assert_checksum_contract() {
  seen_path=$1
  contract_version=$2
  [ "$entry_count" -eq 5 ] ||
    die "SHA256SUMS does not match the five-platform release contract"
  for contract_name in \
    "netft-cli-$contract_version-linux-x86_64.tar.gz" \
    "netft-cli-$contract_version-linux-arm64.tar.gz" \
    "netft-cli-$contract_version-macos-x86_64.tar.gz" \
    "netft-cli-$contract_version-macos-arm64.tar.gz" \
    "netft-cli-$contract_version-windows-x86_64.zip"; do
    grep -Fqx -- "$contract_name" "$seen_path" ||
      die "SHA256SUMS does not match the five-platform release contract"
  done
}

select_explicit_asset() {
  checksum_path=$1
  expected_name=$2
  selected_digest=
  selected_count=0
  while IFS= read -r line || [ -n "$line" ]; do
    digest=${line%%  *}
    filename=${line#*  }
    if [ "$filename" = "$expected_name" ]; then
      selected_digest=$digest
      selected_count=$((selected_count + 1))
    fi
  done <"$checksum_path"
  [ "$selected_count" -eq 1 ] ||
    die "SHA256SUMS does not contain exactly one entry for $expected_name"
}

select_latest_asset() {
  checksum_path=$1
  suffix="-$target.tar.gz"
  selected_digest=
  selected_name=
  selected_version=
  selected_count=0
  while IFS= read -r line || [ -n "$line" ]; do
    digest=${line%%  *}
    filename=${line#*  }
    case "$filename" in
      netft-cli-*"$suffix")
        candidate=${filename#netft-cli-}
        candidate=${candidate%"$suffix"}
        is_stable_version "$candidate" ||
          die "latest release has an invalid stable version"
        selected_digest=$digest
        selected_name=$filename
        selected_version=$candidate
        selected_count=$((selected_count + 1))
        ;;
    esac
  done <"$checksum_path"
  [ "$selected_count" -eq 1 ] ||
    die "latest release does not contain exactly one asset for $target"
}

sha256_file() {
  path=$1
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print $1}'
    return
  fi
  die "sha256sum or shasum is required"
}

validate_and_extract_archive() {
  archive_path=$1
  extract_path=$2
  root="netft-cli-$selected_version"
  expected_names=$(printf '%s\n' \
    "$root/LICENSE" \
    "$root/LICENSES/curl.txt" \
    "$root/LICENSES/netft-cpp.txt" \
    "$root/netft")
  if ! actual_names=$(tar -tzf "$archive_path"); then
    die "invalid release archive"
  fi
  [ "$actual_names" = "$expected_names" ] ||
    die "release archive payload does not match the release contract"

  if ! member_types=$(
    tar -tvzf "$archive_path" |
      while IFS= read -r entry; do
        printf '%s\n' "${entry%"${entry#?}"}"
      done
  ); then
    die "unable to inspect release archive"
  fi
  expected_types=$(printf '%s\n' - - - -)
  [ "$member_types" = "$expected_types" ] ||
    die "release archive members must be regular files"

  assert_file_within_limit "$archive_path" "$MAX_ARCHIVE_BYTES"
  mkdir "$extract_path"
  extracted_total=0
  bounded_extract_member "$archive_path" "$root/LICENSE" \
    "$extract_path/LICENSE"
  bounded_extract_member "$archive_path" "$root/LICENSES/curl.txt" \
    "$extract_path/curl.txt"
  bounded_extract_member "$archive_path" "$root/LICENSES/netft-cpp.txt" \
    "$extract_path/netft-cpp.txt"
  bounded_extract_member "$archive_path" "$root/netft" \
    "$extract_path/netft"
  candidate="$extract_path/netft"
  [ -f "$candidate" ] && [ ! -L "$candidate" ] ||
    die "release archive did not produce a regular netft executable"
  chmod 0755 "$candidate"
}

bounded_extract_member() {
  bounded_archive=$1
  bounded_member=$2
  bounded_output=$3
  member_block_limit=$(((MAX_MEMBER_BYTES + 511) / 512))
  rm -f "$bounded_output"
  if ! (
    ulimit -c 0 2>/dev/null || :
    ulimit -f "$member_block_limit" 2>/dev/null || exit 1
    tar -xOzf "$bounded_archive" "$bounded_member" >"$bounded_output"
  ); then
    die "release archive member exceeds its limit or cannot be read"
  fi
  member_size=$(measure_file_bytes "$bounded_output") ||
    die "unable to measure extracted archive member"
  [ "$member_size" -le "$MAX_MEMBER_BYTES" ] ||
    die "release archive member exceeds its size limit"
  extracted_total=$((extracted_total + member_size))
  [ "$extracted_total" -le "$MAX_TOTAL_BYTES" ] ||
    die "release archive exceeds its expanded size limit"
}

version_argument=
version_provided=0
bin_dir=
bin_dir_provided=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version)
      [ "$#" -ge 2 ] || die "--version requires a value"
      version_argument=$2
      version_provided=1
      shift 2
      ;;
    --bin-dir)
      [ "$#" -ge 2 ] || die "--bin-dir requires a value"
      bin_dir=$2
      bin_dir_provided=1
      shift 2
      ;;
    --help | -h)
      usage
      exit 0
      ;;
    *) die "unknown argument: $1" ;;
  esac
done

if [ "$bin_dir_provided" -eq 0 ]; then
  [ -n "${HOME:-}" ] || die "HOME is not set; use --bin-dir"
  bin_dir=$HOME/.local/bin
  default_bin_dir=1
else
  default_bin_dir=0
fi
[ -n "$bin_dir" ] || die "installation directory must not be empty"

target=$(detect_target)
if [ "${NETFT_CLI_RELEASE_BASE_URL+x}" = x ]; then
  base_url=$NETFT_CLI_RELEASE_BASE_URL
  release_transport=loopback
else
  base_url=$DEFAULT_RELEASE_BASE_URL
  release_transport=production
fi
base_url=${base_url%/}
case "$release_transport:$base_url" in
  production:https://github.com/netft/netft-cli/releases)
    ;;
  loopback:http://127.0.0.1:*)
    printf '%s\n' "$base_url" |
      grep -Eq '^http://127\.0\.0\.1:[0-9]+(/[^[:space:]]*)?$' ||
      die "invalid loopback release fixture URL"
    ;;
  *)
    die "release base URL must use HTTPS"
    ;;
esac
if [ "$release_transport" = loopback ]; then
  fixture_authority=${base_url#http://}
  fixture_authority=${fixture_authority%%/*}
else
  fixture_authority=
fi
validate_download_url "$base_url"

if [ -L "$bin_dir" ]; then
  die "installation directory must not be a symbolic link"
fi
if [ -e "$bin_dir" ]; then
  [ -d "$bin_dir" ] || die "installation destination must be a directory"
else
  mkdir -p "$bin_dir"
fi
temporary=
lock_path=$bin_dir/.netft-install.lock
lock_acquired=0
lock_token=
cleanup() {
  if [ -n "$temporary" ]; then
    rm -rf "$temporary"
  fi
  if [ "$lock_acquired" -eq 1 ] &&
    [ -L "$lock_path" ]; then
    observed_lock_token=$(readlink "$lock_path" 2>/dev/null || :)
    if [ "$observed_lock_token" = "$lock_token" ]; then
      rm -f "$lock_path" 2>/dev/null || :
    fi
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! temporary=$(mktemp -d "$bin_dir/.netft-install.XXXXXX"); then
  die "unable to create installation staging directory"
fi
lock_token=netft-owner-${temporary##*/}
# GNU ln uses -T and BSD/macOS ln uses -h to avoid following a
# destination symlink. The fallback precheck also rejects every existing type.
if ! ln -sT "$lock_token" "$lock_path" 2>/dev/null; then
  if [ -e "$lock_path" ] || [ -L "$lock_path" ] ||
    ! ln -sh "$lock_token" "$lock_path" 2>/dev/null; then
    die "another installer is active for this destination"
  fi
fi
if [ ! -L "$lock_path" ] ||
  [ "$(readlink "$lock_path" 2>/dev/null || :)" != "$lock_token" ]; then
  die "unable to acquire installer lock"
fi
lock_acquired=1

checksums=$temporary/SHA256SUMS
seen=$temporary/checksum-names
if [ "$version_provided" -eq 1 ]; then
  selected_version=$(normalize_version "$version_argument")
  asset_base="$base_url/download/v$selected_version"
  selected_name="netft-cli-$selected_version-$target.tar.gz"
  download "$asset_base/SHA256SUMS" "$checksums" "$MAX_CHECKSUM_BYTES" ||
    die "unable to download SHA256SUMS"
  validate_checksum_inventory "$checksums" "$seen"
  assert_checksum_contract "$seen" "$selected_version"
  select_explicit_asset "$checksums" "$selected_name"
else
  asset_base="$base_url/latest/download"
  download "$asset_base/SHA256SUMS" "$checksums" "$MAX_CHECKSUM_BYTES" ||
    die "unable to download latest SHA256SUMS"
  validate_checksum_inventory "$checksums" "$seen"
  select_latest_asset "$checksums"
  assert_checksum_contract "$seen" "$selected_version"
fi

archive=$temporary/$selected_name
download "$asset_base/$selected_name" "$archive" "$MAX_ARCHIVE_BYTES" ||
  die "unable to download $selected_name"
actual_digest=$(sha256_file "$archive")
[ "$actual_digest" = "$selected_digest" ] ||
  die "checksum mismatch for $selected_name"

validate_and_extract_archive "$archive" "$temporary/extracted"
candidate="$temporary/extracted/netft"
version_output=$temporary/version-output
if ! "$candidate" --version >"$version_output"; then
  die "downloaded netft executable failed its version check"
fi
expected_output=$temporary/expected-version-output
printf 'netft %s\n' "$selected_version" >"$expected_output"
cmp -s "$version_output" "$expected_output" ||
  die "downloaded netft executable reported the wrong version"

destination=$bin_dir/$PROGRAM
[ ! -L "$destination" ] ||
  die "refusing to replace a symbolic-link destination"
if [ -e "$destination" ]; then
  [ -f "$destination" ] ||
    die "refusing to replace a non-file destination"
fi
mv -f "$candidate" "$destination" ||
  die "unable to replace $destination"
printf 'Installed netft %s to %s\n' "$selected_version" "$destination" || :

if [ "$default_bin_dir" -eq 1 ]; then
  case ":${PATH:-}:" in
    *":$bin_dir:"*) ;;
    *)
      printf 'Add %s to PATH to run netft from your shell.\n' "$bin_dir" || :
      ;;
  esac
fi
exit 0
