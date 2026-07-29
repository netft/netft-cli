#!/usr/bin/env bash

set -euo pipefail

curl_version="8.21.0"
curl_archive_sha256="aa1b66a70eace83dc624508745646c08ae561de512ab403adffb93ac87fc72e6"
curl_source_url="https://curl.se/download/curl-${curl_version}.tar.xz"
prefix="${NETFT_CLI_CURL_PREFIX:?NETFT_CLI_CURL_PREFIX is required}"
build_jobs="${NETFT_CLI_BUILD_JOBS:-2}"
build_root="$(mktemp -d)"
archive="${NETFT_CLI_CURL_ARCHIVE_CACHE:-${build_root}/curl-${curl_version}.tar.xz}"
source_root="${build_root}/source"

cleanup() {
  rm -rf "${build_root}"
}
trap cleanup EXIT

if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
  echo "NETFT_CLI_BUILD_JOBS must be a positive integer" >&2
  exit 1
fi
if [[ "${curl_source_url}" != https://* ]]; then
  echo "curl source URL must use HTTPS" >&2
  exit 1
fi

mkdir -p "$(dirname "${archive}")"
if [[ ! -f "${archive}" ]]; then
  curl --fail --location --silent --show-error \
    --proto '=https' --tlsv1.2 \
    "${curl_source_url}" -o "${archive}"
fi

if command -v shasum >/dev/null 2>&1; then
  actual_sha256="$(shasum -a 256 "${archive}" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
  actual_sha256="$(sha256sum "${archive}" | awk '{print $1}')"
else
  echo "shasum or sha256sum is required" >&2
  exit 1
fi
if [[ "${actual_sha256}" != "${curl_archive_sha256}" ]]; then
  echo "curl source archive checksum mismatch" >&2
  exit 1
fi

mkdir -p "${source_root}"
tar -xJf "${archive}" --strip-components=1 -C "${source_root}"

cd "${source_root}"
./configure \
  --prefix="${prefix}" \
  --disable-shared \
  --enable-static \
  --disable-dependency-tracking \
  --with-pic \
  --enable-http \
  --disable-dict \
  --disable-file \
  --disable-ftp \
  --disable-gopher \
  --disable-imap \
  --disable-ipfs \
  --disable-ldap \
  --disable-ldaps \
  --disable-mqtt \
  --disable-pop3 \
  --disable-rtsp \
  --disable-smb \
  --disable-smtp \
  --disable-telnet \
  --disable-tftp \
  --disable-websockets \
  --disable-manual \
  --disable-docs \
  --without-ssl \
  --without-libpsl \
  --without-zlib \
  --without-brotli \
  --without-zstd \
  --without-libidn2 \
  --without-nghttp2 \
  --without-ngtcp2 \
  --without-nghttp3 \
  --without-quiche \
  --without-libuv \
  --without-libgsasl \
  --without-libssh2 \
  --without-libssh \
  --without-gssapi
make -j"${build_jobs}"
make install

if [[ "$("${prefix}/bin/curl-config" --version)" != "libcurl ${curl_version}" ]]; then
  echo "installed curl version does not match the pinned release" >&2
  exit 1
fi
if [[ "$("${prefix}/bin/curl-config" --protocols)" != "HTTP" ]]; then
  echo "installed curl protocol set is not HTTP-only" >&2
  exit 1
fi
if [[ ! -f "${prefix}/lib/libcurl.a" ]]; then
  echo "curl static library was not installed" >&2
  exit 1
fi
if compgen -G "${prefix}/lib/libcurl.so*" >/dev/null ||
  compgen -G "${prefix}/lib/libcurl*.dylib" >/dev/null; then
  echo "curl shared library was unexpectedly installed" >&2
  exit 1
fi
