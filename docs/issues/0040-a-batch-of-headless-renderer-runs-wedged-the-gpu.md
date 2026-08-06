---
id: 40
title: A batch of headless renderer runs wedged the GPU: 219 ring timeouts and 65 full resets
status: open
symptom: reported after the machine went down. amdgpu on an RX 6700 XT logged 219 'ring gfx timeout' and 65 'GPU reset begin' with 'VRAM is lost due to GPU reset', ALL of them inside the last 75 minutes of a boot that had been up 46 hours with none before -- and that window is exactly a run of back-to-back headless Vulkan sessions of this port. The same boot then died to the OOM killer with 19 concurrent compilers alive
tags: performance
created: 2026-08-06
updated: 2026-08-06
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
