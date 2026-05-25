#!/bin/sh
# SteamControllerGyroDSU installer
# Supports: Steam Deck (SteamOS), Bazzite, and generic Linux.

INSTALL_DIR="$HOME/SteamControllerGyroDSU"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
cd "$SCRIPT_DIR"

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { printf "  %s\n" "$1"; }
ok()    { printf "${GREEN}  ✓ %s${NC}\n" "$1"; }
warn()  { printf "${YELLOW}  ⚠ %s${NC}\n" "$1"; }
err()   { printf "${RED}  ✗ %s${NC}\n" "$1"; }
hdr()   { printf "\n${GREEN}▶ %s${NC}\n" "$1"; }

echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║    SteamControllerGyroDSU — Installer         ║"
echo "╚═══════════════════════════════════════════════╝"

# ── Platform detection ───────────────────────────────────────────────────────
PLATFORM="linux"
PLATFORM_NAME="Generic Linux"
if [ -f /etc/os-release ]; then
    . /etc/os-release
    _str="$ID ${ID_LIKE:-} ${VARIANT_ID:-}"
    case "$_str" in
        *bazzite*)    PLATFORM="bazzite";   PLATFORM_NAME="Bazzite"     ;;
        *steamos*)    PLATFORM="steamdeck"; PLATFORM_NAME="Steam Deck"  ;;
        *steamdeck*)  PLATFORM="steamdeck"; PLATFORM_NAME="Steam Deck"  ;;
    esac
fi
info "Platform detected: $PLATFORM_NAME"

# ── Stop existing service ────────────────────────────────────────────────────
hdr "Stopping existing service"
systemctl --user -q stop    SteamControllerGyroDSU.service 2>/dev/null && ok "Service stopped"  || true
systemctl --user -q disable SteamControllerGyroDSU.service 2>/dev/null || true

# ── Install binaries ─────────────────────────────────────────────────────────
hdr "Installing binaries"
mkdir -p "$INSTALL_DIR"

cp SteamControllerGyroDSU "$INSTALL_DIR/"
chmod +x "$INSTALL_DIR/SteamControllerGyroDSU"
ok "sc2gyrodsu daemon installed"

if [ -f sc2gyrodsu-config ]; then
    cp sc2gyrodsu-config "$INSTALL_DIR/"
    chmod +x "$INSTALL_DIR/sc2gyrodsu-config"
    ok "sc2gyrodsu-config GUI installed"
fi

# ── Library check ────────────────────────────────────────────────────────────
hdr "Checking library dependencies"
_MISSING=$(ldd "$INSTALL_DIR/SteamControllerGyroDSU" 2>/dev/null \
    | awk '/not found/{print $1}' | tr '\n' ' ')

if [ -z "$_MISSING" ]; then
    ok "All libraries satisfied"
else
    warn "Missing libraries: $_MISSING"
    case "$PLATFORM" in
        bazzite)
            info "On Bazzite, install via Homebrew:  brew install hidapi"
            info "Or via rpm-ostree (requires reboot): rpm-ostree install hidapi"
            ;;
        steamdeck)
            info "On SteamOS: sudo pacman -S hidapi"
            info "(Package resets on OS updates — prefer the static release binary)"
            ;;
        *)
            info "Install libhidapi-hidraw for your distribution."
            info "e.g. Ubuntu/Debian: sudo apt install libhidapi-hidraw0"
            info "     Arch:          sudo pacman -S hidapi"
            info "     Fedora:        sudo dnf install hidapi"
            ;;
    esac
fi

# ── udev rules ───────────────────────────────────────────────────────────────
hdr "HID device access (udev)"

# Check whether Steam or another package already ships rules for Valve VID 28de.
_STEAM_RULES=$(find /lib/udev/rules.d /usr/lib/udev/rules.d /run/udev/rules.d \
    2>/dev/null -name "*.rules" -exec grep -l "idVendor.*28de\|28de.*idVendor" {} \; \
    | head -1)

if [ -n "$_STEAM_RULES" ]; then
    ok "Valve HID rules already present: $(basename "$_STEAM_RULES")"
    info "No custom udev rules needed."
    _INSTALLED_RULES=0
else
    info "No existing Valve HID rules found — installing our own."
    _RULES_DEST="/etc/udev/rules.d/99-sc2gyrodsu.rules"
    if sudo cp "$SCRIPT_DIR/99-sc2gyrodsu.rules" "$_RULES_DEST" 2>/dev/null; then
        sudo udevadm control --reload-rules 2>/dev/null || true
        sudo udevadm trigger --subsystem-match=hidraw 2>/dev/null || true
        ok "udev rules installed to $_RULES_DEST"
        _INSTALLED_RULES=1
    else
        warn "Could not write to /etc/udev/rules.d/ (read-only or no sudo)."
        info "If the controller is not detected, manually copy:"
        info "  sudo cp $SCRIPT_DIR/99-sc2gyrodsu.rules /etc/udev/rules.d/"
        info "  sudo udevadm control --reload-rules && sudo udevadm trigger"
        _INSTALLED_RULES=0
    fi
