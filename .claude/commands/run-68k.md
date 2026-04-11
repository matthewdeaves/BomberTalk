---
description: Build the 68k target and launch it in the QemuMac Quadra 800 VM (Mac OS 7.6.1).
---

## User Input

```text
$ARGUMENTS
```

## Goal

Build BomberTalk for the 68k target and boot it in the QEMU Quadra 800 emulator. The build produces a MacBinary `.bin` file which is copied onto the QemuMac shared disk (which has a proper Apple Partition Map) using `hcopy -m` to preserve resource forks. The VM mounts this shared disk as a SCSI drive.

## Steps

1. **Build the 68k target** using the local clog and peertalk checkouts:

```bash
cd /home/matt/BomberTalk
rm -rf build-68k
mkdir -p build-68k && cd build-68k
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/home/matt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPEERTALK_DIR=/home/matt/peertalk \
  -DCLOG_DIR=/home/matt/clog
make -j$(nproc)
```

If the build fails, stop and report the errors. Do not proceed to launching the VM.

2. **Copy BomberTalk onto the shared disk** using hfsutils (preserves resource fork via MacBinary decode):

```bash
export PATH=/home/matt/Retro68-build/toolchain/bin:$PATH
hmount /home/matt/QemuMac/shared/shared-disk.img
hdel :BomberTalk 2>/dev/null; true
hcopy -m /home/matt/BomberTalk/build-68k/BomberTalk.bin :
humount
```

3. **Launch QEMU** with the Quadra 800 config and shared disk attached as SCSI ID 4:

```bash
cd /home/matt/QemuMac
/home/matt/QemuMac/qemu-source/build/qemu-system-m68k \
  -M q800 \
  -cpu m68040 \
  -m 128M \
  -bios roms/800.ROM \
  -g 1152x870x8 \
  -display sdl,grab-mod=rctrl \
  -nic user,model=dp83932,mac=08:00:07:12:34:56 \
  -drive "file=vms/68k_quadra_800/pram.img,format=raw,if=mtd" \
  -device scsi-hd,scsi-id=6,drive=hd0 \
  -drive "file=vms/68k_quadra_800/hdd.qcow2,format=qcow2,cache=writeback,aio=threads,detect-zeroes=on,if=none,id=hd0" \
  -device scsi-hd,scsi-id=4,drive=shared0 \
  -drive "file=shared/shared-disk.img,format=raw,if=none,id=shared0"
```

Run the QEMU command in the background so the user can continue using the terminal.

4. **Report** that the VM is booting and remind the user:
   - BomberTalk is on the "untitled" shared disk on the Mac desktop
   - Right-Ctrl + G releases mouse grab
   - Left-Shift + Left-Cmd + Q/W for Quit/Close inside the VM
