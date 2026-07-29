#!/bin/sh

set -eu

PROGRAM=netft
DEFAULT_RELEASE_BASE_URL=https://github.com/netft/netft-cli/releases

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
  if command -v curl >/dev/null 2>&1; then
    if [ "$release_transport" = https ]; then
      curl --fail --location --silent --show-error \
        --proto '=https' --proto-redir '=https' --output "$output" "$url"
    else
      curl --fail --silent --show-error \
        --proto '=http' --output "$output" "$url"
    fi
    return
  fi
  if command -v wget >/dev/null 2>&1; then
    if [ "$release_transport" = https ]; then
      wget --https-only --quiet --output-document="$output" "$url"
    else
      wget --quiet --output-document="$output" "$url"
    fi
    return
  fi
  die "curl or wget is required"
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

  mkdir "$extract_path"
  if ! tar -xzf "$archive_path" -C "$extract_path" "$root/netft"; then
    die "unable to extract release archive"
  fi
  candidate="$extract_path/$root/netft"
  [ -f "$candidate" ] && [ ! -L "$candidate" ] ||
    die "release archive did not produce a regular netft executable"
  chmod 0755 "$candidate"
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
base_url=${NETFT_CLI_RELEASE_BASE_URL:-$DEFAULT_RELEASE_BASE_URL}
base_url=${base_url%/}
case "$base_url" in
  https://*)
    release_transport=https
    ;;
  http://127.0.0.1:*)
    printf '%s\n' "$base_url" |
      grep -Eq '^http://127\.0\.0\.1:[0-9]+(/[^[:space:]]*)?$' ||
      die "invalid loopback release fixture URL"
    release_transport=http
    ;;
  *)
    die "release base URL must use HTTPS"
    ;;
esac

mkdir -p "$bin_dir"
temporary=$(mktemp -d "$bin_dir/.netft-install.XXXXXX") ||
  die "unable to create installation staging directory"
cleanup() {
  rm -rf "$temporary"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

checksums=$temporary/SHA256SUMS
seen=$temporary/checksum-names
if [ "$version_provided" -eq 1 ]; then
  selected_version=$(normalize_version "$version_argument")
  asset_base="$base_url/download/v$selected_version"
  selected_name="netft-cli-$selected_version-$target.tar.gz"
  download "$asset_base/SHA256SUMS" "$checksums" ||
    die "unable to download SHA256SUMS"
  validate_checksum_inventory "$checksums" "$seen"
  assert_checksum_contract "$seen" "$selected_version"
  select_explicit_asset "$checksums" "$selected_name"
else
  asset_base="$base_url/latest/download"
  download "$asset_base/SHA256SUMS" "$checksums" ||
    die "unable to download latest SHA256SUMS"
  validate_checksum_inventory "$checksums" "$seen"
  select_latest_asset "$checksums"
  assert_checksum_contract "$seen" "$selected_version"
fi

archive=$temporary/$selected_name
download "$asset_base/$selected_name" "$archive" ||
  die "unable to download $selected_name"
actual_digest=$(sha256_file "$archive")
[ "$actual_digest" = "$selected_digest" ] ||
  die "checksum mismatch for $selected_name"

validate_and_extract_archive "$archive" "$temporary/extracted"
candidate="$temporary/extracted/netft-cli-$selected_version/netft"
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
