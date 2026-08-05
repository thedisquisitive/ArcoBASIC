#!/usr/bin/env bash
#
# install-xbps-builder.sh
#
# Installs an isolated XBPS/xbps-src package-building environment on Debian
# and launches an interactive package-template builder.
#
# This does NOT replace APT or install Void packages into Debian.
#
# Usage:
#   chmod +x install-xbps-builder.sh
#   ./install-xbps-builder.sh
#
# Optional custom location:
#   XBPS_WORKDIR="$HOME/my-xbps-workspace" ./install-xbps-builder.sh
#

set -Eeuo pipefail

XBPS_WORKDIR="${XBPS_WORKDIR:-$HOME/xbps}"
VOID_PACKAGES_DIR="$XBPS_WORKDIR/void-packages"

# ---------------------------------------------------------------------------
# Terminal formatting
# ---------------------------------------------------------------------------

if [[ -t 1 ]]; then
    BOLD=$'\033[1m'
    CYAN=$'\033[1;36m'
    GREEN=$'\033[1;32m'
    YELLOW=$'\033[1;33m'
    RED=$'\033[1;31m'
    RESET=$'\033[0m'
else
    BOLD=""
    CYAN=""
    GREEN=""
    YELLOW=""
    RED=""
    RESET=""
fi

log() {
    printf '\n%s==>%s %s\n' "$CYAN" "$RESET" "$*"
}

success() {
    printf '\n%s==>%s %s\n' "$GREEN" "$RESET" "$*"
}

warn() {
    printf '\n%sWARNING:%s %s\n' "$YELLOW" "$RESET" "$*" >&2
}

die() {
    printf '\n%sERROR:%s %s\n' "$RED" "$RESET" "$*" >&2
    exit 1
}

trap 'die "The script failed near line $LINENO."' ERR

# ---------------------------------------------------------------------------
# Prompt helpers
# ---------------------------------------------------------------------------

prompt() {
    local message="$1"
    local default_value="${2:-}"
    local answer

    if [[ -n "$default_value" ]]; then
        read -r -p "$message [$default_value]: " answer
        printf '%s' "${answer:-$default_value}"
    else
        read -r -p "$message: " answer
        printf '%s' "$answer"
    fi
}

prompt_required() {
    local message="$1"
    local answer=""

    while [[ -z "$answer" ]]; do
        answer="$(prompt "$message")"

        if [[ -z "$answer" ]]; then
            warn "A value is required."
        fi
    done

    printf '%s' "$answer"
}

confirm() {
    local message="$1"
    local default_answer="${2:-y}"
    local answer

    if [[ "$default_answer" == "y" ]]; then
        read -r -p "$message [Y/n]: " answer
        answer="${answer:-y}"
    else
        read -r -p "$message [y/N]: " answer
        answer="${answer:-n}"
    fi

    [[ "${answer,,}" == "y" || "${answer,,}" == "yes" ]]
}

escape_template_string() {
    local value="$1"

    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"

    printf '%s' "$value"
}

validate_package_name() {
    local value="$1"

    [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9.+-]*$ ]]
}

validate_version() {
    local value="$1"

    # XBPS versions must contain at least one digit and cannot contain
    # underscores or hyphens.
    [[ "$value" =~ [0-9] ]] &&
        [[ "$value" != *"_"* ]] &&
        [[ "$value" != *"-"* ]] &&
        [[ "$value" != *[[:space:]]* ]]
}

# ---------------------------------------------------------------------------
# Host setup
# ---------------------------------------------------------------------------

check_host() {
    if [[ "$(uname -s)" != "Linux" ]]; then
        die "This script requires Linux."
    fi

    if [[ ! -f /etc/debian_version ]]; then
        warn "This system does not appear to be Debian-based."
        warn "The dependency installation step may need adjustment."
    fi

    if [[ $EUID -eq 0 ]]; then
        die "Run this script as a normal user, not as root."
    fi

    if ! command -v sudo >/dev/null 2>&1; then
        die "sudo is required for installing Debian dependencies."
    fi
}