fi

# ── input group membership ───────────────────────────────────────────────────
hdr "User group membership"
if id -nG "$USER" | tr ' ' '\n' | grep -qx "input"; then
    ok "User '$USER' is already in the 'input' group"
else
    info "Adding '$USER' to the 'input' group..."
    if sudo usermod -aG input "$USER" 2>/dev/null; then
        ok "Added. Changes take effect after next login or reboot."
    else
        warn "Could not add '$USER' to the input group."
        info "Run manually: sudo usermod -aG input $USER  (then log out and back in)"
    fi
fi

# ── Install scripts and rules ────────────────────────────────────────────────
hdr "Installing scripts"
cp update.sh   "$INSTALL_DIR/"
cp uninstall.sh "$INSTALL_DIR/"
cp 99-sc2gyrodsu.rules "$INSTALL_DIR/" 2>/dev/null || true
chmod +x "$INSTALL_DIR/update.sh" "$INSTALL_DIR/uninstall.sh"
# Pass _INSTALLED_RULES to uninstall.sh via a marker file
if [ "${_INSTALLED_RULES:-0}" = "1" ]; then
    touch "$INSTALL_DIR/.udev_rules_installed"
else
    rm -f "$INSTALL_DIR/.udev_rules_installed"
fi
ok "Scripts installed"

# ── systemd user service ─────────────────────────────────────────────────────
hdr "Installing systemd user service"
mkdir -p "$HOME/.config/systemd/user"
cat > "$HOME/.config/systemd/user/SteamControllerGyroDSU.service" << EOF
[Unit]
Description=Steam Controller Gyro DSU Server
Documentation=https://github.com/TyanColte/Steam-Controller-GyroDSU
After=sockets.target
StartLimitIntervalSec=0

[Service]
Type=simple
Restart=always
RestartSec=2
ExecStart=$INSTALL_DIR/SteamControllerGyroDSU --port 26761

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable --now SteamControllerGyroDSU.service
ok "Service enabled and started"

# ── Icon installation ────────────────────────────────────────────────────────
hdr "Installing application icon"
ICON_512="$HOME/.local/share/icons/hicolor/512x512/apps"
mkdir -p "$ICON_512"

if [ -f "$SCRIPT_DIR/sc2gyrodsu.png" ]; then
    cp "$SCRIPT_DIR/sc2gyrodsu.png" "$ICON_512/sc2gyrodsu.png"
    ok "512x512 icon installed"

    # Install pre-rendered PNG fallbacks bundled in the release package.
    for SIZE in 16 32 48 128 256; do
        _PNG="$SCRIPT_DIR/sc2gyrodsu_${SIZE}.png"
        if [ -f "$_PNG" ]; then
            ICON_PNG="$HOME/.local/share/icons/hicolor/${SIZE}x${SIZE}/apps"
            mkdir -p "$ICON_PNG"
            cp "$_PNG" "$ICON_PNG/sc2gyrodsu.png"
            ok "${SIZE}x${SIZE} PNG icon installed"
        fi
    done

    # If no pre-sized PNGs were bundled, generate them now using Python or
    # ImageMagick (at least one should be present on any modern distro).
    if ! ls "$SCRIPT_DIR"/sc2gyrodsu_*.png >/dev/null 2>&1; then
        if command -v python3 >/dev/null 2>&1 && \
           python3 -c "from PIL import Image" 2>/dev/null; then
            python3 - "$SCRIPT_DIR/sc2gyrodsu.png" "$HOME/.local/share/icons/hicolor" << 'PYEOF'
import sys
from PIL import Image
src, icon_dir = sys.argv[1], sys.argv[2]
img = Image.open(src)
import os
for size in (16, 32, 48, 128, 256):
    d = f"{icon_dir}/{size}x{size}/apps"
    os.makedirs(d, exist_ok=True)
    img.resize((size, size), Image.LANCZOS).save(f"{d}/sc2gyrodsu.png")
PYEOF
            ok "Icon sizes generated via Python"
        elif command -v magick >/dev/null 2>&1; then
            for SIZE in 16 32 48 128 256; do
                ICON_PNG="$HOME/.local/share/icons/hicolor/${SIZE}x${SIZE}/apps"
                mkdir -p "$ICON_PNG"
                magick "$SCRIPT_DIR/sc2gyrodsu.png" \
                    -resize "${SIZE}x${SIZE}" \
                    "$ICON_PNG/sc2gyrodsu.png" 2>/dev/null \
                    && ok "${SIZE}x${SIZE} icon generated via ImageMagick"
            done
        fi
    fi

    # Refresh icon caches (best-effort — not all envs have these tools).
    gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
    xdg-icon-resource forceupdate 2>/dev/null || true
