#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_arcosh="${repo_root}/build/arcosh"
desktop_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
mime_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/mime/packages"
icon_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor/512x512"
pixmaps_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/pixmaps"

if [[ ! -x "${build_arcosh}" ]]; then
    echo "Build arcosh first: cmake --build build -j2" >&2
    exit 1
fi

mkdir -p "${desktop_dir}" "${mime_dir}" "${icon_dir}/apps" "${icon_dir}/mimetypes" "${pixmaps_dir}"

cp "${repo_root}/packaging/linux/application-x-arcobasic.xml" "${mime_dir}/application-x-arcobasic.xml"
cp "${repo_root}/assets/arconaut/title-icon.png" "${icon_dir}/apps/arconaut.png"
cp "${repo_root}/assets/arconaut/title-icon.png" "${pixmaps_dir}/arconaut.png"
cp "${repo_root}/assets/arconaut/arcobasic-file.png" "${icon_dir}/mimetypes/application-x-arcobasic.png"

sed \
    -e "s|@CMAKE_INSTALL_FULL_BINDIR@/arcosh|${build_arcosh}|g" \
    -e "s|@CMAKE_INSTALL_FULL_DATADIR@/arcobasic/examples/arconaut.abas|${repo_root}/examples/arconaut.abas|g" \
    "${repo_root}/packaging/linux/arconaut.desktop.in" > "${desktop_dir}/arconaut.desktop"

chmod 0644 "${desktop_dir}/arconaut.desktop"

if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "${XDG_DATA_HOME:-${HOME}/.local/share}/mime" >/dev/null
fi

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${desktop_dir}" >/dev/null || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f "${XDG_DATA_HOME:-${HOME}/.local/share}/icons/hicolor" >/dev/null || true
fi

echo "Installed Arconaut desktop integration into ${XDG_DATA_HOME:-${HOME}/.local/share}"
