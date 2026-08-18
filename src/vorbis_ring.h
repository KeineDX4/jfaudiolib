// vorbis_ring.h -- v48: background OGG decoder thread + ready-PCM ring.
//
// Problem: stb_vorbis decode is heavy (~110ms per block on the ARM11, no VFP)
// and the mixer runs on the real-time audio path -- the decode can't live there.
//
// v48 splits the job:
//   - BelleVorbisRing + BelleVorbisDecoder (belle_main.cpp) run the stb_vorbis
//     decode on a DEDICATED NormalPriority thread, filling a ring of ready
//     16-bit-mono blocks AHEAD of playback;
//   - the mixer only memcpy()'s the next ready block out of the ring
//     (belle_vorbis_ring_consume) and never touches stb_vorbis.
//
// Ownership: vorbis.c owns the stb_vorbis instance and exposes the
// decoder-side API (vorbis_decoder_read/seek) that the thread pulls samples
// through. belle_main.cpp implements the ring + thread and the belle_vorbis_* hooks.

#ifndef __vorbis_ring_h__
#define __vorbis_ring_h__

#ifdef __cplusplus
extern "C" {
#endif

/* Ring of ready PCM blocks. Producer = the decoder thread (blocks while the
   ring is full); consumer = the mixer (NEVER blocks -- a short underrun just
   repeats the previous block). All ring state is guarded internally. */
void *belle_vorbis_ring_create(int slot_samples, int nslots);
void  belle_vorbis_ring_destroy(void *ring);

/* Producer side (decoder thread). Copies n samples into the next free slot,
   blocking while the ring is full. Returns 1 on success, 0 if aborted. */
int   belle_vorbis_ring_produce(void *ring, const short *samples, int n);
/* Producer side: no more data will come (EOF). Wakes a waiting consumer. */
void  belle_vorbis_ring_set_eof(void *ring);
/* Abort: wake a producer stuck on a full ring; ring is being torn down. */
void  belle_vorbis_ring_abort(void *ring);

/* Consumer side (mixer thread). Copies the next ready block into out (which
   must hold >= slot_samples shorts) and returns its sample count. Returns
   0 = ring empty AND decoder EOF (track over), -1 = ring empty (underrun). */
int   belle_vorbis_ring_consume(void *ring, short *out);

/* Spawn the decoder thread for 'dec' (a vorbis_data* from vorbis.c), filling
   'ring' until EOF (looping and seeking while 'loop' is non-zero). The thread
   is the ONLY user of vorbis_decoder_read/seek. */
void  belle_vorbis_spawn(void *dec, void *ring, int loop);
/* Abort + join the decoder thread for this ring (if any), then destroy the
   ring. Idempotent; safe to call on NULL. */
void  belle_vorbis_shutdown(void *ring);

/* Decoder-side API (implemented in vorbis.c): pulled by the decoder thread. */
int   vorbis_decoder_read(void *dec, short *out, int max_samples); /* 0 = EOF */
void  vorbis_decoder_seek(void *dec);

#ifdef __cplusplus
}
#endif

#endif /* __vorbis_ring_h__ */
