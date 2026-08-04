#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build}"
prefix="${PREFIX:-/usr/local}"
bench_dir="${BENCH_DIR:-/etc/arcology-lazarus}"
run_tests="${RUN_TESTS:-1}"

usage() {
    cat <<EOF
Arcology Lazarus installer

Usage:
  lazarus/scripts/install.sh [options]

Options:
  --prefix PATH       Install prefix. Default: ${prefix}
  --build-dir PATH    CMake build directory. Default: ${build_dir}
  --bench-dir PATH    Bench profile directory. Default: ${bench_dir}
  --skip-tests        Build without running ctest.
  -h, --help          Show this help.

Environment:
  PREFIX, BUILD_DIR, BENCH_DIR, RUN_TESTS

Use sudo when installing to system paths:
  sudo lazarus/scripts/install.sh
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            prefix="${2:?--prefix requires a path}"
            shift 2
            ;;
        --build-dir)
            build_dir="${2:?--build-dir requires a path}"
            shift 2
            ;;
        --bench-dir)
            bench_dir="${2:?--bench-dir requires a path}"
            shift 2
            ;;
        --skip-tests)
            run_tests=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

require_command cmake

echo "Configuring Lazarus..."
cmake -S "${project_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" -DCMAKE_INSTALL_PREFIX="${prefix}"

echo "Building Lazarus..."
cmake --build "${build_dir}"

if [[ "${run_tests}" == "1" ]]; then
    echo "Running Lazarus smoke tests..."
    ctest --test-dir "${build_dir}" --output-on-failure
fi

echo "Installing binaries and shared files to ${prefix}..."
cmake --install "${build_dir}"

echo "Installing bench profile template to ${bench_dir}..."
install -d "${bench_dir}"
if [[ ! -e "${bench_dir}/bench-alpha.profile" ]]; then
    install -m 0644 "${project_dir}/examples/bench-alpha.profile" "${bench_dir}/bench-alpha.profile"
else
    echo "Keeping existing ${bench_dir}/bench-alpha.profile"
fi

cat <<EOF

Installed:
  ${prefix}/bin/lazarus
  ${prefix}/bin/lazarus-tui
  ${prefix}/bin/lazarus-gui
  ${prefix}/bin/lazarum
  ${prefix}/bin/lazarum-gui
  ${prefix}/sbin/lazarus-service
  ${bench_dir}/bench-alpha.profile

Run:
  ${prefix}/bin/lazarus-tui ${bench_dir}/bench-alpha.profile
  ${prefix}/bin/lazarus-gui ${bench_dir}/bench-alpha.profile
  sudo ${prefix}/sbin/lazarus-service --config ${bench_dir}/bench-alpha.profile --socket /run/arcology-lazarus/service.sock

Edit the bench profile before real imaging or restore work. Bench paths must match your actual source-only and destination-only device identities.
EOF
