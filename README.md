# GodModeU

A text-mode file manager for the **Wii U**, in the style of [GodMode9](https://github.com/d0k3/GodMode9). It is a native
Cafe OS homebrew app and a port of [GodModeWii](https://github.com/quatric/GodModeWii). You get a text grid on the TV, a
cursor and a lot of power: copy, cut, paste and delete files between every drive the Wii U can mount, see how full each one
is, and (only if you really mean it) change the protected storage.

By quatric & cmyksoda.

> ## ⚠️ Brick risk
>
> GodModeU can permanently brick your console. Writing, deleting or renaming files on the internal storage (MLC), the
> Wii U NAND (SLC) or the vWii NAND can leave it unbootable.
>
> * Protected drives are **locked by default**. Copying *from* them is always safe.
> * The first time you try to change one (delete, paste, cut, rename, new folder...) you get a warning and have to enter
>   the **Konami code**. Each protected drive asks for its own code and stays unlocked until you exit or lock it again from
>   the HOME menu.
> * **Make a full NAND / MLC backup first** and keep a copy off the console. See [NAND backup](#nand-backup).
> * There is no warranty. What you do with it is your responsibility. If you don't know what a file does, leave it alone.

## Install

* **Aroma:** copy `GodModeU.wuhb` to `sd:/wiiu/apps/`.
* **Homebrew Launcher:** copy the `GodModeU` folder from the release zip (`GodModeU.rpx`, `meta.xml`, `icon.png`) to
  `sd:/wiiu/apps/GodModeU/`.

You need Aroma for USB drives the Wii U didn't format and for both NANDs, because they are reached through Aroma's Mocha.

Settings, themes, drawings and memos are kept in `sd:/wiiu/apps/GodModeU/`.

## Status

Version 0.4.0 was tested on a real Wii U with Aroma: SD, WFS, FAT32, NTFS and ext4 USB drives, MLC and both NANDs, copying,
the GamePad, a Wii Remote, the FTP server, Export Miis and Save Wii U keys.

Newer than that and not yet tried on a console: see [Not yet tested](#not-yet-tested). Reports from real hardware are very
welcome.

## Drives

| Drive | Path | Notes |
| --- | --- | --- |
| SD card | `sd:/` | Read / write. |
| USB drives | `usb1:/` to `usb8:/` | Wii U (WFS) drives, FAT32 / exFAT / NTFS (read / write) and ext2 / ext3 / ext4 (read-only). More partitions take the next free `usbN:`. |
| Wii U storage (MLC) | `mlc:/` | The internal storage. Protected. |
| vWii NAND | `nand:/` | System Menu, IOS, Wii channels and saves. Protected. Needs Aroma. |
| Wii U NAND (SLC) | `slc:/` | The Wii U's own system NAND. Protected, with an extra red warning, because a mistake can stop the console booting at all. Needs Aroma. |

FAT32 has the usual 4 GiB file limit; exFAT and NTFS don't. An NTFS drive that Windows didn't shut down cleanly is left
alone. Remount drives from the HOME menu after plugging one in or out.

## Controls

| Button | In the file browser |
| --- | --- |
| D-Pad Up / Down | Move (hold to scroll) |
| D-Pad Left / Right | Page up / down |
| **A** | Open a folder, archive or file. Paste on the green `Paste` row. |
| **B** | Go up one folder (back to the drive list at the top) |
| **Y** (Wii Remote **1**) | Mark / unmark the item and move down. Clears the clipboard on the `Paste` row. |
| **X** (Wii Remote **2**) | Actions menu |
| **+** | Copy the marked items (or the one under the cursor) |
| **-** | Cut the marked items |
| **HOME** | HOME menu (HOME or B closes it) |
| Touch screen | Tap to point (a tap is **A**) |

Every controller [libwiiuinput](https://github.com/quatric/libwiiuinput) reads works, with its buttons mapped to the Wii
Remote's: GamePad, Wii Remotes (with Nunchuk or Classic Controller), Pro Controllers, the GameCube adapter, USB game
controllers, keyboards and mice. They can be plugged in at any time.

## Features

**Files and drives**

* SD card, USB drives, the Wii U storage (MLC), the vWii NAND and the Wii U NAND in one text interface
* Free space and a usage bar for each drive
* WFS (Wii U), FAT32, exFAT and NTFS USB drives read / write
* ext2, ext3 and ext4 USB drives read-only
* Copy, cut, paste and delete with progress, with **hold B to cancel**
* Multi-select, select all, deselect all and invert selection
* Rename, new folder and new file
* Info and folder size
* Disk usage
* Text viewer and hex viewer
* In-place text and hex editors
* Protected drives locked behind a Konami code, each with its own code
* Forced risky mode, switched on or off with the Konami code on the logo screen

**Viewing and playing**

* Everything GodModeWii opens: archives, disc images, disk partitions, WADs, images, audio, MIDI, video, text, PDF and EPUB
* Wii U archives: SARC, `.szs`, BFSAR, BFWAR, BARS and NUS3AUDIO
* Wii U images: GTX, BFLIM, BFFNT, BFRES textures, NUT, DDS and Retro Studios TXTR
* A 3D viewer for BFRES models, with skinning and skeletal animations
* A BFLYT layout viewer with BFLAN animations
* Wii U audio: BFSTM, BFWAV, BTSND, BFSEQ sequences and IDSP
* Wii U text formats: BYAML / BYML, XMB, AAMP, BGLPBD, BFLYT, BFLAN and BFSEQ
* Miis

**Installing and backing up**

* Install Wii U titles from WUP folders
* Install Wii U titles from WUD / WUX disc images
* Back up installed titles as WADs, the way YABDM does
* Split DLC WADs into small WADs
* Export all vWii saves
* Export Wii U Miis as `.ffsd`
* Save this console's WFS keys for GodModeWii
* NAND / MLC backup instructions in the HOME menu

**Network**

* FTP server, read-only or read / write, with a random password
* FTP install server that unpacks `.zip`, `.7z` and `.rar` uploads
* Wiiload: send `.rpx` / `.wuhb` homebrew and have it installed to the SD card or a USB drive

**Input**

* GamePad buttons and touch screen
* Wii Remotes with a Nunchuk or Classic Controller
* Wii U Pro Controllers and the GameCube adapter
* USB game controllers, keyboards, mice and pen tablets
* The Wii U's own on-screen keyboard for typing names
* Aroma's plugin menu over the app (L + Down + Minus)

**HOME menu extras**

* Sketchpad
* Voice memos
* Alarm clock
* Calculator
* Clock and calendar
* Colour themes
* Sound effects, which can be turned off
* Lock the protected drives again, remount, restart, power off, exit to the Homebrew Launcher or Wii U Menu
* A TiVo sound mode, as an easter egg

The details of each are below, and in [docs/FORMATS.md](docs/FORMATS.md).

## More about the features

* **Browse and manage files:** copy, cut, paste and delete with progress (**hold B to cancel**), multi-select, rename, new
  folder, new file, info, disk usage, and text / hex viewers and editors.
* **Open lots of file types.** The viewer is chosen from what is inside the file. Everything GodModeWii opens opens here
  (archives, disc images, disk partitions, WADs, images, audio, MIDI, video, text, PDF and EPUB), plus the Wii U formats:
  SARC, BFSAR, BFWAR, BARS, NUS3AUDIO, GTX, BFLIM, BFFNT, BFRES (with a 3D viewer and animations), BFLYT / BFLAN layouts, NUT,
  TXTR, BYAML, XMB, AAMP and more. Details are in [docs/FORMATS.md](docs/FORMATS.md).
* **Install Wii U titles** from WUP folders and WUD / WUX disc images. See [below](#installing-wii-u-titles).
* **Split a DLC WAD** into small WADs the way [dlc-wad-splitter](https://github.com/DarkMatterCore/dlc-wad-splitter) does
  (actions menu on a DLC `.wad`, or mark several). Each one holds the TMD as it is (still signed), content 0 and the next
  1, 5, 10, 20 or 50 contents, named `<title id>_split_<n>.wad`, in `GodModeU/wad-split/` on the SD card or a USB drive. They
  install on the vWii with GodModeWii or any WAD manager.
* **FTP server** and FTP install server. See [FTP server](#ftp-server).
* **Export Wii U Miis** (HOME menu): pick Miis by name and save them as `<Name>.ffsd` in `/savemiis/` on the SD card or a
  USB drive. The Mii database is only read.
* **Save Wii U keys for GodModeWii** (HOME menu): writes this console's two WFS keys to `sd:/wfs/` so GodModeWii can read
  the Wii U drive from vWii. They are unique to your console, so keep them private.
* **Wiiload:** homebrew (`.rpx` / `.wuhb`) sent over the network is installed to `wiiu/apps/` on the SD card or a USB drive. See [below](#installing-with-wiiload).
* **Extras in the HOME menu:** alarm clock, calculator, clock and calendar, sketchpad, voice memos, sound effects, themes.
* **Forced risky mode:** enter the Konami code on the logo screen at startup to switch "dangerous permissions always on"
  on or off. It is saved as `sd:/wiiu/apps/GodModeU/force.cfg`.
* **Typing names** uses the Wii U's own on-screen keyboard on the GamePad. If it can't start, GodModeWii's character roller is
  used.
* **Aroma's plugin menu** opens over the app with its usual combo (L + Down + Minus).

## Installing Wii U titles

### WUP folders

A WUP folder holds `title.tmd`, `title.tik`, `title.cert`, the `.app` contents and their `.h3` hashes. GodModeU installs it
with the system's own installer (MCP), the way [WUP Installer GX2](https://github.com/Fangal-Airbag/wup-installer-gx2) does.

1. Copy the folder to the SD card, for example `sd:/install/<name>/`.
2. Press **A** on its `title.tmd`, or put the cursor on the folder (or mark several) and pick *Install Wii U title (WUP)*.
3. Choose internal storage or a USB drive. A progress bar shows the bytes written.

Games, updates, DLC and demos can be installed. System titles are refused. Titles that aren't signed by Nintendo for your
console (eShop dumps, DLC, homebrew) need **signature patches** (Aroma's sigpatches module). Common errors are explained when
they happen: no USB drive, not enough space, a bad or foreign ticket, an SD card problem (format it FAT32 with 32 KB
clusters).

### WUD / WUX disc images

Press **A** on a `.wud`, a `.wux` or wudump's `game_part1.wud`. GodModeU reads the disc, writes it out as a WUP folder in
`sd:/install/<disc name>/` the way FIX94's [wud2app](https://github.com/FIX94/wud2app) does, then installs that folder and
offers to delete it. *Show the partitions* lists what is on the disc. You need:

* the **disc key**: wudump's `game.key` next to the image, or `<image name>.key`
* the **Wii U common key**: `common.key` (16 bytes, or 32 hex digits) or the `otp.bin` from a NAND backup. Put it in
  `sd:/wiiu/apps/GodModeU/`, at the root of the SD card, or next to the image. Every key found is tried until the game's file
  table decrypts, so a wrong key is reported, never used.
* room on the SD card for the whole game (no file in a game is over 4 GB, so FAT32 is fine)

Hold **B** to cancel. The HOME menu's *Install Wii U titles (WUP / WUD help)* shows these steps on the console.

## Installing with Wiiload

Wiiload sends homebrew to the Wii U over the network and puts it on the **SD card or a USB drive** (press **1** on the Wii U to
switch). On the Wii U, open HOME → *Wiiload*, then from your computer:

```sh
WIILOAD=tcp:192.168.1.23 wiiload MyApp.wuhb
```

* A `.rpx` goes to `wiiu/apps/<name>/`, and a `.wuhb` to `wiiu/apps/`, where Aroma and the Homebrew Launcher look.
* A `.zip`, `.7z` or `.rar` is unpacked into `wiiu/apps/`.
* Hold **B** to cancel.

## NAND backup

Cafe OS doesn't let an app read the NAND chips raw, so GodModeU only copies files. To make a full backup, use
[nanddumper](https://github.com/GaryOderNichts/nanddumper) under Aroma (see [wiiu.hacks.guide](https://wiiu.hacks.guide/)):

1. Put nanddumper in `sd:/wiiu/apps/` and start it.
2. Set SLC, SLCCMPT, OTP and SEEPROM to *Yes*. Add MLC too if the card has room (8 GB or 32 GB more).
3. Press **A**.
4. Copy `slc.bin`, `slccmpt.bin`, `otp.bin`, `seeprom.bin` and `mlc.bin.part*` off the card and keep them safe.

The HOME menu's *Back up the NAND / MLC (how to)* has the same steps. The `otp.bin` is also what GodModeU needs for WUD
images.

## FTP server

HOME menu → *FTP server (read-only)* or *(read/write)* shows the address, the user name (`godmode`) and a random password.
Connect with any FTP client. Every drive appears as a folder (`/sd`, `/usb1`, `/mlc`, `/nand`, `/slc`...).

* It listens on port 21, or **2121** if Aroma's ftpiiu already uses 21.
* Login is required by default, with a new random password each run. The status screen can switch to open login for a
  trusted network. Failed logins are slowed down, and a burst of them locks the server for a minute.
* Local network only: connections from outside private / link-local ranges are refused.
* Read-only by default. Changing MLC or the NANDs still needs the Konami code on the console.
* Uploads go to a temporary name and only replace the target when complete.
* `RMD` only removes empty folders, so nothing is deleted recursively over the network.
* FTP bounce is blocked: `PORT` must point back at the client, and passive connections must come from the client's address.

The **FTP install server** unpacks uploaded `.zip`, `.7z` and `.rar` files onto the SD card or the first USB drive.

## Not yet tested

These couldn't be checked without a console:

* exFAT USB drives, more than one non-WFS USB drive at once, and the WFS reader on a dump of a real Wii U drive
* the screen: a 640x480 picture drawn 1.5x on the TV and 1:1 on the GamePad (`source/wiiu/screen.c`)
* sound: GodModeWii's sound voices mixed onto two AX voices (`source/wiiu/asnd.c`)
* installing to the SD card through MCP's `/vol/app_sd/...` path (`source/wiiu_install.c`)
* Wii U texture tiling: checked against sample files on a PC, but BFFNT sheets after the first use a guessed swizzle, and NUT
  files from the Wii U use tile mode 4

## What GodModeWii has that this doesn't

These need the Wii's IOS or hardware, or GX, and have no equivalent in a native Cafe OS app:

* the GX 3D viewer for Wii formats (BRRES, BMD...), the drawn BRLYT viewer and Mii faces. Wii U BFRES models and BFLYT layouts
  have their own software viewers. Wii BRLYT layouts fall back to text.
* installing WADs to the NAND, wad2bin, WAD backups and Wii Menu themes
* the Wii NAND through IOS, raw NAND dumping, the SEEPROM / OTP readers and the `keys:/` drive
* the Wii disc drive, TinyLoad, SD Gecko, GameCube memory cards and the disc slot light

## Building

You need [devkitPro](https://devkitpro.org/) with devkitPPC and wut, plus these portlibs:

```sh
dkp-pacman -S wut wut-tools ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-libvorbisidec ppc-libogg ppc-bzip2 ppc-zlib
git submodule update --init --recursive
./scripts/build_libmocha.sh   # builds libmocha, libdvm, libntfs and lwext4 into ./portlibs, once each
./scripts/build_libdvm.sh
./scripts/build_libntfs.sh
./scripts/build_libext4.sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make
```

`make` builds the `libwiiuinput` submodule first, then `GodModeU.rpx` and `GodModeU.wuhb`. CI builds every push (download the
`GodModeU` artifact), and pushing a tag like `v0.5.0` publishes a GitHub Release with the zip.

## Credits

GodModeU stands on a lot of other people's work. Thank you!

**MoonShell by Moonlight inspired us a lot.** Its 2.10 interface sounds are used here (GPL-2.0 or later, `res/sfx.bin`), and the
clock, calendar, calculator and alarm follow its own.

**Code included**

* [GodModeWii](https://github.com/quatric/GodModeWii) (quatric & cmyksoda): the code this port is made of, and all its credits
* [MoonShell](https://www.gamebrew.org/wiki/Moonshell) (Moonlight): the interface sound effects
* TiVo: the easter-egg interface sounds (`res/tivo.bin`). The last credits page has a hint, and the code also works on the drive list.
* [libwiiuinput](https://github.com/quatric/libwiiuinput) (quatric): every controller
  * after snes9xgx's Wii U driver (Daryl Borth) and RiftWii's GameCube adapter code
* [libmocha](https://github.com/wiiu-env/libmocha) (Maschell, LGPL-3.0): reaches USB drives, internal storage and both NANDs through Aroma's Mocha
* [wii-mobiclip-player](https://github.com/quatric/wii-mobiclip-player) (quatric, MIT): the Mobiclip, THP and Flipnote players
  * FFmpeg's Mobiclip decoder (Florian Nouwt, Adib Surani, Paul B Mahol; LGPL 2.1+)
  * NanoJPEG (Martin J. Fiedler) and Tremor
* [TinySoundFont](https://github.com/schellingb/TinySoundFont) (Bernhard Schelling): MIDI playback
  * [SFZero](https://github.com/stevefolta/SFZero) (Steve Folta)
* [dr_libs](https://github.com/mackron/dr_libs) (David Reid): WAV, MP3 and FLAC decoding
  * [minimp3](https://github.com/lieff/minimp3) (lieff)
* [Zstandard](https://github.com/facebook/zstd) (Meta): RVZ decoding, with xxHash and Finite State Entropy (Yann Collet)
* [unarr](https://github.com/selmf/unarr) (selmf and contributors, LGPL-3.0): zip, 7z and RAR readers
  * LZMA SDK (Igor Pavlov) and The Unarchiver's XADMaster code
* [lwext4](https://github.com/gkostka/lwext4) (Grzegorz Kostka; Extrems' big-endian fork) and Extrems' libdvm ext4 driver: ext2/3/4 drives
* [libntfs](https://github.com/extremscorner/libntfs) (Extrems) on NTFS-3G (Tuxera and the NTFS-3G authors): NTFS drives
* [libdvm](https://github.com/devkitPro/libdvm) (fincs / devkitPro) with [FatFs](https://github.com/devkitPro/fatfs-mod) (ChaN): FAT32 and exFAT drives
* [wut](https://github.com/devkitPro/wut) and devkitPPC (devkitPro): the toolchain and the Cafe OS libraries
* libpng, libjpeg-turbo, libwebp, zlib, bzip2 and libogg (devkitPro portlibs)

**Formats and references**

* [nintoolbox](https://github.com/quatric/nintoolbox) (quatric): file type detection and the Wii U format readers
  * GX2 tiling after aboood40091's addrlib.py, and decaf-emu's AddrLib
  * BFRES, GTX, BFLIM, BFFNT, SARC, BFSAR, BARS, NUS3AUDIO, BYML, XMB, AAMP, BFLYT
* [mobipeg](https://github.com/quatric/mobipeg) (quatric): the audio and texture formats, and the AJPG / ODH codec
* [VGMTrans](https://github.com/vgmtrans/vgmtrans): the RSEQ sequence layouts
* [RiftWii](https://github.com/KakarottoCake/RiftWii) (KakarottoCake) and Dolphin's format description: the RVZ / WIA reader
* [lzo](https://www.oberhumer.com/opensource/lzo/) / lzop (Markus Oberhumer): LZO1X
* [wfslib / wfs-tools](https://github.com/koolkdev/wfs-tools) (koolkdev): the WFS layout
* [WUP Installer GX2](https://github.com/xhp-creations/WUP-Installer-GX2) (Dimok, Crediar, FIX94, xhp-creations; the [WUHB fork](https://github.com/Fangal-Airbag/wup-installer-gx2) by Fangal-Airbag) and [WUP Installer Y Mod](https://github.com/Yardape8000/wupinstaller) (Yardape8000): the MCP install sequence and its error messages
* [wud2app](https://github.com/FIX94/wud2app) (FIX94, MIT) and JWUDTool (Maschell): the WUD layout and how it becomes a WUP folder
* [dlc-wad-splitter](https://github.com/DarkMatterCore/dlc-wad-splitter) and [wad2bin](https://github.com/DarkMatterCore/wad2bin) (DarkMatterCore): the split DLC WAD layout, and finding the contents of a partial DLC WAD
* [nanddumper](https://github.com/GaryOderNichts/nanddumper) (GaryOderNichts) and [wiiu.hacks.guide](https://wiiu.hacks.guide/): the NAND backup steps

**Inspiration**

* [GodMode9](https://github.com/d0k3/GodMode9) (d0k3) and [GodMode9i](https://github.com/DS-Homebrew/GodMode9i) (DS-Homebrew): the look, feel and workflow
* MoonCalc and Morning Timer (for MoonShell): the calculator and the alarm
* [ftpii](https://code.google.com/p/ftpii/source/list) (Joseph Jordan and contributors): the FTP idea
* [WiiXplorer](https://github.com/niuus/wiixplorer) (Dimok, niuus and contributors): the file manager reference
