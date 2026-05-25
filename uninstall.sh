#!/bin/sh
# SteamControllerGyroDSU uninstaller

INSTALL_DIR="$HOME/SteamControllerGyroDSU"
DESKTOP_DIR="$HOME/Desktop"

echo ""
echo "╔═══════════════════════════════════════════════╗"
echo "║    SteamControllerGyroDSU — Uninstaller       ║"
echo "╚═══════════════════════════════════════════════╝"
echo ""

echo "  Stopping and disabling service..."
systemctl --user -q stop    SteamControllerGyroDSU.service 2>/dev/null || true
systemctl --user -q disable SteamControllerGyroDSU.service 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/SteamControllerGyroDSU.service"
systemctl --user daemon-reload 2>/dev/null || true

echo "  Removing files..."
rm -f "$INSTALL_DIR/SteamControllerGyroDSU"
rm -f "$INSTALL_DIR/sc2gyrodsu-config"
rm -f "$INSTALL_DIR/update.sh"
rm -f "$INSTALL_DIR/uninstall.sh"
rm -f "$INSTALL_DIR/99-sc2gyrodsu.rules"
rmdir "$INSTALL_DIR" 2>/dev/null || true   # only removes if empty

echo "  Removing desktop shortcuts and app-menu entries..."
rm -f "$DESKTOP_DIR/sc2gyrodsu-config.desktop"
rm -f "$DESKTOP_DIR/sc2gyrodsu-update.desktop"
rm -f "$DESKTOP_DIR/sc2gyrodsu-uninstall.desktop"
rm -f "$HOME/.local/share/applications/sc2gyrodsu-config.desktop"
rm -f "$HOME/.local/share/applications/sc2gyrodsu-update.desktop"
rm -f "$HOME/.local/share/applications/sc2gyrodsu-uninstall.desktop"
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true

# Remove custom udev rules only if the installer put them there.
# (We never remove Steam's own rules.)
if [ -f "$INSTALL_DIR/.udev_rules_installed" ] || \
   [ -f /etc/udev/rules.d/99-sc2gyrodsu.rules ]; then
    echo "  Removing udev rules..."
    if sudo rm -f /etc/udev/rules.d/99-sc2gyrodsu.rules 2>/dev/null; then
        sudo udevadm control --reload-rules 2>/dev/null || true
        rm -f "$INSTALL_DIR/.udev_rules_installed"
        echo "  udev rules removed."
    else
        echo "  Note: could not remove /etc/udev/rules.d/99-sc2gyrodsu.rules"
        echo "        Remove manually with: sudo rm /etc/udev/rules.d/99-sc2gyrodsu.rules"
    fi
fi

echo ""
echo "  Uninstall complete."
echo "  Config file preserved at: ~/.config/sc2gyrodsu/config.ini"
echo "  Remove it manually if desired: rm -rf ~/.config/sc2gyrodsu/"
echo ""
read -n 1 -s -r -p "  Press any key to exit."
echo ""
