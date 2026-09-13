#!/usr/bin/env bash

set -euo pipefail

# PlatformIO executable path (VS Code extension manages its own environment)
PIO="${HOME}/.platformio/penv/bin/pio"

repository_root="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.." &&
    pwd
)"

for project_directory in "${repository_root}"/projects/*; do
    if [[ ! -f "${project_directory}/platformio.ini" ]]; then
        continue
    fi

    project_name="$(basename "${project_directory}")"

    echo
    echo "Building ${project_name}"

    "${PIO}" run --project-dir "${project_directory}"
done

echo
echo "All PlatformIO projects built successfully."