#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ARCOSH_BIN="arcosh"

usage() {
    cat <<USAGE
ArcoBASIC interactive Debian installer

Usage:
  $SCRIPT_NAME
  $SCRIPT_NAME optional/path/to/arcobasic.deb

Options:
  --latest    Use the newest *.deb beside this installer script. This is also the default.
  --help      Show this help.
USAGE
}

say() {
    printf '\n== %s ==\n' "$1"
}

ask() {
    local prompt="$1"
    local fallback="$2"
    local answer
    read -r -p "$prompt [$fallback]: " answer
    if [[ -z "$answer" ]]; then
        printf '%s\n' "$fallback"
    else
        printf '%s\n' "$answer"
    fi
}

ask_yes_no() {
    local prompt="$1"
    local fallback="$2"
    local answer
    read -r -p "$prompt [$fallback]: " answer
    answer="${answer:-$fallback}"
    case "${answer,,}" in
        y|yes) return 0 ;;
        *) return 1 ;;
    esac
}

need_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "$SCRIPT_NAME: missing required command: $1" >&2
        exit 1
    }
}

shell_quote() {
    printf "'%s'" "${1//\'/\'\\\'\'}"
}

short_cwd() {
    local cwd="$PWD"
    if [[ -n "${HOME:-}" && "$cwd" == "$HOME"* ]]; then
        printf '~%s\n' "${cwd#$HOME}"
    else
        printf '%s\n' "$cwd"
    fi
}

preview_prompt() {
    local pattern="$1"
    local preview="$pattern"
    preview="${preview//\{user\}/${USER:-user}}"
    preview="${preview//\{host\}/$(hostname 2>/dev/null || printf localhost)}"
    preview="${preview//\{cwd:short\}/$(short_cwd)}"
    preview="${preview//\{cwd\}/$PWD}"
    preview="${preview//\{status\}/0}"
    preview="${preview//\{shell\}/arcosh}"
    printf '%s\n' "$preview"
}

choose_prompt() {
    local pattern="{shell}:{cwd:short}:{status}> "
    while true; do
        printf '\n== Prompt Preview ==\n' >&2
        cat >&2 <<PROMPTS
1) sysadmin   {shell}:{cwd:short}:{status}> 
2) compact    {cwd:short}> 
3) classic    arcosh> 
4) power      {user}@{host}:{cwd:short} [{status}]> 
5) custom
PROMPTS
        printf 'Current preview: %s\n' "$(preview_prompt "$pattern")" >&2
        local choice
        choice="$(ask "Choose prompt preset, or accept" "accept")"
        case "${choice,,}" in
            1|sysadmin) pattern="{shell}:{cwd:short}:{status}> " ;;
            2|compact) pattern="{cwd:short}> " ;;
            3|classic) pattern="arcosh> " ;;
            4|power) pattern="{user}@{host}:{cwd:short} [{status}]> " ;;
            5|custom) pattern="$(ask "Prompt pattern" "$pattern")" ;;
            accept|a|done|"") printf '%s\n' "$pattern"; return 0 ;;
            *) echo "Unknown prompt choice: $choice" >&2 ;;
        esac
    done
}

write_prompt_config() {
    local profile_home="$1"
    local pattern="$2"
    local rc="$profile_home/rc.abas"
    local tmp
    mkdir -p "$profile_home"
    tmp="$(mktemp)"
    if [[ -f "$rc" ]]; then
        awk '
            /^'\'' BEGIN ARCOBASIC INSTALLER PROMPT$/ { skip=1; next }
            /^'\'' END ARCOBASIC INSTALLER PROMPT$/ { skip=0; next }
            skip == 0 { print }
        ' "$rc" > "$tmp"
    fi
    {
        cat "$tmp"
        printf "' BEGIN ARCOBASIC INSTALLER PROMPT\n"
        printf 'ArcoSH.SetPrompt("%s")\n' "${pattern//\"/\\\"}"
        printf "' END ARCOBASIC INSTALLER PROMPT\n"
    } > "$rc"
    rm -f "$tmp"
}

