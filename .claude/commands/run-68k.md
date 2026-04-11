---
description: Build the 68k OT target and launch it in the QemuMac Quadra 800 VM (Mac OS 7.6.1). Pass "2p" to launch two VMs with VLAN networking for multiplayer testing.
---

## User Input

```text
$ARGUMENTS
```

## Goal

Build BomberTalk for the 68k Open Transport target (matching the QEMU Quadra 800 VMs running Mac OS 7.6.1 with OT) and boot it in the emulator. The build produces a MacBinary `.bin` file which is copied onto the QemuMac shared disk(s) using `hcopy -m` to preserve resource forks.

If the user passes `2p` as an argument, launch **two** VMs with VLAN networking (QEMU socket multicast) so they can discover each other via PeerTalk UDP broadcast and play a 2-player game.

## Steps

1. **Build the 68k OT target** using the local clog and peertalk checkouts:

```bash
cd /home/matt/BomberTalk
rm -rf build-68k-ot
mkdir -p build-68k-ot && cd build-68k-ot
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/home/matt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=OT \
  -DPEERTALK_DIR=/home/matt/peertalk \
  -DCLOG_DIR=/home/matt/clog
make -j$(nproc)
```

If the build fails, stop and report the errors. Do not proceed to launching the VM.

2. **Copy BomberTalk onto the shared disk(s)** using hfsutils (preserves resource fork via MacBinary decode):

```bash
export PATH=/home/matt/Retro68-build/toolchain/bin:$PATH
hmount /home/matt/QemuMac/shared/shared-disk.img
hdel :BomberTalk 2>/dev/null; true
hcopy -m /home/matt/BomberTalk/build-68k-ot/BomberTalk.bin :
humount
```

If **2p mode**, also copy to the second shared disk:

```bash
hmount /home/matt/QemuMac/shared/shared-disk-2.img
hdel :BomberTalk 2>/dev/null; true
hcopy -m /home/matt/BomberTalk/build-68k-ot/BomberTalk.bin :
humount
```

3. **Launch QEMU VM(s)**

**Single player** (default) — launch one VM with user networking:

```bash
cd /home/matt/QemuMac
./run-mac.sh -c vms/68k_quadra_800/68k_quadra_800.conf
```

**Two player** (argument contains `2p`) — launch both VMs with VLAN networking. Run both in the background:

```bash
cd /home/matt/QemuMac
./run-mac.sh -c vms/68k_quadra_800/68k_quadra_800.conf -n vlan
```

```bash
cd /home/matt/QemuMac
./run-mac.sh -c vms/68k_quadra_800_2/68k_quadra_800_2.conf -n vlan
```

Run both QEMU commands in the background so the user can continue using the terminal.

4. **Report** that the VM(s) are booting and remind the user:
   - BomberTalk is on the "untitled" shared disk on the Mac desktop
   - Right-Ctrl + G releases mouse grab
   - Left-Shift + Left-Cmd + Q/W for Quit/Close inside the VM
   - If 2p mode: remind about static IP configuration in TCP/IP (OT) — VM1: `10.0.0.1`, VM2: `10.0.0.2`, subnet `255.255.255.0`
   - If 2p mode: VLAN mode has no internet access — use single player mode if downloads are needed