install_host_dependencies() {
    log "Installing Debian host dependencies"

    sudo apt-get update

    sudo apt-get install -y \
        bash \
        binutils \
        bzip2 \
        ca-certificates \
        coreutils \
        curl \
        file \
        git \
        gzip \
        libarchive-tools \
        make \
        patch \
        tar \
        unzip \
        util-linux \
        xz-utils \
        zstd
}

install_void_packages_tree() {
    mkdir -p "$XBPS_WORKDIR"

    if [[ -d "$VOID_PACKAGES_DIR/.git" ]]; then
        log "Updating the existing void-packages checkout"

        git -C "$VOID_PACKAGES_DIR" fetch --prune origin
        git -C "$VOID_PACKAGES_DIR" switch master
        git -C "$VOID_PACKAGES_DIR" pull --ff-only
    elif [[ -e "$VOID_PACKAGES_DIR" ]]; then
        die "$VOID_PACKAGES_DIR exists but is not a Git checkout."
    else
        log "Cloning the Void Linux package collection"

        git clone \
            --filter=blob:none \
            https://github.com/void-linux/void-packages.git \
            "$VOID_PACKAGES_DIR"
    fi
}

configure_xbps_src() {
    local jobs

    jobs="$(nproc 2>/dev/null || printf '1')"

    log "Configuring xbps-src"

    mkdir -p \
        "$VOID_PACKAGES_DIR/etc" \
        "$VOID_PACKAGES_DIR/hostdir/sources"

    if [[ ! -f "$VOID_PACKAGES_DIR/etc/conf" ]]; then
        cat > "$VOID_PACKAGES_DIR/etc/conf" <<EOF
# Local xbps-src configuration generated by install-xbps-builder.sh

XBPS_DISTDIR=\$XBPS_HOSTDIR/sources
XBPS_MAKEJOBS=$jobs

# Private packages may use licenses or sources that are unsuitable for the
# official Void repository.
XBPS_ALLOW_RESTRICTED=yes

# Rebuild local packages when their template or source changes.
XBPS_PRESERVE_PKGS=no
EOF
    else
        success "Keeping the existing xbps-src configuration:"
        printf '  %s\n' "$VOID_PACKAGES_DIR/etc/conf"
    fi
}

bootstrap_xbps_src() {
    cd "$VOID_PACKAGES_DIR"

    if [[ -d masterdir ]] && [[ -x masterdir/usr/bin/xbps-install ]]; then
        success "The Void build environment is already bootstrapped."
    else
        log "Bootstrapping the isolated Void build environment"

        ./xbps-src binary-bootstrap
    fi
}

# ---------------------------------------------------------------------------
# Template creation
# ---------------------------------------------------------------------------

choose_build_style() {
    local selection

    printf '\n%sBuild system%s\n' "$BOLD" "$RESET"
    printf '  1) CMake\n'
    printf '  2) Meson\n'
    printf '  3) GNU Autotools / configure\n'
    printf '  4) Plain Makefile\n'
    printf '  5) Python PEP 517\n'
    printf '  6) Custom build commands\n'

    while true; do
        selection="$(prompt "Choose a build system" "1")"

        case "$selection" in
            1)
                printf 'cmake'
                return
                ;;
            2)
                printf 'meson'
                return
                ;;
            3)
                printf 'gnu-configure'
                return
                ;;
            4)
                printf 'gnu-makefile'
                return
                ;;
            5)
                printf 'python3-pep517'
                return
                ;;
            6)
                printf 'custom'
                return
                ;;
            *)
                warn "Choose a number from 1 through 6."
                ;;
        esac
    done
}

