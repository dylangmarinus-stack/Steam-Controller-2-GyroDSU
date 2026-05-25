#!/usr/bin/env python3
"""Resize assets/icons/sc2gyrodsu.png to standard XDG icon sizes."""
import sys
from PIL import Image

src_path = sys.argv[1] if len(sys.argv) > 1 else "assets/icons/sc2gyrodsu.png"
out_dir  = sys.argv[2] if len(sys.argv) > 2 else "SteamControllerGyroDSUSetup"

src = Image.open(src_path)
for size in (16, 32, 48, 128, 256):
    src.resize((size, size), Image.LANCZOS).save(f"{out_dir}/sc2gyrodsu_{size}.png")
    print(f"  {size}x{size} → {out_dir}/sc2gyrodsu_{size}.png")
