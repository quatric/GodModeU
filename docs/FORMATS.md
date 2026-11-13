# Wii U formats in GodModeU

Press **A** on a file and the viewer is picked by what is in the file (its magic bytes), not by its name.
Everything [GodModeWii](https://github.com/quatric/GodModeWii) opens opens here too. This page lists the extra Wii U
formats (big endian; Switch files are not handled) and how each viewer works.

## Archives

These open like folders.

| Format | What you see |
| --- | --- |
| SARC (`.sarc`, `.pack`, `.szs`) | Its files. `.szs` is Yaz0-compressed SARC and is unpacked first. |
| BFSAR | Every internal file, named after its sound, bank, wave archive or group. A *Sequences* folder holds sequences that play with their bank and waves. An *External* folder holds the streams it names that sit in files next to it. |
| BFWAR | Its waves. |
| BARS | Each asset's FWAV / FSTP, named from its AMTA. |
| NUS3AUDIO | Its tracks. |

## Images

| Format | Notes |
| --- | --- |
| GTX | Every texture in the file. |
| BFLIM | Luminance and alpha formats (L8, A8, LA8, BC4, BC5) show as grey, alpha or grey + alpha, as the console reads them. |
| BFFNT | The glyph sheets. |
| BFRES | Its FTEX textures. A BFRES with no model opens here. |
| NUT | Smash Bros. for Wii U. |
| DDS | |
| TXTR | Retro Studios' Wii U format (Donkey Kong Country: Tropical Freeze). LZSS or zlib buffers. |

All GX2 tile modes work (linear, 1D, and 2D / 2B macro tiling with pipe and bank swizzle). So do the formats: R8 to
RGBA32F, RGB565, RGB5A1, RGBA4, RGB10A2, R11G11B10F, depth, and BC1 to BC5. BC5 normal maps get their Z rebuilt so
they look like normal maps.

Textures are only expanded when shown, so a BFRES with dozens of them opens quickly. **+** / **-** steps through them.

## Layouts (BFLYT)

Drawn in software: picture, text, window (1, 4 or 8 frames) and part panes, with each material's first texture,
black / white colours, texture SRT, vertex colours, transforms and passed-down alpha.

* Textures come from the archive's `timg/` folder next to `blyt/`. Parts come from the `.bflyt` next to it.
* Text uses the console font (the system fonts aren't in the archives).
* **D-Pad** or **Point + A drag** moves, **+/-** zooms, **A** outlines the panes, **1** shows hidden panes, **2** shows the
  pane list as text.
* The `.bflan` files in the archive's `anim/` folder that move this layout's panes play. **1** steps through them. They
  animate pane SRT, visibility, vertex colours and alpha, material colours, and the first texture's SRT and pattern.
  **A** then cycles outlines and hidden panes.
* TEV combiners aren't played.

## 3D models (BFRES, Wii U v3 / v4)

Every model is drawn in software with its materials' first textures, lit, unlit or as a wireframe. Shapes sit on their
bones (rigid, one-bone and blended multi-bone skinning), and the file's skeletal animations (FSKA) play at 30 frames a
second.

**D-Pad** or **Point + A drag** turns the model, **+/-** zooms, **A** changes the shading, **1** steps from the bind pose
through each animation to the next model, **2** shows its textures.

## Audio

BFSTM, BFWAV, BTSND (the boot sound), BFSEQ sequences from a BFSAR, and IDSP (NUS3AUDIO's DSP-ADPCM tracks). Opus and
BNSF tracks in a NUS3AUDIO are listed but not played.

## Text views

| Format | Shown as |
| --- | --- |
| BYAML / BYML (v1 to v4) | A YAML-style tree |
| XMB (Smash Bros.) | Elements and attributes |
| AAMP parameter files, BGLPBD light probes | Lists, objects and values |
| BFLYT | Textures, fonts, materials, and the pane tree with positions, sizes and texts |
| BFLAN | The tag, its groups, and what each pane / material animates |
| BFSEQ | Its labels, when it isn't inside a BFSAR |

## Disc images

WUD, WUX and wudump's `game_part1.wud` install as Wii U titles. See the README.