write_dependency_variable() {
    local variable_name="$1"
    local variable_value="$2"
    local output_file="$3"

    if [[ -n "$variable_value" ]]; then
        printf '%s="%s"\n' \
            "$variable_name" \
            "$(escape_template_string "$variable_value")" \
            >> "$output_file"
    fi
}

write_standard_build_style() {
    local build_style="$1"
    local template_file="$2"

    case "$build_style" in
        cmake)
            cat >> "$template_file" <<'EOF'

build_style=cmake
EOF
            ;;

        meson)
            cat >> "$template_file" <<'EOF'

build_style=meson
EOF
            ;;

        gnu-configure)
            cat >> "$template_file" <<'EOF'

build_style=gnu-configure
EOF
            ;;

        gnu-makefile)
            cat >> "$template_file" <<'EOF'

build_style=gnu-makefile
EOF
            ;;

        python3-pep517)
            cat >> "$template_file" <<'EOF'

build_style=python3-pep517
hostmakedepends+=" python3-build python3-installer"
EOF
            ;;
    esac
}

write_custom_build_functions() {
    local template_file="$1"
    local configure_command="$2"
    local build_command="$3"
    local install_command="$4"

    if [[ -n "$configure_command" ]]; then
        cat >> "$template_file" <<EOF

do_configure() {
    $configure_command
}
EOF
    fi

    if [[ -n "$build_command" ]]; then
        cat >> "$template_file" <<EOF

do_build() {
    $build_command
}
EOF
    fi

    cat >> "$template_file" <<EOF

do_install() {
    $install_command
}
EOF
}

