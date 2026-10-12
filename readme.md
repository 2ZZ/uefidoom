# Cacodemon345's UEFI-DOOM (forked from warfish's DOOM repo)

A port of DOOM to UEFI systems.
Tested with: QEMU with OVMF, MSI B150M-PRO-VD UEFI firmware, a HP Pavilion 8th Gen laptop.

# Building with Docker
Requires Docker. Run:

```sh
./build.sh          # RELEASE build
./build.sh DEBUG    # DEBUG build
```

This builds an image with EDK II, EDK II LibC and AudioPkg (pinned versions, GCC toolchain) and writes `out/doom.efi`. It works on x86_64 and ARM64 hosts (ARM64 cross-compiles to X64).

# Building with Visual Studio
Prerequisites:
1. EDK II: https://github.com/tianocore/edk2
2. Visual Studio 2015 toolset
3. EDK II LibC (EADK): https://github.com/tianocore/edk2-libc

Instructions:
1. Install the Visual Studio 2015 toolset.
2. Clone the EDK II and EDK II LibC repo.
3. Follow the instuctions for setting up the workspace. You also can just drop everything from the EDK II LibC repo to the main EDK II workspace.
4. Edit the AppPkg.dsc; add the following lines at the end of [LibraryClasses] list:
	`SerialPortLib|PcAtChipsetPkg/Library/SerialIoLib/SerialIoLib.inf`
	`RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf`
   Add this to its [PcdsFixedAtBuild] section (needed to launch without the UEFI Shell):
	`gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE`
   Also add the path to the inf file of this project in the [Components] list like this:
	`AppPkg/Applications/uefidoom/doom.inf`
5. Move the "uefidoom" folder from this project inside Applications.
6. Download the AudioDxe source code from https://github.com/FangJiaZhen/AudioPkg.
7. Open a command prompt, type `edksetup.bat` (assuming you set up the environment properly) and then type `build -t VS2015 -b DEBUG -a X64`
8. If everything goes well, look for a file called "doom.efi" inside this path: 
`path\to\edk2\Build\AppPkg\DEBUG_VS2015\X64\`

Alternatively, `uefidoom.dsc` in this repo is a standalone platform file (used by the Docker build).

# IWADs
Put an IWAD in the same folder as `doom.efi`. It is chosen by file name, in this order:

| File name    | Game |
|--------------|------|
| `doom2.wad`  | Doom II |
| `doomu.wad`  | The Ultimate Doom (4 episodes; Steam/GOG ship it as `DOOM.WAD`, so rename it) |
| `doom.wad`   | Registered Doom (3 episodes) |
| `doom1.wad`  | Shareware Doom (free) |

The game mode is then corrected from the WAD's contents, so a misnamed Doom 1 WAD still works. `plutonia.wad` and `tnt.wad` are not detected.

[Freedoom](https://freedoom.github.io/) works as a free replacement: rename `freedoom1.wad` to `doomu.wad`, or `freedoom2.wad` to `doom2.wad`.

# Running
Copy `doom.efi` and an IWAD into a folder on a FAT32 partition, such as the EFI System Partition (e.g. `\EFI\doom\`). Then either:

* **Boot manager / firmware:** launch `doom.efi` directly, e.g. from rEFInd (it picks it up automatically) or a firmware boot entry. No UEFI Shell is needed. Files are read from and saved to `doom.efi`'s folder.
* **UEFI Shell:** change to the folder (e.g. `FS0:` then `cd EFI\doom`) and type `doom`.

The game keeps the current display mode and scales the picture to the largest 4:3 area that fits the screen (e.g. 1440x1080 at 1080p).

If the game exits with an error, the message stays on screen until a key is pressed.

# Bugs:
1. ~~The "numsprites" variable in the sprite loading function will be decreased by 3 to allow it to load the sprites.~~
2. ~~Game is very slow.~~
3. ~~TICRATE constant remains 65 from the original DOOM UEFI port by warfish.~~
4. ~~In-game input doesn't work?~~
# Planned:
1. Audio support (using GoldFish64's AudioDxe driver).
# Helping
Help is accepted! Please let me know of any problems. Thanks.
