/*
 Copyright (C) 2009 Jonathon Fowler <jf@jonof.id.au>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 */

/**
 * OggVorbis source support for MultiVoc -- Symbian/Belle port.
 *
 * Drop-in replacement for the upstream vorbis.c that used libvorbisfile
 * (ov_open_callbacks/ov_read/ov_info). libvorbis is not available on the
 * Symbian GCCE toolchain, so the decoder here is stb_vorbis (public domain,
 * http://nothings.org/stb/stb_vorbis.c) compiled into this translation unit.
 * The MV_* interface and the streaming contract (block at a time through
 * voice->GetSound, native sample rate, multivoc does the resampling via
 * voice->RateScale) are identical to the original.
 *
 * One deliberate simplification: stb_vorbis_get_frame_short is always asked
 * for a single (mono) output channel -- stb downsamples stereo sources to
 * mono itself -- so the voice is always 16-bit mono at the source sample rate
 * and multivoc resamples it to MV_MixRate. This matches the mono DevSound
 * output of this port.
 */

#ifdef HAVE_VORBIS

/* GCCE ships no alloca.h; stb_vorbis uses alloca() for per-frame scratch
   when no alloc buffer is supplied. __builtin_alloca exists on GCC 4.4. */
#if defined(__SYMBIAN32__) && !defined(alloca)
#define alloca __builtin_alloca
#endif

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

#include <stdlib.h>
#include <string.h>
#include "pitch.h"
#include "multivoc.h"
#include "_multivc.h"
#include "asssys.h"
#include "assmisc.h"
#include "vorbis_ring.h"

/* Ring geometry. One slot is 4096 mono samples -- 0.093s at 44100 Hz, 0.186s
   at the v49 22050 Hz re-encodes. 32 slots = ~6s of ready music, absorbing
   even a multi-second scheduler starvation of the NormalPriority decoder
   thread. The mixer never blocks here; produce() polls, never CV-waits. */
#define VORBIS_BLOCK_SAMPLES  4096
#define VORBIS_RING_SLOTS     32

typedef struct {
   unsigned char * ptr;
   int length;
   int pos;

   stb_vorbis * vf;
   int channels;        /* source channels (1 or 2); always mixed to mono */

   /* v48: the decode moved OFF the mixer onto a background thread
      (BelleVorbisDecoder in belle_main.cpp). vd->block is now just the consumer's
      scratch -- belle_vorbis_ring_consume copies the next ready block into
      it. After MV_PlayLoopedVorbis, vd->vf is touched ONLY by the decoder
      thread. */
   void *ring;          /* BelleVorbisRing* (belle_main.cpp); 0 = not streaming */
   char block[0x2000];  /* 4096 mono shorts: one ring slot (consumer scratch) */
} vorbis_data;

/*---------------------------------------------------------------------
Function: MV_GetNextVorbisBlock

Controls playback of OggVorbis data
---------------------------------------------------------------------*/

static playbackstatus MV_GetNextVorbisBlock
(
 VoiceNode *voice
 )

{
   vorbis_data * vd = (vorbis_data *) voice->extra;
   int n;

   voice->Playing = TRUE;

   /* v48: the decode happens on the background decoder thread (belle_main.cpp);
      here we just copy the next ready block out of the ring -- a memcpy, not
      an stb_vorbis call, so the mixer's real-time path is never stalled. */
   n = belle_vorbis_ring_consume( vd->ring, (short *) vd->block );

   if (n == 0) {
      /* Ring drained AND the decoder hit EOF -- the track is over. The ring
         and thread are released HERE because MV_ServiceVoc removes a finished
         voice directly (LL_Remove/LL_Add) and never calls
         MV_ReleaseVorbisVoice. */
      belle_vorbis_shutdown( vd->ring );
      vd->ring = 0;
      voice->Playing = FALSE;
      return NoMoreData;
   }

   if (n < 0) {
      /* Ring drained but the decoder is still running -- an underrun. Repeat
         the last block (voice->sound/length are unchanged) instead of
         dropping the tune; with the ~6s ring this should be rare. */
      return KeepPlaying;
   }

   voice->position    = 0;
   voice->sound       = vd->block;
   voice->BlockLength = 0;
   voice->length      = n << 16;

   return KeepPlaying;
}


/*---------------------------------------------------------------------
Function: MV_PlayVorbis3D

Begin playback of sound data at specified angle and distance
from listener.
---------------------------------------------------------------------*/

int MV_PlayVorbis3D
(
 char *ptr,
 unsigned int ptrlength,
 int  pitchoffset,
 int  angle,
 int  distance,
 int  priority,
 unsigned int callbackval
 )

{
   int left;
   int right;
   int mid;
   int volume;
   int status;

   if ( !MV_Installed )
   {
      MV_SetErrorCode( MV_NotInstalled );
      return( MV_Error );
   }

   if ( distance < 0 )
   {
      distance  = -distance;
      angle    += MV_NumPanPositions / 2;
   }

   volume = MIX_VOLUME( distance );

   // Ensure angle is within 0 - 31
   angle &= MV_MaxPanPosition;

   left  = MV_PanTable[ angle ][ volume ].left;
   right = MV_PanTable[ angle ][ volume ].right;
   mid   = max( 0, 255 - distance );

   status = MV_PlayVorbis( ptr, ptrlength, pitchoffset, mid, left, right, priority,
                           callbackval );

   return( status );
}


/*---------------------------------------------------------------------
Function: MV_PlayVorbis

Begin playback of sound data with the given sound levels and
priority.
---------------------------------------------------------------------*/

int MV_PlayVorbis
(
 char *ptr,
 unsigned int ptrlength,
 int   pitchoffset,
 int   vol,
 int   left,
 int   right,
 int   priority,
 unsigned int callbackval
 )

