---
id: 4
title: Surface arena overwrote every sound buffer; game played bitmaps as audio
status: resolved
symptom: SFX loud, skippy, wrong pitch, garbage; first menu sounds correct, all later SFX broken
tags: audio,dsound,memory,arena,corruption,root-cause
created: 2026-08-04
updated: 2026-08-04
---

Cause: two unbounded bump arenas. Surfaces at VRAM_BASE 0x50000000 grow to ~0x63a00000 (393 allocs, 322 MB measured); sound PCM arena sat at 0x60000000 (116 buffers, ~4 MB). vram_alloc had no upper bound, so surfaces overwrote all sound PCM silently.

Why menu sounds worked: they play BEFORE the surface arena grows past 0x60000000. Nothing to do with sample rate -- a rate-based theory fit the same evidence and was WRONG.

Method that found it: checksum each buffer's PCM at Unlock, re-check at Play. First established the bytes matched the on-disk WAVs at Unlock (116/116 byte-identical, LF2_AUDIO_DUMP_SRC), so a mismatch at Play meant an outside writer. Two CLOBBERED reports at 60000000 and 60009000 -- the head of the PCM arena, exactly where surfaces reach first.

Fix: runtime/cpu/guest_map.h declares every arena base/size in one place, _Static_assert fails the build on overlap, and both allocators abort at their reservation end. Map uses #define not enum -- 0x90000000 as an enum constant is a negative int and bounds checks against it silently pass.

Ruled out along the way (do not re-derive): mixer resampling (unit-tested, tests/test_mixer.c, with the old implementation as a control -- it already reproduced pitch correctly at every rate); WAVEFORMATEX parse (matches file headers; odd rates 10989/11300/20200/22100/23600/38400/44000 are genuinely in the files); MAX_BUFS overflow (0 unregistered); mixer under-delivery (max request 512 vs 4096 slice, never fires); music use-after-free (real, fixed separately, but not this symptom).

Evidence: PCM clobbers 2->0; jumps>16000 in an 86.7s recording 7180->1653; clipped 74->19. Residual 1653 is source content (m_join.wav has 315 such jumps, plays 5x; m_ok 12, plays 4x -> ~1623 predicted).

Instruments added: LF2_AUDIO_DUMP_MIX (records the device mix to WAV), LF2_AUDIO_DUMP_SRC (dumps each buffer at Unlock), LF2_PLAY_DEBUG, permanent Unlock/Play PCM checksum guard.
