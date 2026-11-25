"""Packs the built plug-in into the installer payload (payload.zip).

usage: python make_payload.py <build dir> <output zip> <repo root>
"""
import os
import sys
import zipfile

build, out, root = sys.argv[1], sys.argv[2], sys.argv[3]
release = os.path.join(build, "ModernEQ_artefacts", "Release")
bundle = os.path.join(release, "VST3", "GKEQ.vst3")
app = os.path.join(release, "Standalone", "GKEQ.exe")

for path in (bundle, app):
    if not os.path.exists(path):
        sys.exit("missing build output: " + path)

notices = """GKEQ - third-party notices
================================

GKEQ is built with JUCE (https://juce.com), used under the terms of the
GNU General Public License v3 or a commercial JUCE licence.

It contains PFFFT (https://github.com/marton78/pffft), distributed under the
following licence:

"""
with open(os.path.join(root, "external", "pffft", "LICENSE.txt"), encoding="utf-8", errors="replace") as f:
    notices += f.read()

os.makedirs(os.path.dirname(out), exist_ok=True)
count = 0
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for folder, _, files in os.walk(bundle):
        for name in sorted(files):
            if ".old" in name:          # binaries moved aside while a host had them loaded
                continue
            full = os.path.join(folder, name)
            rel = os.path.relpath(full, os.path.dirname(bundle)).replace("\\", "/")
            z.write(full, "vst3/" + rel)
            count += 1
    z.write(app, "app/GKEQ.exe")
    z.writestr("app/Third-Party Notices.txt", notices.replace("\n", "\r\n"))
    count += 2

print("payload: %d files, %.1f MB -> %s" % (count, os.path.getsize(out) / 1e6, out))
