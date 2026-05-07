#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-License-Identifier: Apache-2.0
#
# Maintainer: Andy Curtis <contactandyc@gmail.com>

set -Eeuo pipefail

# --- Discover and source scoped environment ---
_cur="$PWD"
while [ "$_cur" != "/" ]; do
  if [ -f "$_cur/.scaffoldrc.yaml" ]; then
    [ -f "$_cur/.scaffoldrc_c_cmake_app" ] && source "$_cur/.scaffoldrc_c_cmake_app"
    break
  fi
  _cur="$(dirname "$_cur")"
done
[ -z "${WORKSPACE_DIR:-}" ] && [ -f "$HOME/.scaffoldrc_c_cmake_app" ] && source "$HOME/.scaffoldrc_c_cmake_app"

# --- Script Commands ---
COMMAND="${1:-build}"

# --- Knobs ---
PREFIX="${PREFIX:-/usr/local}"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
BUILD_VARIANT="${BUILD_VARIANT:-static}"
GENERATOR="${GENERATOR:-}"

pick_generator() {
  if [ -n "$GENERATOR" ]; then return; fi
  if command -v ninja >/dev/null; then GENERATOR="Ninja"
  elif command -v make >/dev/null; then GENERATOR="Unix Makefiles"
  else echo "Error: No build tool found. Install Ninja or Make." >&2; exit 1; fi
}

case "$COMMAND" in
  bootstrap)
    echo "--- Bootstrapping Hermetic Workspace ---"

    # --- FAILSAFE: Shield against leaky parent environments ---
    export PREFIX="$PWD/repos/install"
    export WORKSPACE_DIR="$PWD/repos"

    # 1. Fetch the scaffolding engine to disk so it is visible and editable
    mkdir -p repos
    if [ ! -d "repos/scaffold-repo" ]; then
        echo "📦 Fetching scaffold engine into repos/scaffold-repo..."
        git clone --branch "main" \
            "https://github.com/contactandyc/scaffold-repo.git" \
            "repos/scaffold-repo"
    else
        echo "📦 Scaffold engine already exists at repos/scaffold-repo. Leaving untouched."
    fi

    # 2. Isolate the Python scaffolding environment (The Sandbox)
    if [ ! -d ".scaffold/venv" ]; then
        echo "Creating isolated Python sandbox..."
        python3 -m venv .scaffold/venv
        source .scaffold/venv/bin/activate
        pip install --upgrade pip -q
    else
        source .scaffold/venv/bin/activate
    fi

    # ALWAYS sync the engine's entry points into the sandbox
    echo "Installing scaffold engine into sandbox..."
    pip install -q -e repos/scaffold-repo

    # 3. Generate local hermetic configs & clone dependencies recursively
    scaffold-bootstrap

    # 4. Compile the newly cloned dependencies
    scaffold-repo --build-deps

    echo ""
    echo "✅ Bootstrap complete! All dependencies are isolated in your workspace."

    # Automatically cascade into a first-party build!
    "$0" build
    ;;

  build|install)
    pick_generator
    echo "--- Building Application (Generator: $GENERATOR, Type: $BUILD_TYPE, Linking: $BUILD_VARIANT) ---"

    cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
      -DCMAKE_PREFIX_PATH="$PREFIX" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DA_BUILD_VARIANT="$BUILD_VARIANT"

    cmake --build "$BUILD_DIR" -j

    if [ "$COMMAND" = "install" ]; then
      echo "--- Installing to '$PREFIX' ---"
      INSTALL_SUDO=""
      if [[ "$PREFIX" == "/usr"* ]] && [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
        INSTALL_SUDO="sudo"
      fi
      $INSTALL_SUDO cmake --install "$BUILD_DIR"
      echo "✅ Install complete."
    else
      echo "✅ Build complete. Artifacts are in '$BUILD_DIR/'."
    fi
    ;;

  clean)
    echo "--- Cleaning Build Directories ---"
    rm -rf "$BUILD_DIR"
    echo "✅ Clean complete."
    ;;

  *)
    echo "Usage: $0 [bootstrap|build|install|clean]" >&2
    exit 1
    ;;
esac
