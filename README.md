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
> * The first time you try to change a protected drive (delete, paste, cut, rename, new folder, ...) a warning appears and
>   you must enter the **Konami code**. Each drive (MLC, vWii NAND, Wii U NAND) asks for its own code, and stays unlocked
>   until you exit or lock it again from the SYSTEM menu.
> * **Make a full NAND / MLC backup first** and keep a copy off the console.
> * This software comes with **no warranty**. You are solely responsible for whatever you do with it.
>
> If you aren't sure what a file does, leave it alone.

## Status

Tested on a real Wii U with Aroma: the drives below (SD, a WFS USB drive, FAT32, NTFS and ext4 USB drives, MLC and both NANDs),
copying, the GamePad, the FTP server, Export Miis and Save Wii U keys. It builds with devkitPPC and
[wut](https://github.com/devkitPro/wut) (wut 1.9.1, the version in devkitPro's package index). What hasn't been tried on a console
yet is listed under [Not yet tested](#not-yet-tested). Reports from real hardware are very welcome.

USB drives that aren't Wii U-formatted, both NANDs and parts of the internal storage are reached through **Aroma's Mocha**,
so they need Aroma.

## Drives

| Drive | Path | Notes |
| --- | --- | --- |
| SD card | `sd:/` | read / write. Mounted with `WHBMountSdCard` |
| USB drives | `usb1:/` ... `usb8:/` | Wii U-formatted (WFS) drives, which Cafe OS mounts as `/vol/storage_usb01` and up, and FAT32 / exFAT / NTFS drives, which GodModeU mounts itself (read / write; the 4 GiB file limit applies to FAT32 only), plus ext2 / ext3 / ext4 (Linux) drives, read-only. An NTFS drive Windows didn't shut down fully (Fast Startup, hibernation) is left alone. Plug a drive the Wii U didn't format in while GodModeU is running and pick *Remount drives*: the Wii U Menu offers to format drives it doesn't know |
| Wii U storage (MLC) | `mlc:/` | the console's internal storage. Protected: Konami code to change anything |
| vWii NAND (SLCCMPT) | `nand:/` | the vWii's NAND: System Menu, IOS, Wii channels and saves. Protected: Konami code to change anything. Mounted through Aroma's Mocha |
| Wii U NAND (SLC) | `slc:/` | the Wii U's own system NAND. Protected: Konami code, plus a red warning the first time you change it in a session, because a mistake there can stop the Wii U from booting at all. Mounted through Aroma's Mocha |

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
  off (saved as `sd:/wiiu/apps/GodModeU/force.cfg`). While it is on, nothing asks for the warning or code, except the
  Wii U NAND's own warning, once per session.
* Konami-code gate (asked once per drive, when you first try to change it) and brick warnings.
* **Export Miis** (SYSTEM menu): pick Miis by name and save them to the SD card or a USB drive in `/savemiis/`.
  Wii U Miis become `<Name>.ffsd` (the format Mii Studio loaders and other PC Mii tools read); **L / -** switches to the
  vWii's Miis, saved as `<Name>.miigx` like SaveGame Manager GX does. The Mii databases are only read.
* **Save Wii U keys for GodModeWii** (SYSTEM menu): writes this console's two WFS keys to `sd:/wfs/`, so GodModeWii can
  read the Wii U drive from vWii. They're unique to your console: keep them private.
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
| **HOME** | SYSTEM menu: lock risky options again, remount drives, FTP server, credits, exit. Press HOME or B again to close it |

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
  instead, and so is the Wii U Pro Controller. PS3 pads and USB keyboards aren't supported yet
* the `content.bin` and `data.bin` decryption that needs this console's own keys. WAD contents still decrypt with the
  common keys.

## FTP server

SYSTEM → *FTP server (read-only)* or *(read/write)* opens a status screen with the address, the user name (`godmode`) and a
fresh random password. Connect with any FTP client; every drive shows up as a folder (`/sd`, `/usb1`, `/mlc`, `/nand`, `/slc`, ...).

* It listens on port 21, or on **2121** when Aroma's ftpiiu already answers on 21.
* **Login is required by default**; the password is random per run. Failed logins are throttled and a burst of them locks the server for a minute.
  **Y / 1** on the server screen switches login off for that run (no user name or password; anyone on your network can connect) and back on.
* **LAN only**: peers outside private / link-local ranges are refused.
* **No FTP bounce / hijack**: `PORT` must point back at the client (port >= 1024) and passive data connections must come from the client's IP.
* **Sandboxed paths**: every path is canonicalised inside the virtual root.
* **Read-only by default.** Changes to MLC / NAND need the Konami code on the console (and the Wii U NAND's warning).
* **Safe uploads**: files go to a temporary name and only replace the target when complete.
* `RMD` only removes empty folders; nothing is deleted recursively over the network.

Supported commands: `LIST`, `NLST`, `MLSD`, `RETR` (with `REST`), `STOR`, `DELE`, `MKD`, `RMD`, `RNFR` / `RNTO`, `SIZE`,
`PASV`, `EPSV`, `PORT`.

## Not yet tested

* **Archive readers** are the same code as GodModeWii and were only checked against generated samples on a PC.
  The WFS image reader was never tried on a dump of a real Wii U drive.
* exFAT USB drives, more than one non-WFS USB drive at once, and starting with only a Pro Controller.

The console font is ASCII only, so accented letters show without their accents and other characters as `?`; the files keep
their real names.

## Installation

Build the app (below), then copy the result to the SD card:

* **Aroma** (or any launcher that runs `.wuhb` files): copy `GodModeU.wuhb` to `sd:/wiiu/apps/`. Aroma is needed for USB drives
  the Wii U didn't format and for both NANDs (see [Status](#status)).
* **Homebrew Launcher**: copy the `GodModeU` folder from the release zip (`GodModeU.rpx`, `meta.xml`, `icon.png`)
  to `sd:/wiiu/apps/GodModeU/`.

## Building

You need [devkitPro](https://devkitpro.org/) with devkitPPC and the wut toolchain:

```sh
dkp-pacman -S wut wut-tools
export DEVKITPRO=/opt/devkitpro
./scripts/build_libmocha.sh   # once each: libmocha, libdvm (FAT32 / exFAT), libntfs and lwext4 (ext2/3/4) into ./portlibs
./scripts/build_libdvm.sh
./scripts/build_libntfs.sh
./scripts/build_libext4.sh
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
* **[libmocha](https://github.com/wiiu-env/libmocha)** by Maschell (LGPL-3.0): reaches the USB drive, internal storage and both NANDs through Aroma's Mocha when the app isn't allowed to by itself.
* **[lwext4](https://github.com/gkostka/lwext4)** by Grzegorz Kostka (GPL-2.0 / BSD; Extrems' big-endian fork) with Extrems' libdvm
  ext4 driver (ZPL-2.1), ported and made read-only by `scripts/build_libext4.sh` and `scripts/ext4_driver.patch`.
* **[libntfs](https://github.com/extremscorner/libntfs)** by Extrems and contributors, on **NTFS-3G** by Tuxera and the NTFS-3G authors (GPL-2.0): NTFS USB drives, built by `scripts/build_libntfs.sh`.
* **[libdvm](https://github.com/devkitPro/libdvm)** by fincs / devkitPro (ZPL-2.1) with **[FatFs](https://github.com/devkitPro/fatfs-mod)** by ChaN: FAT32 and exFAT USB drives, built with exFAT on by `scripts/build_libdvm.sh`.
* **[unarr](https://github.com/selmf/unarr)** by Jan Ruhe (selmf) and contributors (LGPL-3.0, in `source/unarr`, with the LZMA SDK by Igor Pavlov inside): the zip, 7z and RAR (up to 4.x) readers of the FTP installer. RAR5 is read by `source/rar5.c`, written from the public format description.
* **GodModeWii** (quatric): the code this port starts from.