install_deb() {
    local deb="$1"
    say "Install Package"
    echo "Installing: $deb"
    sudo dpkg -i "$deb" || sudo apt-get install -f -y
}

configure_login_shell() {
    local shell_path="$1"
    local user_name="$2"

    if ask_yes_no "Add $shell_path to /etc/shells if missing" "y"; then
        if [[ -f /etc/shells ]] && grep -Fxq "$shell_path" /etc/shells; then
            echo "$shell_path is already listed in /etc/shells"
        else
            printf '%s\n' "$shell_path" | sudo tee -a /etc/shells >/dev/null
            echo "Added $shell_path to /etc/shells"
        fi
    fi

    if ask_yes_no "Change login shell for $user_name to ArcoSH" "n"; then
        chsh -s "$shell_path" "$user_name"
        echo "Login shell change requested. Sign out and back in for it to take effect."
    fi
}

install_builtin_mod() {
    local mod="$1"
    local examples="/usr/share/arcobasic/examples/arcosh_mods.abas"
    if [[ ! -f "$examples" ]]; then
        echo "Skipping $mod: mod manager example not found at $examples"
        return 0
    fi
    "$ARCOSH_BIN" --safe "$examples" install-builtin "$mod"
    "$ARCOSH_BIN" --safe "$examples" activate "$mod"
}

latest_deb() {
    find "$SCRIPT_DIR" -maxdepth 1 -name '*.deb' -type f -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR == 1 { print $2 }'
}

main() {
    if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
        usage
        exit 0
    fi

    need_command sudo
    need_command dpkg

    local deb="${1:-}"
    if [[ -z "$deb" || "$deb" == "--latest" ]]; then
        deb="$(latest_deb)"
    fi
    if [[ -z "$deb" ]]; then
        echo "$SCRIPT_NAME: no Debian package found beside installer: $SCRIPT_DIR" >&2
        exit 1
    fi
    if [[ ! -f "$deb" ]]; then
        echo "$SCRIPT_NAME: Debian package not found: $deb" >&2
        exit 1
    fi

    say "ArcoBASIC Installer"
    echo "Package: $deb"
    if ! ask_yes_no "Continue with package install" "y"; then
        echo "Aborted."
        exit 0
    fi

    install_deb "$deb"

    local arcosh_path
    arcosh_path="$(command -v arcosh || true)"
    if [[ -z "$arcosh_path" ]]; then
        arcosh_path="/usr/bin/arcosh"
    fi
    ARCOSH_BIN="$arcosh_path"

    say "Post-Install Doctor"
    if [[ -x "$ARCOSH_BIN" ]]; then
        "$ARCOSH_BIN" --doctor || true
    else
        echo "arcosh is not on PATH yet. Expected at $arcosh_path"
    fi

    local profile_home="${ARCOSH_HOME:-$HOME/.arcosh}"
    if ask_yes_no "Initialize ArcoSH profile at $profile_home" "y"; then
        "$ARCOSH_BIN" --init-profile
    fi

    if ask_yes_no "Configure prompt with live preview" "y"; then
        local prompt
        prompt="$(choose_prompt)"
        say "Selected Prompt"
        printf '%s\n' "$(preview_prompt "$prompt")"
        if ask_yes_no "Write this prompt to rc.abas" "y"; then
            write_prompt_config "$profile_home" "$prompt"
            echo "Updated $profile_home/rc.abas"
        fi
    fi

    if ask_yes_no "Install and activate ArcoGotchi terminal pet mod" "n"; then
        install_builtin_mod arcogotchi
    fi

    if ask_yes_no "Configure ArcoSH as a login shell" "n"; then
        local shell_path user_name
        shell_path="$(ask "Path to arcosh" "$arcosh_path")"
        user_name="$(ask "User for chsh" "${USER:-}")"
        configure_login_shell "$shell_path" "$user_name"
    fi

    say "Finish"
    echo "Installed ArcoBASIC from $(shell_quote "$deb")."
    echo "Try: arcosh --doctor"
    echo "Recovery: arcosh --safe"
}

main "$@"
