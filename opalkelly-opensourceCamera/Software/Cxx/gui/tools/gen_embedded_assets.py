#!/usr/bin/env python3
# Copyright (c) 2026 Opal Kelly Incorporated
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# Regenerates embedded_assets.cpp from the GUI assets (camera.xrc + the logo/icon images the app loads).
#
# Mirrors the RTL app's wxrc output: every resource is a byte array registered in a wxMemoryFSHandler,
# so okCameraApp is self-contained - its window AND branding load with no on-disk assets/ folder.
# Committed-generated (no build-time wxrc/python step). Run after changing any embedded asset:
#
#     python tools/gen_embedded_assets.py
import os, re

HERE   = os.path.dirname(os.path.abspath(__file__))
GUI    = os.path.dirname(HERE)
ASSETS = os.path.join(GUI, "assets")
OUT    = os.path.join(GUI, "embedded_assets.cpp")

# (virtual name the app loads, path under assets/, MIME). camera_axi.lua + bitfiles stay external.
items = [
    ("camera.xrc",         "camera.xrc",         "text/xml"),
    ("logo/logo.png",      "logo/logo.png",      "image/png"),
    ("logo/led.png",       "logo/led.png",       "image/png"),
    ("logo/opalkelly.svg", "logo/opalkelly.svg", "image/svg+xml"),
    ("okApp.ico",          "okApp.ico",          "image/x-icon"),
] + [(f"logo/busy{i}.png", f"logo/busy{i}.png", "image/png") for i in range(1, 40)]

ident = lambda name: "a_" + re.sub(r'[^0-9A-Za-z]', '_', name)

arrays, table = [], []
for vname, rel, mime in items:
    data = open(os.path.join(ASSETS, rel), "rb").read()
    var = ident(vname)
    rows = ["    " + ", ".join("0x%02x" % b for b in data[i:i+16]) + "," for i in range(0, len(data), 16)]
    arrays.append(f"static const unsigned char {var}[] = {{\n" + "\n".join(rows) + "\n}};".replace("}};", "};"))
    table.append(f'    {{ "{vname}", {var}, sizeof({var}), "{mime}" }},')

cpp = f'''// embedded_assets.cpp - GENERATED; do not edit by hand.
// Regenerate after changing any embedded asset:  python tools/gen_embedded_assets.py
//
// Embeds the GUI resources (camera.xrc + the logo/icon images) into the executable and registers them
// in an in-memory filesystem, mirroring the RTL app's wxrc output. okCameraApp is therefore
// self-contained: its window and branding load with no dependency on the on-disk assets/ folder.
// (camera_axi.lua and the FPGA bitfiles stay external by design - editable script + large per-board data.)
#include <wx/string.h>
#include <wx/filesys.h>
#include <wx/fs_mem.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <wx/bmpbndl.h>
#include <wx/icon.h>
#include <wx/xrc/xmlres.h>

{chr(10).join(arrays)}

namespace {{
struct EmbFile {{ const char* name; const unsigned char* data; size_t size; const char* mime; }};
const EmbFile k_files[] = {{
{chr(10).join(table)}
}};
const EmbFile* findEmb(const wxString& name) {{
    for (const EmbFile& f : k_files) if (name == f.name) return &f;
    return nullptr;
}}
}}  // namespace

// Register every embedded resource in the wx in-memory filesystem (mirrors the RTL app's InitXmlResource()).
static void initEmbeddedFs() {{
    static bool s_done = false;
    if (s_done) return;
    wxFileSystem::AddHandler(new wxMemoryFSHandler);
    for (const EmbFile& f : k_files)
        wxMemoryFSHandler::AddFileWithMimeType(f.name, f.data, f.size, f.mime);
    s_done = true;
}}

bool LoadEmbeddedCameraXrc() {{
    initEmbeddedFs();
    return wxXmlResource::Get()->Load("memory:camera.xrc");
}}

wxBitmap EmbeddedBitmap(const wxString& name) {{
    const EmbFile* f = findEmb(name);
    if (!f) return wxNullBitmap;
    wxMemoryInputStream s(f->data, f->size);
    wxImage img(s);
    return img.IsOk() ? wxBitmap(img) : wxNullBitmap;
}}

wxBitmapBundle EmbeddedSVG(const wxString& name, const wxSize& size) {{
    const EmbFile* f = findEmb(name);
    if (!f) return wxBitmapBundle();
    return wxBitmapBundle::FromSVG(f->data, f->size, size);
}}

wxIcon EmbeddedIcon(const wxString& name) {{
    wxIcon ic;
    wxBitmap bmp = EmbeddedBitmap(name);  // wxImage decodes .ico once the ICO handler is registered
    if (bmp.IsOk()) ic.CopyFromBitmap(bmp);
    return ic;
}}
'''
open(OUT, "w", newline="\n").write(cpp)
print(f"wrote {OUT}: {len(items)} files embedded ({os.path.getsize(OUT)} bytes)")
for vname, rel, _ in items[:5]:
    print(f"  {vname:22s} magic={open(os.path.join(ASSETS, rel),'rb').read(4).hex()}")
