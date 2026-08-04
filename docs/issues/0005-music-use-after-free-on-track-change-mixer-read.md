---
id: 5
title: Music use-after-free on track change: mixer read mus_pcm unlocked
status: resolved
symptom: ~1s burst of full-amplitude garbage shortly after the music changes
tags: audio,dsound,race,use-after-free
created: 2026-08-04
updated: 2026-08-04
---

music_load() freed mus_pcm up front, then spent ~0.15s decoding the replacement. The mixer callback runs on the audio thread and read mus_pcm/mus_frames while holding nothing -- SDL_LockMutex started BELOW the music block and covered only bufs[]. A track change with a pull in flight mixed freed heap at full scale.

Second, smaller window: mus_pcm was published before mus_frames, so a callback could read a new buffer with a stale length.

Fix: decode into a new buffer first; swap pointer, length and cursor together under mix_lock; free the old one only after the swap. The lock now covers the music read as well. ensure_mix_lock() exists because music can load before the device is ever opened. A failed load still clears the track, through the same locked swap.

Seen in a user recording: music restarted ~t=18s (per-second peaks repeated the opening values), then 2885 of 2905 discontinuities landed in t=20 at 1-frame spacing.

NOTE: this is NOT the cause of the general 'all SFX broken' symptom -- that was the arena collision, see the surface-arena issue. Fixing this alone left 7180 discontinuities in an 86.7s route that changes music only once.
