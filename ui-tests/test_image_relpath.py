#!/usr/bin/env python3
"""Regression test for GNOME/dia#524: a .dia that references an image by a
RELATIVE path must export correctly even when `dia --export` is run from a
different working directory — the path is resolved against the .dia's folder,
not the cwd.

https://gitlab.gnome.org/GNOME/dia/-/issues/524

Run via meson test. Env: DIA_BIN = the built dia binary.
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib


def make_png(width=8, height=8):
    """Build a valid solid-colour RGB PNG without external libraries."""
    def chunk(typ, data):
        body = typ + data
        return (struct.pack(">I", len(data)) + body
                + struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    row = b"\x00" + b"\x40\x80\xc0" * width        # filter byte + RGB pixels
    idat = chunk(b"IDAT", zlib.compress(row * height))
    return sig + ihdr + idat + chunk(b"IEND", b"")


PNG_BYTES = make_png()

# A diagram with one Standard - Image object referencing "pic.png" by name.
DIA_XML = """<?xml version="1.0" encoding="UTF-8"?>
<dia:diagram xmlns:dia="http://www.lysator.liu.se/~alla/dia/">
  <dia:layer name="Background" visible="true">
    <dia:object type="Standard - Image" version="0" id="O0">
      <dia:attribute name="obj_pos"><dia:point val="1,1"/></dia:attribute>
      <dia:attribute name="elem_corner"><dia:point val="1,1"/></dia:attribute>
      <dia:attribute name="elem_width"><dia:real val="3"/></dia:attribute>
      <dia:attribute name="elem_height"><dia:real val="3"/></dia:attribute>
      <dia:attribute name="file"><dia:string>#pic.png#</dia:string></dia:attribute>
    </dia:object>
  </dia:layer>
</dia:diagram>
"""


def main():
    dia = os.environ.get("DIA_BIN")
    if not dia:
        print("DIA_BIN must point at the dia binary")
        return 2

    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "pic.png"), "wb") as fh:
            fh.write(PNG_BYTES)
        with open(os.path.join(d, "img.dia"), "w") as fh:
            fh.write(DIA_XML)
        out = os.path.join(d, "out.png")

        # Run from a DIFFERENT cwd ("/") so "pic.png" only resolves if the
        # loader uses the .dia's directory.
        res = subprocess.run([dia, "--export", out, os.path.join(d, "img.dia")],
                             cwd="/", capture_output=True, text=True)
        if res.returncode != 0:
            print("FAIL: export exited %d\n%s" % (res.returncode, res.stderr))
            return 1
        if "not found" in (res.stderr or "").lower():
            print("FAIL: relative image path not resolved:\n%s" % res.stderr)
            return 1
        if not os.path.exists(out) or os.path.getsize(out) == 0:
            print("FAIL: no output produced")
            return 1

    print("image relative-path export OK (#524)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
