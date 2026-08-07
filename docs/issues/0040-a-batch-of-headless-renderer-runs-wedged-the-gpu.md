---
id: 40
title: A batch of headless renderer runs wedged the GPU: 219 ring timeouts and 65 full resets
status: open
symptom: reported after the machine went down. amdgpu on an RX 6700 XT logged 219 'ring gfx timeout' and 65 'GPU reset begin' with 'VRAM is lost due to GPU reset', ALL of them inside the last 75 minutes of a boot that had been up 46 hours with none before -- and that window is exactly a run of back-to-back headless Vulkan sessions of this port. The same boot then died to the OOM killer with 19 concurrent compilers alive
tags: performance
created: 2026-08-06
updated: 2026-08-07
---

## Root cause


## What was tried / dead ends


## Resolution

### Note (2026-08-06)
TWO CAUSES FOUND IN THIS PORT'S OWN CODE, both mine, both now fixed. Neither is proven to be
THE cause -- a ring timeout is the driver saying a job missed its deadline and an overloaded
machine can produce that on its own -- but both are real, both are the kind of thing that
wedges an allocator, and the evidence is one-sided enough not to wait for proof:

  219 timeouts and 65 resets in 75 minutes; ZERO in the preceding 46 hours of the same boot.

1. GPU TEXTURE CHURN, runtime/render.c. GDI text arrives as ~40 small tiles per frame and each
   was given a freshly created SDL texture, destroyed again at the frame reset. At 30 fps that
   is about 2400 GPU texture allocations AND frees per second, sustained for the length of
   every run, across dozens of runs. Now POOLED and keyed on exact size -- a tile's size
   repeats constantly, so after a few frames the steady-state allocation count is zero. The
   report prints allocations PER FRAME precisely so a return of the churn is visible as a
   number rather than as a wedged machine three hours later.

2. AN ABANDONED ALLOCATION, runtime/ddraw.c. HIGH_MAX was raised to 2304 rows while trying a
   full-window composition. That experiment was measured, rejected and reverted -- but the
   allocation stayed, so every window-following surface was 37.7 MB instead of 9 MB for rows
   nothing would ever draw into: 302 MB of committed guest memory per instance instead of 72.
   On a 15 GB machine already running 19 compilers that is not nothing. HIGH_MAX is the game's
   own 550 now, which is every row the composition can ever have.

WHAT WAS ALSO WRONG AND IS NOT CODE: `cmake --build -j` with no job limit, on a 16-core, 15 GB
machine, run repeatedly and sometimes alongside another project's C++ builds, and headless
game runs overlapped with ctest despite this project's own rule that the slow tests are
RUN_SERIAL and must not be doubled up. The OOM kill names 19 live compilers. Use -j4.

STILL OPEN: nothing here has been re-verified on the GPU, because the machine has just been
rebooted and running the thing that may have wedged it is not the next move. What a
verification should look like: one run, watching `journalctl -k -f` for ring timeouts, with
LF2_RENDER_DEBUG=1 checking that tile allocations per frame go flat.

### Note (2026-08-07)
RECURRED 2026-08-07, reported again by the user after another forced reboot. The two fixes
from 2026-08-06 (texture pooling in render.c, HIGH_MAX back to 550 in ddraw.c) did NOT stop
it, and they were never verified on the GPU. Do not treat #40 as understood.

TODAY'S EVIDENCE (boot -1, monotonic clock so kernel/userspace lines are comparable -- the
wall-clock stamps in this journal are skewed ~3h for early-boot kernel lines and an
interleaving built on them is WRONG):

  18 'GPU reset begin', 8 'Illegal register access'/'Illegal opcode in command stream',
  gfxhub page faults (GCVM_L2_PROTECTION_FAULT_STATUS), 'VRAM is lost due to GPU reset',
  and 16 devcoredumps naming 'Process lf2' -- each within 3-55 s of an lf2 launch.
  225 lf2 launches that boot.

PER-BOOT DISCRIMINATOR (run against both classes, not reasoned about):

  boot | resets | illegal | coredumps naming lf2 | lf2 launches
   -1  |   18   |    8    |         16           |    225
   -2  |   65   |    1    |          0           |   1200
   -3  |    0   |    0    |          0           |      0
   -4  |    0   |    0    |          0           |      0

  Zero resets in both boots with zero lf2 runs. But the correlation does NOT establish lf2 as
  the culprit, and the note in this issue that implied it was overstated:
   - boot -2 is the WORST boot (65 resets) and names lf2 ZERO times; it names plasmashell 262
     and kwin_wayland 97. A devcoredump names the process whose job was on the ring at reset
     time, which after a hang is usually the VICTIM (the compositor) rather than the cause.
   - boot -1 has a second GPU-using suspect: 'x2native' named 24 times, another project's
     binary that was building/running concurrently. It is NOT ruled out.
  So: strong association with lf2 ACTIVITY, unproven attribution to lf2 CODE.

RULED OUT TODAY (with denominators, so this is not a silent negative):
 - Non-terminating shader, the textbook app-side cause of a ring timeout: all 3 hd2d shader
   sources (runtime/shaders/hd2d_{gbuf,light,shadow}.frag) contain ZERO for/while/do
   constructs. 3 of 3 files checked.
 - The port emitting bad GPU packets directly: not possible through Vulkan/SDL_GPU. An app
   does not write PM4. 'Illegal opcode in command stream' therefore means either RADV being
   driven into generating bad packets, or GPU-visible memory being CORRUPTED or FREED WHILE
   IN FLIGHT. That reframes this issue away from the 2026-08-06 'allocation churn/overload'
   theory, which cannot produce an illegal opcode.

LEADING HYPOTHESIS, NOT YET TESTED: a stray guest write into GPU-visible memory. This port
gives the guest a 4 GiB address space with hand-rolled arenas (guest_map.h) and has ALREADY
had a surface arena overrun a sound arena and play bitmaps as audio. A wild write landing in
a mapped GPU buffer produces exactly this signature. Second candidate: use-after-free of a
GPU resource in the new hd2d path (runtime/hd2d.c), which is the newest and least-verified
GPU code in the tree, landed in cb246df.

WHAT A VERIFICATION MUST LOOK LIKE (none of this has been done):
 1. ONE run, not a batch, with nothing else on the GPU -- x2native must be off too, or the
    run cannot discriminate.
 2. Vulkan validation layers ON (VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation). Validation
    is the instrument that catches use-after-free and out-of-bounds GPU access; running
    without it is why three sessions have now guessed instead of measured.
 3. `journalctl -k -f` watched live for the first ring timeout.
 4. An A/B against LF2_HD2D=off, which is the only cheap way to test whether the new shader
    path is involved.
Until at least 1-3 are done, DO NOT run batches of headless sessions on this machine.