create_template() {
    local pkgname
    local version
    local revision
    local short_desc
    local maintainer
    local license
    local homepage
    local distfiles
    local build_style
    local hostmakedepends
    local makedepends
    local checkdepends
    local runtime_depends
    local configure_args
    local make_build_args
    local make_install_args
    local configure_command=""
    local build_command=""
    local install_command=""
    local package_dir
    local template_file
    local escaped_desc
    local escaped_maintainer
    local escaped_license
    local escaped_homepage
    local escaped_distfiles

    printf '\n'
    printf '%sXBPS Interactive Template Builder%s\n' "$BOLD" "$RESET"
    printf '%s\n' '---------------------------------'
    printf 'Enter the package metadata and build information.\n'
    printf 'Press Enter to accept values shown in brackets.\n\n'

    while true; do
        pkgname="$(prompt_required "Package name")"

        if validate_package_name "$pkgname"; then
            break
        fi

        warn "Use letters, numbers, dots, plus signs, and hyphens only."
    done

    while true; do
        version="$(prompt "Version" "0.1.0")"

        if validate_version "$version"; then
            break
        fi

        warn "The version must contain a digit and cannot contain '-' or '_'."
    done

    revision="$(prompt "Package revision" "1")"

    if [[ ! "$revision" =~ ^[1-9][0-9]*$ ]]; then
        warn "Invalid revision. Using revision 1."
        revision="1"
    fi

    short_desc="$(prompt "Short description" "$pkgname package")"

    if (( ${#short_desc} > 72 )); then
        warn "XBPS descriptions should be no longer than 72 characters."
        short_desc="${short_desc:0:72}"
    fi

    maintainer="$(prompt "Maintainer" "Local Builder <local@localhost>")"
    license="$(prompt "License" "custom:private")"
    homepage="$(prompt "Homepage or project URL" "https://localhost/$pkgname")"

    printf '\n%sSource archive%s\n' "$BOLD" "$RESET"
    printf 'Enter an HTTP, HTTPS, or file:// URL pointing to a source archive.\n'
    printf 'Examples:\n'
    printf '  https://example.com/releases/%s-%s.tar.gz\n' \
        "$pkgname" "$version"
    printf '  file:///home/user/releases/%s-%s.tar.gz\n\n' \
        "$pkgname" "$version"

    distfiles="$(prompt_required "Source archive URL")"
    build_style="$(choose_build_style)"

    printf '\n%sDependencies%s\n' "$BOLD" "$RESET"
    printf 'Use Void package names separated by spaces.\n'
    printf 'Leave a field blank when it is not needed.\n\n'

    hostmakedepends="$(prompt "Host build-tool dependencies")"
    makedepends="$(prompt "Compile/link dependencies")"
    checkdepends="$(prompt "Test dependencies")"
    runtime_depends="$(prompt "Explicit runtime dependencies")"

    case "$build_style" in
        cmake)
            if [[ -z "$hostmakedepends" ]]; then
                hostmakedepends="cmake ninja pkg-config"
            fi
            ;;

        meson)
            if [[ -z "$hostmakedepends" ]]; then
                hostmakedepends="meson ninja pkg-config"
            fi
            ;;

        gnu-configure)
            if [[ -z "$hostmakedepends" ]]; then
                hostmakedepends="automake libtool pkg-config"
            fi
            ;;

        python3-pep517)
            if [[ -z "$hostmakedepends" ]]; then
                hostmakedepends="python3-build python3-installer"
            fi
            ;;
    esac

    configure_args=""
    make_build_args=""
    make_install_args=""

    if [[ "$build_style" != "custom" ]]; then
        printf '\n%sOptional build arguments%s\n' "$BOLD" "$RESET"

        configure_args="$(prompt "Configure arguments")"
        make_build_args="$(prompt "Build arguments")"
        make_install_args="$(prompt "Install arguments")"
    else
        printf '\n%sCustom build commands%s\n' "$BOLD" "$RESET"
        printf 'Commands run from the extracted source directory.\n'
        printf 'Use XBPS variables such as $DESTDIR when installing files.\n\n'

        configure_command="$(prompt "Configure command, blank for none")"
        build_command="$(prompt "Build command" "make -j\${XBPS_MAKEJOBS}")"
        install_command="$(
            prompt \
                "Install command" \
                "make DESTDIR=\"\${DESTDIR}\" PREFIX=/usr install"
        )"
    fi

    package_dir="$VOID_PACKAGES_DIR/srcpkgs/$pkgname"
    template_file="$package_dir/template"

    if [[ -e "$template_file" ]]; then
        warn "A template already exists at:"
        printf '  %s\n' "$template_file"

        if ! confirm "Overwrite the existing template?" "n"; then
            printf '\nTemplate creation cancelled.\n'
            return 1
        fi

        cp "$template_file" "$template_file.backup"
        success "Existing template backed up to:"
        printf '  %s\n' "$template_file.backup"
    fi

    mkdir -p "$package_dir"

    escaped_desc="$(escape_template_string "$short_desc")"
    escaped_maintainer="$(escape_template_string "$maintainer")"
    escaped_license="$(escape_template_string "$license")"
    escaped_homepage="$(escape_template_string "$homepage")"
    escaped_distfiles="$(escape_template_string "$distfiles")"

    cat > "$template_file" <<EOF
# Template file for '$pkgname'
pkgname=$pkgname
version=$version
revision=$revision

short_desc="$escaped_desc"
maintainer="$escaped_maintainer"
license="$escaped_license"
homepage="$escaped_homepage"

