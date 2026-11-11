#ifndef LF2_NETPLAY_ONLINE_SESSION_H
#define LF2_NETPLAY_ONLINE_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Two LF2s meeting by a pasted code over Lucent's peer-to-peer transport, then playing every frame
 * through Lucent's rollback engine. LF2_ONLINE=host shows an invite; LF2_ONLINE=<invite> joins it.
 * The meeting happens before the guest boots, so both peers boot into the same state; the friend's
 * guest takes the host's calendar. */
void lf2_online_start(void);
int lf2_online_playing(void);
/* The session's next logic frame, rerunning earlier ones when a late input changed them and waiting
 * while the peer is too far behind. 0 once a quit came first. */
int lf2_online_advance(void);

#ifdef __cplusplus
}
#endif

#endif
