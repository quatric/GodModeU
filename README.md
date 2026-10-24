# GodModeU

A text-mode, [GodMode9](https://github.com/d0k3/GodMode9)-style file manager for the **Wii U**, written as a native
Cafe OS homebrew app. It's a port of [GodModeWii](https://github.com/quatric/GodModeWii), which targets the Wii (and
the Wii U's vWii mode). No fancy GUI: a text grid on the TV, a cursor and a lot of power. Copy, cut, paste and delete
files between the drives the Wii U can mount, watch the free space of each one, and (only if you really mean it) change
the protected storage.

> ## ⚠️ BRICK RISK
>
> **GodModeU can permanently brick your console.**
> Writing, deleting or renaming files on the Wii U's internal storage (MLC) or on the vWii NAND (SLC) can leave the
> console unbootable.
>
> * Everything on protected storage is **locked by default**. Copying *from* it is always safe.
> * The first time you try to change protected storage (delete, paste, cut, rename, new folder, ...) a warning appears and
>   you must enter the **Konami code**. It stays unlocked until you exit or lock it again from the SYSTEM menu.
> * **Make a full NAND / MLC backup first** and keep a copy off the console.
> * This software comes with **no warranty**. You are solely responsible for whatever you do with it.
>
> If you aren't sure what a file does, leave it alone.

## Status

**Not yet tested on a real Wii U or in an emulator.** The project builds with devkitPPC and
[wut](https://github.com/devkitPro/wut) (wut 1.9.1, the version in devkitPro's package index), but every part that talks to
the console has only been checked by reading the wut headers. Expect fixes. The points most likely to need them are
listed under [Unverified assumptions](#unverified-assumptions). Reports from real hardware are very welcome.

## Drives

| Drive | Path | Notes |
| --- | --- | --- |
| SD card | `sd:/` | read / write. Mounted with `WHBMountSdCard` |
| USB drives | `usb1:/` ... `usb8:/` | every volume Cafe OS mounts (`/vol/external01` ... `external08`). FAT, exFAT and WFS are handled by the system |
| Wii U storage (MLC) | `mlc:/` | the console's internal storage. Protected: Konami code to change anything |
| vWii NAND (SLC) | `nand:/` | the vWii NAND. Protected, and only visible if the app is allowed to mount it (Aroma / Mocha) |

Remount drives from the SYSTEM menu (HOME) after inserting or removing a drive.

## Features

* One text interface for every drive, with free space and a usage bar.
* **Open archives like folders** (read-only, copy out only): press **A** on a
  U8 archive (`.arc`, `.app`, `.bnr`, `.carc` ... also with an IMET or IMD5 header), an **LZ10 / LZ11** file, a **WAD**, a
  **`content.bin`**, a **`data.bin`** or a **VFF** (`.vff`). Archives nest (`sd:/x.wad|/00000001.app|/meta/banner.bin`),
  **B** goes back out, copy anything out with **+** / the actions menu. A **WFS image** file (a dump of a Wii U USB drive
  or the MLC) opens the same way; its keys are read from `sd:/wfs/` (see below).
* Copy, cut, paste and delete between any two writable drives, folders included, with a progress screen,
  transfer speed, overwrite / skip prompts and **hold B to cancel**. Half-written files are removed again.
* Multi-select, select all / invert, rename, new folder, folder size / info, a text viewer and a hex viewer.
* **Read-only FAT files and folders can be deleted.** Copy / move / delete summaries keep the result line (files, size, time)
  and let you page through every problem with **L / R** (**-** / **+**).
* **Forced risky mode**: enter the Konami code on the logo screen at startup to switch "dangerous permissions always on" on or
  off (saved as `sd:/wiiu/apps/GodModeU/force.cfg`). While it is on, nothing asks for the warning or code.
* Konami-code gate (asked once, when you first try to change protected storage) and brick warnings.
* A built-in **FTP server** (SYSTEM → *FTP server*): LAN only, login with a random password per run, sandboxed paths,
  read-only unless you pick read / write, and protected storage still needs the Konami code on the console.
  Plain FTP, no TLS, so only use it on a network you trust.
* **FTP install server** (SYSTEM → *FTP install server (archives to SD / USB)*): upload a **`.zip`, `.7z` or `.rar`**
  (RAR 1.5 - 5.x, single volume, no password) to it and it is unpacked onto the SD card or the first USB drive right away,
  then the archive is deleted. The files go where they belong: an archive that already holds the drive layout
  (`wiiu/apps/...`, `apps/...`, `wad/...`) is unpacked at the root of the drive, a wrapper folder around it is dropped,
  a single program folder (`Foo/Foo.rpx`) goes to `wiiu/apps/Foo`, and loose files get a folder named after the archive
  (`.wad` files go to `wad/`, Aroma `.wps` plugins to `wiiu/environments/aroma/plugins/`). Docs (`.txt`, `.md`...) at the
  top level of a ready-made layout are skipped. A failed or cancelled install keeps the archive; hold **B** to cancel.
  Large 7z and RAR archives need RAM for their dictionary, so an archive can be refused on low memory.

### Controls

| Button | File browser |
| --- | --- |
| D-Pad Up / Down | Move (hold to scroll) |
| D-Pad Left / Right | Page up / down |
| **A** | Open folder or archive / actions menu on any other file / paste on the green `Paste` row |
| **B** | Go up one folder (back to the drive list at the top) |
| **Y** | Mark / unmark the item and move down / clear the clipboard on the green `Paste` row |
| **X** | Actions menu (paste, clear clipboard, copy, cut, delete, rename, new folder, info, text viewer, hex viewer, selection tools) |
| **+** | Copy the marked items (or the item under the cursor). **R** and **ZR** do the same |
| **-** | Cut the marked items. **L** and **ZL** do the same |
| **HOME** | SYSTEM menu: lock risky options again, remount drives, FTP server, credits, exit. **Press HOME again to leave** |

Copy or cut something, browse to the destination and press **A** on the green `Paste` row at the top of the list
(or use *Paste* in the actions menu). A copy stays in the clipboard so you can paste it in several places. To empty the
clipboard without pasting, press **Y** (**1** on a Wii Remote) on the `Paste` row or pick *Clear clipboard* in the actions menu.

The GamePad is the primary input. A Wii Remote with a Classic Controller works as well, and so does a Wii Remote on
its own (D-Pad, A, B, 1, 2, +, -, HOME). The Classic Controller mapping follows GlowWii's.

## What was left out compared to GodModeWii

These depend on the Wii's IOS, the Wii's hardware or the vWii, and have no equivalent in a native Cafe OS app:

* the Wii NAND through IOS (ISFS) and its permission patch, and the Homebrew Channel / BootMii key tricks
* the SEEPROM and OTP readers, and the `keys:/` console keys drive
* the Wii disc drive (`disc:/`) and raw Wii disc images
* SD Gecko slots and GameCube memory cards
* the runtime IOS patch and the Dolphin / vWii detection
* the Wii-only controller libraries (libwiidrc, libwupc, libsicksaxis). The GamePad and Wii Remotes are read through Cafe OS
  instead. Wii U Pro Controller, PS3 pads and USB keyboards aren't supported yet
* the `content.bin` and `data.bin` decryption that needs this console's own keys. WAD contents still decrypt with the
  common keys.

## FTP server

SYSTEM → *FTP server (read-only)* or *(read/write)* opens a status screen with the address, the user name (`godmode`) and a
fresh random password. Connect with any FTP client; every drive shows up as a folder (`/sd`, `/usb1`, `/mlc`, `/nand`, ...).

* **Login is always required**; the password is random per run. Failed logins are throttled and a burst of them locks the server for a minute.
* **LAN only**: peers outside private / link-local ranges are refused.
* **No FTP bounce / hijack**: `PORT` must point back at the client (port >= 1024) and passive data connections must come from the client's IP.
* **Sandboxed paths**: every path is canonicalised inside the virtual root.
* **Read-only by default.** Changes to MLC / NAND need the Konami code on the console.
* **Safe uploads**: files go to a temporary name and only replace the target when complete.
* `RMD` only removes empty folders; nothing is deleted recursively over the network.

Supported commands: `LIST`, `NLST`, `MLSD`, `RETR` (with `REST`), `STOR`, `DELE`, `MKD`, `RMD`, `RNFR` / `RNTO`, `SIZE`,
`PASV`, `EPSV`, `PORT`.

## Unverified assumptions

These are the things I could not check without a console or an emulator. Each one is a single constant or a short function,
so they're easy to change once they've been tried:

* **Mount points**: `/vol/external01`..`08` for USB, `/vol/storage_mlc01` for MLC, `/vol/slc01` for the vWii NAND. The SD
  card uses whatever `WHBGetSdCardMountPath()` returns (`source/vfs.c`).
* **Colours**: OSScreen text is white only, so colours only tint backgrounds (highlight bars). Text colours are ignored.
* **Networking**: the IP comes from `ACGetAssignedAddress`, and the FTP server runs on the BSD sockets from wut's `nsysnet`.
* **Archive readers** are the same code as GodModeWii and were only checked against generated samples on a PC.
  The WFS reader was never tested against a real Wii U drive.

## Installation

Build the app (below), then copy the result to the SD card:

* **Aroma** (or any launcher that runs `.wuhb` files): copy `GodModeU.wuhb` to `sd:/wiiu/apps/`.
* **Homebrew Launcher**: copy the `GodModeU` folder from the release zip (`GodModeU.rpx`, `meta.xml`, `icon.png`)
  to `sd:/wiiu/apps/GodModeU/`.

## Building

You need [devkitPro](https://devkitpro.org/) with devkitPPC and the wut toolchain:

```sh
dkp-pacman -S wut wut-tools
export DEVKITPRO=/opt/devkitpro
make
```

This produces `GodModeU.rpx` (the executable) and `GodModeU.wuhb` (the package for Aroma). The CI builds every push
(download the `GodModeU` artifact), and pushing a tag like `v0.1.0` publishes a GitHub Release with the zip.

## Credits

GodModeU is built on a lot of other people's work. Thank you!

* **[GodMode9](https://github.com/d0k3/GodMode9)** by d0k3: the look, feel and workflow this tool is modelled on.
* **[GodMode9i](https://github.com/DS-Homebrew/GodMode9i)** by the DS-Homebrew team: the DSi / DS take on the same idea.
* **[ftpii](https://code.google.com/p/ftpii/source/list)** by Joseph Jordan and contributors: the FTP idea.
* **[WiiXplorer](https://github.com/niuus/wiixplorer)** by Dimok and contributors, and the niuus fork: the file-manager reference.
* **GlowWii**: the Classic Controller button mapping (carried over from GodModeWii).
* **Sharpii** and **libWiiSharp** (Leathl and contributors): WAD, U8, IMD5 and LZ77 (LZ10 / LZ11) layouts.
* **[wad2bin](https://github.com/DarkMatterCore/wad2bin)** by DarkMatterCore: the `content.bin` layout and the IMET / U8 handling.
* **Dolphin's WiiSave code**: the `data.bin` layout.
* **wfslib** and **wfs-tools** by koolkdev: the WFS layout that the WFS image reader follows.
* **vffmod** (wii-tools): the VFF layout.
* **[wut](https://github.com/devkitPro/wut)** and **devkitPPC** (devkitPro): the toolchain and the Cafe OS libraries.
* **[unarr](https://github.com/selmf/unarr)** by Jan Ruhe (selmf) and contributors (LGPL-3.0, in `source/unarr`, with the LZMA SDK by Igor Pavlov inside): the zip, 7z and RAR (up to 4.x) readers of the FTP installer. RAR5 is read by `source/rar5.c`, written from the public format description.
* **GodModeWii** (quatric): the code this port starts from.