{
   int status;

   status = MV_PlayLoopedVorbis( ptr, ptrlength, -1, -1, pitchoffset, vol, left, right,
                                 priority, callbackval );

   return( status );
}


/*---------------------------------------------------------------------
Function: MV_PlayLoopedVorbis

Begin playback of sound data with the given sound levels and
priority.
---------------------------------------------------------------------*/

int MV_PlayLoopedVorbis
(
 char *ptr,
 unsigned int ptrlength,
 int   loopstart,
 int   loopend,
 int   pitchoffset,
 int   vol,
 int   left,
 int   right,
 int   priority,
 unsigned int callbackval
 )

{
   VoiceNode   *voice;
   int          status;
   vorbis_data * vd = 0;
   stb_vorbis_info vi;
   int err = 0;

   (void)loopend;

   if ( !MV_Installed )
   {
      MV_SetErrorCode( MV_NotInstalled );
      return( MV_Error );
   }

   vd = (vorbis_data *) malloc( sizeof(vorbis_data) );
   if (!vd) {
      MV_SetErrorCode( MV_InvalidVorbisFile );
      return MV_Error;
   }

   memset(vd, 0, sizeof(vorbis_data));
   vd->ptr = (unsigned char *) ptr;
   vd->pos = 0;
   vd->length = (int)ptrlength;

   vd->vf = stb_vorbis_open_memory(vd->ptr, vd->length, &err, NULL);
   if (!vd->vf) {
      free(vd);
      MV_SetErrorCode( MV_InvalidVorbisFile );
      return MV_Error;
   }

   vi = stb_vorbis_get_info(vd->vf);
   if (vi.channels != 1 && vi.channels != 2) {
      stb_vorbis_close(vd->vf);
      free(vd);
      MV_SetErrorCode( MV_InvalidVorbisFile );
      return MV_Error;
   }
   vd->channels = vi.channels;

   // Request a voice from the voice pool
   voice = MV_AllocVoice( priority );
   if ( voice == NULL )
   {
      stb_vorbis_close(vd->vf);
      free(vd);
      MV_SetErrorCode( MV_NoVoices );
      return( MV_Error );
   }

   voice->wavetype    = Vorbis;
   voice->bits        = 16;
   voice->channels    = 1;              // always mixed to mono
   voice->extra       = (void *) vd;
   voice->GetSound    = MV_GetNextVorbisBlock;
   voice->NextBlock   = vd->block;
   voice->DemandFeed  = NULL;
   voice->LoopCount   = (loopstart >= 0 ? TRUE : FALSE);
   voice->BlockLength = 0;
   voice->PitchScale  = PITCH_GetScale( pitchoffset );
   voice->length      = 0;
   voice->next        = NULL;
   voice->prev        = NULL;
   voice->priority    = priority;
   voice->callbackval = callbackval;
   voice->LoopStart   = 0;
   voice->LoopEnd     = 0;
   voice->LoopSize    = 0;
   voice->Playing     = TRUE;
   voice->Paused      = FALSE;

   voice->SamplingRate = (unsigned)vi.sample_rate;
   voice->RateScale    = ( voice->SamplingRate * voice->PitchScale ) / MV_MixRate;
   voice->FixedPointBufferSize = ( voice->RateScale * MixBufferSize ) -
      voice->RateScale;
   MV_SetVoiceMixMode( voice );

   MV_SetVoiceVolume( voice, vol, left, right );

   /* v48: hand the decode to a background thread + ring so the mixer never
      runs stb_vorbis on its real-time path (~110ms soft-float decode on the
      ARM11). vd->vf is now used only by the decoder thread. */
   vd->ring = belle_vorbis_ring_create( VORBIS_BLOCK_SAMPLES, VORBIS_RING_SLOTS );
   if ( !vd->ring )
      {
      stb_vorbis_close( vd->vf );
      free( vd );
      MV_SetErrorCode( MV_NoVoices );
      return( MV_Error );
      }
   belle_vorbis_spawn( vd, vd->ring, (loopstart >= 0 ? TRUE : FALSE) );

   MV_PlayVoice( voice );

   return( voice->handle );
}


void MV_ReleaseVorbisVoice( VoiceNode * voice )
{
   vorbis_data * vd = (vorbis_data *) voice->extra;

   if (voice->wavetype != Vorbis) {
      return;
   }

   if (vd) {
      if (vd->ring) {
         /* abort + join the decoder thread for this ring, then free it */
         belle_vorbis_shutdown( vd->ring );
         vd->ring = 0;
      }
      if (vd->vf) {
         stb_vorbis_close( vd->vf );
         vd->vf = 0;
      }
      free( vd );
   }

   voice->extra = 0;
}


/*---------------------------------------------------------------------
Decoder-side API (v48). Called by the BelleVorbisDecoder thread in
belle_main.cpp. The vorbis_data* handed in is the one opened by
MV_PlayLoopedVorbis; only the decoder thread touches vd->vf after that
point, so no locking is needed here.
---------------------------------------------------------------------*/

int vorbis_decoder_read
(
 void * dec,
 short * out,
 int max_samples
 )

{
   vorbis_data * vd = (vorbis_data *) dec;
   short * buf = out;

   /* Always request mono output -- stb downsamples stereo to a single
      channel itself, matching this port's mono DevSound output. */
   return stb_vorbis_get_frame_short( vd->vf, 1, &buf, max_samples );
}


void vorbis_decoder_seek
(
 void * dec
 )

{
   vorbis_data * vd = (vorbis_data *) dec;

   stb_vorbis_seek_start( vd->vf );
}

#endif //HAVE_VORBIS