else
    warn "sc2gyrodsu.svg not found in package — icon may fall back to 'input-gamepad'"
fi

# ── Desktop shortcuts + app menu ────────────────────────────────────────────
hdr "Installing desktop shortcuts"
DESKTOP_DIR="$HOME/Desktop"
APP_DIR="$HOME/.local/share/applications"
mkdir -p "$DESKTOP_DIR" "$APP_DIR"

# Helper: write a .desktop file with @INSTALL_DIR@ substituted,
# then install it to both the Desktop and the app menu.
install_desktop() {
    _name="$1"    # e.g. "sc2gyrodsu-update"
    _content="$2" # here-doc content
    printf '%s' "$_content" \
        | sed "s|@INSTALL_DIR@|$INSTALL_DIR|g" \
        > "$DESKTOP_DIR/${_name}.desktop"
    chmod +x "$DESKTOP_DIR/${_name}.desktop"
    # Also install to XDG app menu (no chmod needed — menus don't exec directly)
    cp "$DESKTOP_DIR/${_name}.desktop" "$APP_DIR/${_name}.desktop"
}

install_desktop "sc2gyrodsu-update" "[Desktop Entry]
Version=1.1
Type=Application
Name=Update SteamControllerGyroDSU
GenericName=Gyro DSU Updater
Comment=Download and install the latest SteamControllerGyroDSU release
Exec=@INSTALL_DIR@/update.sh
Icon=system-software-update
Terminal=true
Categories=Game;System;
Keywords=steam;controller;gyro;dsu;update;
StartupNotify=false"

install_desktop "sc2gyrodsu-uninstall" "[Desktop Entry]
Version=1.1
Type=Application
Name=Uninstall SteamControllerGyroDSU
GenericName=Gyro DSU Uninstaller
Comment=Remove SteamControllerGyroDSU and its service
Exec=@INSTALL_DIR@/uninstall.sh
Icon=edit-delete
Terminal=true
Categories=Game;System;
Keywords=steam;controller;gyro;dsu;uninstall;
StartupNotify=false"

if [ -f "$INSTALL_DIR/sc2gyrodsu-config" ]; then
    install_desktop "sc2gyrodsu-config" "[Desktop Entry]
Version=1.1
Type=Application
Name=SteamControllerGyroDSU Config
GenericName=Gyro DSU Configuration
Comment=Configure axis mapping and calibrate gyro for Steam Controller 2
Exec=@INSTALL_DIR@/sc2gyrodsu-config
Icon=sc2gyrodsu
Terminal=false
Categories=Game;Settings;
Keywords=steam;controller;gyro;dsu;cemuhook;calibrate;
StartupNotify=true"
fi

# Refresh the app menu so shortcuts appear immediately (best-effort)
update-desktop-database "$APP_DIR" 2>/dev/null || true

ok "Desktop shortcuts and app-menu entries created"

# Remove the one-shot installer .desktop — it served its purpose and
# the Config/Update/Uninstall shortcuts replace it.
rm -f "$DESKTOP_DIR/SteamControllerGyroDSU.desktop"

# ── Post-install summary ─────────────────────────────────────────────────────
echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║    Installation complete!                     ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""
echo "  Service : running on  127.0.0.1:26761"
echo "  Emulator: point at    127.0.0.1:26761  (slot 0 = first controller)"
if systemctl --user is-active --quiet SteamControllerGyroDSU.service 2>/dev/null; then
    printf "  Status  : ${GREEN}Running${NC}\n"
else
    printf "  Status  : ${RED}Not running${NC} — check: systemctl --user status SteamControllerGyroDSU\n"
fi
echo ""

case "$PLATFORM" in
    steamdeck)
        echo "  ┌─ Steam Deck notes ──────────────────────────────────────────────┐"
        echo "  │ • Files in ~/SteamControllerGyroDSU/ survive SteamOS updates.  │"
        echo "  │ • After an OS update, run 'Update SteamControllerGyroDSU' from │"
        echo "  │   the Desktop to refresh the service and udev rules if needed. │"
        echo "  │ • Use Desktop Mode to play with gyro in emulators.             │"
        echo "  └────────────────────────────────────────────────────────────────┘"
        ;;
    bazzite)
        echo "  ┌─ Bazzite notes ────────────────────────────────────────────────┐"
        echo "  │ • The service runs as a user systemd service and persists       │"
        echo "  │   across Bazzite updates (user home is on a separate mount).   │"
        echo "  │ • Config file: ~/.config/sc2gyrodsu/config.ini                 │"
        if [ -f "$INSTALL_DIR/sc2gyrodsu-config" ]; then
        echo "  │ • Use the 'SteamControllerGyroDSU Config' desktop shortcut     │"
        echo "  │   to remap axes and run gyro calibration.                      │"
        fi
        echo "  └────────────────────────────────────────────────────────────────┘"
        ;;
esac

echo ""
read -n 1 -s -r -p "  Press any key to exit."
echo ""