distfiles="$escaped_distfiles"
checksum=SKIP
EOF

    write_standard_build_style "$build_style" "$template_file"

    write_dependency_variable \
        "hostmakedepends" \
        "$hostmakedepends" \
        "$template_file"

    write_dependency_variable \
        "makedepends" \
        "$makedepends" \
        "$template_file"

    write_dependency_variable \
        "checkdepends" \
        "$checkdepends" \
        "$template_file"

    write_dependency_variable \
        "depends" \
        "$runtime_depends" \
        "$template_file"

    write_dependency_variable \
        "configure_args" \
        "$configure_args" \
        "$template_file"

    write_dependency_variable \
        "make_build_args" \
        "$make_build_args" \
        "$template_file"

    write_dependency_variable \
        "make_install_args" \
        "$make_install_args" \
        "$template_file"

    if [[ "$build_style" == "custom" ]]; then
        write_custom_build_functions \
            "$template_file" \
            "$configure_command" \
            "$build_command" \
            "$install_command"
    fi

    success "Created package template:"
    printf '  %s\n\n' "$template_file"

    printf '%sGenerated template%s\n' "$BOLD" "$RESET"
    printf '%s\n' '---------------------------------'
    cat "$template_file"
    printf '%s\n' '---------------------------------'

    warn "This template uses checksum=SKIP."
    warn "That is convenient for private builds but unsuitable for submission"
    warn "to the official Void Linux package repository."

    if confirm "Build $pkgname now?" "y"; then
        build_package "$pkgname"
    else
        print_build_instructions "$pkgname"
    fi
}

# ---------------------------------------------------------------------------
# Package build
# ---------------------------------------------------------------------------

build_package() {
    local pkgname="$1"
    local package_files=()

    cd "$VOID_PACKAGES_DIR"

    log "Cleaning previous build state for $pkgname"

    ./xbps-src clean "$pkgname" >/dev/null 2>&1 || true

    log "Building $pkgname"

    ./xbps-src pkg "$pkgname"

    while IFS= read -r -d '' package_file; do
        package_files+=("$package_file")
    done < <(
        find "$VOID_PACKAGES_DIR/hostdir/binpkgs" \
            -type f \
            -name "${pkgname}-*.xbps" \
            -print0 2>/dev/null
    )

    success "Package build completed."

    if (( ${#package_files[@]} > 0 )); then
        printf '\n%sGenerated package files%s\n' "$BOLD" "$RESET"

        printf '  %s\n' "${package_files[@]}"
    else
        printf '\nLocal repository:\n'
        printf '  %s\n' "$VOID_PACKAGES_DIR/hostdir/binpkgs"
    fi

    printf '\nInstall on a Void system with:\n'
    printf '  sudo xbps-install --repository=/path/to/binpkgs %s\n' "$pkgname"
}

print_build_instructions() {
    local pkgname="$1"

    cat <<EOF

Build it later with:

  cd "$VOID_PACKAGES_DIR"
  ./xbps-src pkg "$pkgname"

Clean and rebuild it with:

  cd "$VOID_PACKAGES_DIR"
  ./xbps-src clean "$pkgname"
  ./xbps-src pkg "$pkgname"

Packages are written beneath:

  $VOID_PACKAGES_DIR/hostdir/binpkgs
EOF
}

# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

main_menu() {
    local selection

    while true; do
        printf '\n%sWhat would you like to do?%s\n' "$BOLD" "$RESET"
        printf '  1) Create a package template\n'
        printf '  2) Build an existing package template\n'
        printf '  3) Exit\n'

        selection="$(prompt "Choose an option" "1")"

        case "$selection" in
            1)
                create_template || true
                ;;

            2)
                local existing_package
                existing_package="$(prompt_required "Existing package name")"

                if [[ ! -f \
                    "$VOID_PACKAGES_DIR/srcpkgs/$existing_package/template" ]]
                then
                    warn "No template exists for '$existing_package'."
                    continue
                fi

                build_package "$existing_package"
                ;;

            3)
                break
                ;;

            *)
                warn "Choose 1, 2, or 3."
                ;;
        esac

        if ! confirm "Return to the package menu?" "n"; then
            break
        fi
    done
}

main() {
    check_host
    install_host_dependencies
    install_void_packages_tree
    configure_xbps_src
    bootstrap_xbps_src

    success "The XBPS package-building environment is ready."
    printf '\nWorkspace:\n'
    printf '  %s\n' "$VOID_PACKAGES_DIR"

    main_menu

    printf '\nDone. The package foundry is cooling down. ⚙️\n'
}

main "$@"

