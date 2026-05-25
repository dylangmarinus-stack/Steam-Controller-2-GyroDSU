#!/bin/sh
set -e

die() {
    echo ""
    echo "  ✗ $1"
    echo ""
    read -r -p "  Press Enter to close." _
    exit 1
}

echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║    SteamControllerGyroDSU — Downloading       ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""

cd /tmp

echo "  Downloading latest release..."
curl -fsSL -o SteamControllerGyroDSUSetup.zip \
    https://github.com/TyanColte/Steam-Controller-GyroDSU/releases/latest/download/SteamControllerGyroDSUSetup.zip \
    || die "Download failed. Check your internet connection and try again."

echo "  Extracting..."
unzip -o SteamControllerGyroDSUSetup.zip -d /tmp \
    || die "Extraction failed — zip may be corrupt."

cd /tmp/SteamControllerGyroDSUSetup \
    || die "Extracted folder not found."

bash install.sh

rm -rf /tmp/SteamControllerGyroDSUSetup /tmp/SteamControllerGyroDSUSetup.zip
