# GodModeU

**By:** quatric & cmyksoda

A text-mode, [GodMode9](https://github.com/d0k3/GodMode9)-style file manager for the **Wii U**, written as a native
Cafe OS homebrew app. It's a port of [GodModeWii](https://github.com/quatric/GodModeWii), which targets the Wii (and
the Wii U's vWii mode). No fancy GUI: a text grid on the TV, a cursor and a lot of power. Copy, cut, paste and delete
files between the drives the Wii U can mount, watch the free space of each one, and (only if you really mean it) change
the protected storage.

> ## ⚠️ BRICK RISK
>
> **GodModeU can permanently brick your console.**
> Writing, deleting or renaming files on the Wii U's internal storage (MLC), its system NAND (SLC) or the vWii NAND can leave the
> console unbootable.
>
> * Everything on protected storage is **locked by default**. Copying *from* it is always safe.
> * The first time you try to change a protected drive (delete, paste, cut, rename, new folder, ...) a warning appears and
>   you must enter the **Konami code**. Each drive (MLC, vWii NAND, Wii U NAND) asks for its own code, and stays unlocked
>   until you exit or lock it again from the SYSTEM menu.
> * **Make a full NAND / MLC backup first** and keep a copy off the console.
> * This software comes with **no warranty**. You are solely responsible for whatever you do with it.
>
> If you aren't sure what a file does, leave it alone.

## Status

Version 0.4.0 was tested on a real Wii U with Aroma: the drives below (SD, a WFS USB drive, FAT32, NTFS and ext4 USB drives,
MLC and both NANDs), copying, the GamePad, a Wii Remote, the FTP server, Export Miis and Save Wii U keys. This version adds
GodModeWii's code, libwiiuinput, the Wii U format viewers and title installing on top; the format readers were checked on a PC
against nintoolbox's Wii U test files, the rest only against the wut headers and the tools it follows. What hasn't been tried on
a console yet is listed under [Not yet tested](#not-yet-tested). Reports from real hardware are very welcome.

USB drives that aren't Wii U-formatted, both NANDs and parts of the internal storage are reached through **Aroma's Mocha**,
so they need Aroma.

## What's new in this version

GodModeU now runs on the same code as [GodModeWii](https://github.com/quatric/GodModeWii), so everything added there lately is
here too: the viewers and players, the extra archive and disk formats, the editors, the HOME menu tools and the themes. On top
of that it has things only a Wii U can do:

* **Input through [libwiiuinput](https://github.com/quatric/libwiiuinput)** instead of hard-coded GamePad / Wii Remote code:
  the GamePad (buttons and touch screen), Wii Remotes with a Nunchuk or Classic Controller, Wii U Pro Controllers, the GameCube
  adapter, USB game controllers, USB keyboards and mice, pen tablets and microphones, all hot-pluggable.
* **Wii U file formats**: see [What it can open](#what-it-can-open).
* **Installing Wii U titles** from WUP folders and **WUD / WUX disc images**: see [Installing Wii U titles](#installing-wii-u-titles).
* **NAND / MLC backup instructions** in the HOME menu: see [NAND backup](#nand-backup).

## Drives

| Drive | Path | Notes |
| --- | --- | --- |
| SD card | `sd:/` | read / write. Mounted with `WHBMountSdCard` |
| USB drives | `usb1:/` ... `usb8:/` | Wii U-formatted (WFS) drives, which Cafe OS mounts, and FAT32 / exFAT / NTFS drives, which GodModeU mounts itself (read / write; the 4 GiB file limit applies to FAT32 only), plus ext2 / ext3 / ext4 drives, read-only. An NTFS drive Windows didn't shut down fully is left alone. Further FAT32 / exFAT / NTFS partitions take the next free `usbN:` entry |
| Wii U storage (MLC) | `mlc:/` | the console's internal storage. Protected: Konami code to change anything |
| vWii NAND (SLCCMPT) | `nand:/` | the vWii's NAND: System Menu, IOS, Wii channels and saves. Protected. Mounted through Aroma's Mocha |
| Wii U NAND (SLC) | `slc:/` | the Wii U's own system NAND. Protected: Konami code, plus a red warning the first time you change it in a session, because a mistake there can stop the Wii U from booting at all |

Each protected drive asks for its own Konami code. Remount drives from the HOME menu after inserting or removing one.

## What it can open

Press **A** on a file and the viewer is picked by **what is in the file** (its magic bytes, after nintoolbox's detection), not by
its name. Everything GodModeWii opens opens here as well (see its README for the full lists): GameCube / Wii archives, disc
images, disks and partitions (ext2/3/4, XFS, HFS+, APFS, ZFS, MBR / GPT), zip / 7z / RAR, PDF and EPUB, WADs, `content.bin`,
`data.bin`, VFF, BootMii `nand.bin`, WFS images, and the image, audio, MIDI, video and text viewers.

**Wii U formats** (big endian, Cafe NintendoWare and first-party; Switch files are not handled):

| Kind | Formats |
| --- | --- |
| Archives (open like folders) | **SARC** (`.sarc`, `.pack`, and `.szs` once its Yaz0 is undone), **BFSAR** (every internal file, named after its sound, bank, wave archive or group, and a *Sequences* folder whose sequences play with their bank and waves, like a BRSAR's), **BFWAR** (its waves), **BARS** (each asset's FWAV / FSTP, named from its AMTA), **NUS3AUDIO** |
| Images | **GTX** (every texture), **BFLIM**, **BFFNT** (glyph sheets), **BFRES** (its FTEX textures), **NUT** (Smash Bros. for Wii U), **DDS**, and Retro Studios' Wii U **TXTR** (Donkey Kong Country: Tropical Freeze; LZSS or zlib buffers). BFLIM's luminance and alpha formats (L8, A8, LA8, BC4, BC5) show as grey, alpha or grey + alpha, as the console reads them. Every GX2 tile mode (linear, 1D and 2D / 2B macro tiling with pipe and bank swizzle) and format: R8 to RGBA32F, RGB565, RGB5A1, RGBA4, RGB10A2, R11G11B10F, depth, and BC1 - BC5 (BC5 normal maps get their Z rebuilt so they look like normal maps) |
| Layouts | **BFLYT** drawn in software: picture, text, window (1, 4 or 8 frames) and part panes, with their materials' first texture, black / white colours, texture SRT, vertex colours, transforms and passed-down alpha. Textures come from the archive's `timg/` beside `blyt/`, parts from the `.bflyt` beside it. Text uses the console font (the system fonts aren't in the archives). D-Pad or Point + A drag moves, +/- zoom, A outlines the panes, 1 shows hidden panes, 2 the pane list as text. Animations (BFLAN) and TEV combiners aren't played |
| 3D models | **BFRES** (Wii U, v3 / v4): every model, drawn in software with its materials' first textures, lit, unlit or as a wireframe; shapes on their bones (rigid and one-bone skinning). D-Pad or Point + A drag turns it, +/- zoom, A changes the shading, 1 the next model, 2 shows its textures. A BFRES with no model opens in the image viewer |
| Audio | **BFSTM**, **BFWAV**, **BTSND** (the boot sound), as before; **BFSEQ** sequences from a BFSAR (above); **IDSP** (NUS3AUDIO's DSP-ADPCM tracks; Opus and BNSF tracks are listed but not played) |
| Text | **BYAML / BYML** (v1 - v4, as a YAML-style tree), **XMB** (Smash Bros., as elements and attributes), **AAMP** parameter files and **BGLPBD** light probes (lists, objects and values), **BFLYT** (textures, fonts, materials, the pane tree with positions, sizes and texts), **BFLAN** (the tag, its groups, and what each pane / material animates), **BFSEQ** (its labels, when it isn't in a BFSAR) |
| Disc images | **WUD**, **WUX** and wudump's `game_part1.wud`: install (below) |

Textures are only expanded when shown, so a BFRES with dozens of them opens quickly; **+** / **-** steps through them as in the
GameCube / Wii image viewer.

## Installing Wii U titles

**WUP folders** (`title.tmd`, `title.tik`, `title.cert`, the `.app` contents and their `.h3` hashes) install through the system's
own installer (MCP), the way [WUP Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) does it:

1. Copy the folder to the SD card, for example `sd:/install/<name>/`.
2. Press **A** on its `title.tmd`, or put the cursor on the folder (or mark several) and pick *Install Wii U title (WUP)*.
3. Choose the internal storage or a USB drive. A progress bar shows the bytes written.

Games, updates, DLC and demos can be installed; system titles are refused. Titles not signed by Nintendo for this console
(eShop dumps, DLC, homebrew) need **signature patches** (Aroma's sigpatches module, as WUP Installer GX2 needs). The common
errors are explained when they happen: no USB drive, not enough space, a bad or foreign ticket, an SD card problem (format it
FAT32 with 32 KB clusters).

**WUD / WUX disc images**: press **A** on a `.wud`, a `.wux` or wudump's `game_part1.wud`. GodModeU reads the disc's partition
table, the SI partition's ticket, TMD and certificates and the game (GM) partition, and writes it out as a WUP folder in
`sd:/install/<disc name>/` the way FIX94's [wud2app](https://github.com/FIX94/wud2app) does; then it installs that folder
(and offers to delete it). *Show the partitions* lists them. It needs:

* the **disc key**: wudump's `game.key` next to the image, or `<image name>.key`;
* the **Wii U common key**: `common.key` (16 bytes, or 32 hex digits), or the `otp.bin` from a NAND backup, in
  `sd:/wiiu/apps/GodModeU/`, at the root of the SD card or next to the image. Each one found is tried until the game's file
  table decrypts, so a wrong key is reported, never used;
* room on the SD card for the whole game (no file of a game is over 4 GB, so FAT32 is fine).

Hold **B** to cancel the conversion. The HOME menu's *Install Wii U titles (WUP / WUD help)* has the same instructions on the
console.

## NAND backup

*Back up the NAND / MLC (how to)* in the HOME menu explains how to make a full backup with
[nanddumper](https://github.com/GaryOderNichts/nanddumper) under Aroma (as on [wiiu.hacks.guide](https://wiiu.hacks.guide/)):
put it in `sd:/wiiu/apps/`, start it, set SLC, SLCCMPT, OTP and SEEPROM to *Yes* (MLC too if the card has room: 8 GB or 32 GB
more), press **A**, then copy `slc.bin`, `slccmpt.bin`, `otp.bin`, `seeprom.bin` and `mlc.bin.part*` off the card and keep them
safe. Cafe OS doesn't let an app read the NAND chips raw, so GodModeU itself only copies files. Its `otp.bin` is also what
GodModeU needs for WUD images.

## Other features

* **Typing names** (rename, new folder, new file, editing a line) uses the Wii U's own software keyboard on the GamePad,
  the one games and the Wii U Menu use (nn::swkbd); if it can't start, GodModeWii's character roller is used instead.
* One text interface for every drive, with free space and a usage bar, and the GodModeWii browser: copy, cut, paste and delete
  with progress and **hold B to cancel**, multi-select, rename, new folder, new file, info, disk usage, text / hex viewers and editors.
* **Forced risky mode**: enter the Konami code on the logo screen at startup to switch "dangerous permissions always on" on or
  off (saved as `sd:/wiiu/apps/GodModeU/force.cfg`).
* **HOME menu**: lock the protected drives again, remount, FTP server, FTP install server, Wiiload (homebrew `.rpx` / `.wuhb`
  pushed over the network is saved to `sd:/wiiu/apps/`), export all vWii saves, Miis, Wii U Mii export, Save Wii U keys, sketchpad, voice memos, sound effects,
  alarm clock, calculator, clock and calendar, themes, the NAND backup and title install help, credits, exit to the Homebrew
  Launcher / Wii U Menu, restart, power off.
* **Export Wii U Miis** (HOME menu): pick Miis by name and save them as `<Name>.ffsd` in `/savemiis/` on the SD card or a
  USB drive (the format Mii Studio loaders and PC Mii tools read). The Mii database is only read.
* **Save Wii U keys for GodModeWii** (HOME menu): writes this console's two WFS keys to `sd:/wfs/`, so GodModeWii can read
  the Wii U drive from vWii. They're unique to your console: keep them private.
* **Aroma's plugin menu** opens over the app with its usual combo (L + Down + Minus).
* **FTP server** and **FTP install server**: see [FTP server](#ftp-server).

### Controls

| Button | File browser |
| --- | --- |
| D-Pad Up / Down | Move (hold to scroll) |
| D-Pad Left / Right | Page up / down |
| **A** | Open folder, archive or file / paste on the green `Paste` row |
| **B** | Go up one folder (back to the drive list at the top) |
| **Y** (Wii Remote **1**) | Mark / unmark the item and move down / clear the clipboard on the `Paste` row |
| **X** (Wii Remote **2**) | Actions menu |
| **+** | Copy the marked items (or the item under the cursor) |
| **-** | Cut the marked items |
| **HOME** | HOME menu (HOME or B closes it) |
| Touch screen | Point and tap (a tap is **A**) |

Every controller libwiiuinput reads works; the buttons are folded into the Wii Remote's.

## What was left out compared to GodModeWii

These depend on the Wii's IOS, the Wii's hardware or GX, and have no equivalent in a native Cafe OS app:

* the GX 3D model viewer for the Wii's formats (BRRES, BMD, ...; Wii U BFRES models have their own software viewer), the drawn BRLYT layout viewer and the Mii faces (they draw with GX; Wii BRLYT layouts fall back to their text view, Wii U BFLYT layouts have their own software viewer)
* WAD installing, wad2bin, YABDM-style WAD backups and Wii Menu themes (they need IOS and the Wii's keys)
* the Wii NAND through IOS (ISFS), raw NAND dumping (nanddumper@IOS), the SEEPROM / OTP readers and the `keys:/` drive
* the Wii disc drive, TinyLoad, SD Gecko slots, GameCube memory cards and the disc slot light
* the runtime IOS patch and the Dolphin / vWii detection

## FTP server

SYSTEM → *FTP server (read-only)* or *(read/write)* opens a status screen with the address, the user name (`godmode`) and a
fresh random password. Connect with any FTP client; every drive shows up as a folder (`/sd`, `/usb1`, `/mlc`, `/nand`, `/slc`, ...).

* It listens on port 21, or on **2121** when Aroma's ftpiiu already answers on 21.
* **Login is required** by default (a random password per run); the status screen can switch to an open login for a trusted LAN.
  Failed logins are throttled and a burst of them locks the server for a minute.
* **LAN only**: peers outside private / link-local ranges are refused.
* **No FTP bounce / hijack**: `PORT` must point back at the client (port >= 1024) and passive data connections must come from the client's IP.
* **Sandboxed paths**: every path is canonicalised inside the virtual root.
* **Read-only by default.** Changes to MLC / NAND need the Konami code on the console (and the Wii U NAND's warning).
* **Safe uploads**: files go to a temporary name and only replace the target when complete.
* `RMD` only removes empty folders; nothing is deleted recursively over the network.

The **FTP install server** unpacks uploaded `.zip`, `.7z` and `.rar` files onto the SD card or the first USB drive.

## Not yet tested

These could not be checked without a console. Each is a constant or a short function:

* exFAT USB drives, more than one non-WFS USB drive at once, and the WFS image reader on a dump of a real Wii U drive.
* **Screen**: a 640x480 framebuffer (GodModeWii's) drawn 1.5x on the TV and 1:1 on the GamePad through OSScreen (`source/wiiu/screen.c`).
* **Sound**: GodModeWii's ASND voices mixed in software onto two AX voices (`source/wiiu/asnd.c`).
* **Installing**: the MCP install path `/vol/app_sd/...` for the SD card, as WUP Installer GX2 uses it (`source/wiiu_install.c`).
* **Wii U texture tiling**: checked against nintoolbox's BFRES and GTX samples on a PC; BFFNT sheets after the first use a
  guessed swizzle, and NUT files from the Wii U use tile mode 4.

## Installation

* **Aroma**: copy `GodModeU.wuhb` to `sd:/wiiu/apps/`. Aroma is needed for USB drives the Wii U didn't format and for both
  NANDs (see [Status](#status)).
* **Homebrew Launcher**: copy the `GodModeU` folder from the release zip (`GodModeU.rpx`, `meta.xml`, `icon.png`)
  to `sd:/wiiu/apps/GodModeU/`.

Settings, themes, drawings and memos live in `sd:/wiiu/apps/GodModeU/`.

## Building

You need [devkitPro](https://devkitpro.org/) with devkitPPC, wut and these portlibs:

```sh
dkp-pacman -S wut wut-tools ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-libvorbisidec ppc-libogg ppc-bzip2 ppc-zlib
git submodule update --init --recursive
./scripts/build_libmocha.sh   # once each: libmocha, libdvm (FAT32 / exFAT), libntfs and lwext4 (ext2/3/4) into ./portlibs
./scripts/build_libdvm.sh
./scripts/build_libntfs.sh
./scripts/build_libext4.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make
```

`make` builds the `libwiiuinput` submodule first, then `GodModeU.rpx` and `GodModeU.wuhb`. The CI builds every push
(download the `GodModeU` artifact), and pushing a tag like `v0.5.0` publishes a GitHub Release with the zip.

## Credits

GodModeU is built on a lot of other people's work. Thank you!

* **[GodModeWii](https://github.com/quatric/GodModeWii)** (quatric & cmyksoda): the code this port is made of, and its credits.
* **[GodMode9](https://github.com/d0k3/GodMode9)** by d0k3: the look, feel and workflow this tool is modelled on.
* **[nintoolbox](https://github.com/quatric/nintoolbox)** (quatric): the file type detection and the Wii U format readers
  (GX2 tiling after aboood40091's addrlib.py, BFRES / GTX / BFLIM / BFFNT, SARC, BFSAR, BARS, NUS3AUDIO, BYML, XMB, AAMP, BFLYT).
* **[libwiiuinput](https://github.com/quatric/libwiiuinput)** (quatric): every controller.
* **[WUP Installer GX2](https://github.com/xhp-creations/WUP-Installer-GX2)** (Dimok, Crediar, xhp-creations, and the [WUHB fork](https://github.com/Fangal-Airbag/wup-installer-gx2) by Fangal-Airbag) and [WUP Installer Y Mod](https://github.com/Yardape8000/wupinstaller) (Yardape8000): the MCP install sequence and its error messages.
* **[wud2app](https://github.com/FIX94/wud2app)** by FIX94 (MIT): the WUD layout and how it becomes a WUP folder.
* **[nanddumper](https://github.com/GaryOderNichts/nanddumper)** by GaryOderNichts and **[wiiu.hacks.guide](https://wiiu.hacks.guide/)**: the NAND backup instructions.
* **[ftpii](https://code.google.com/p/ftpii/source/list)** by Joseph Jordan and contributors: the FTP idea.
* **[WiiXplorer](https://github.com/niuus/wiixplorer)** by Dimok and contributors: the file-manager reference.
* **wfslib** and **wfs-tools** by koolkdev: the WFS layout.
* **[MoonShell](https://www.gamebrew.org/wiki/Moonshell)** 2.10 by Moonlight (GPL-2.0 or later): the interface sound effects (`res/sfx.bin`).
* **TiVo**'s interface sounds: an easter egg (`res/tivo.bin`, made by `scripts/make_tivo.py`); the last credits page has a hint, and the code works on the drive list too.
* **[libmocha](https://github.com/wiiu-env/libmocha)** by Maschell (LGPL-3.0): reaches the USB drive, internal storage and both NANDs through Aroma's Mocha.
* **[lwext4](https://github.com/gkostka/lwext4)** by Grzegorz Kostka (GPL-2.0 / BSD; Extrems' big-endian fork) with Extrems' libdvm
  ext4 driver (ZPL-2.1), ported and made read-only by `scripts/build_libext4.sh` and `scripts/ext4_driver.patch`.
* **[libntfs](https://github.com/extremscorner/libntfs)** by Extrems and contributors, on **NTFS-3G** by Tuxera and the NTFS-3G authors (GPL-2.0): NTFS USB drives.
* **[libdvm](https://github.com/devkitPro/libdvm)** by fincs / devkitPro (ZPL-2.1) with **[FatFs](https://github.com/devkitPro/fatfs-mod)** by ChaN: FAT32 and exFAT USB drives.
* **[wut](https://github.com/devkitPro/wut)** and **devkitPPC** (devkitPro): the toolchain and the Cafe OS libraries.
* **[unarr](https://github.com/selmf/unarr)** by Jan Ruhe (selmf) and contributors (LGPL-3.0, in `source/unarr`, with the LZMA SDK by Igor Pavlov inside).
