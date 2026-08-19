/*
 Copyright (C) 2026 Symbian Belle (CMMFDevSound) audio driver

 Native-MMF PCM driver for jfaudiolib/multivoc.

 The driver implements the SoundDriver_PCM_* slot contract exactly like
 driver_sdl.c, but the physical output is a native Symbian CMMFDevSound
 (media framework, -lmmfdevsound) created on the audio worker thread in
 belle_main.cpp. The mixer's service callback (MV_ServiceVoc, passed to
 BelleDrv_PCM_BeginPlayback) is NOT invoked here -- the audio worker mixes
 on demand inside DevSound's BufferToBeFilled; belle_audio_exec_cmds only
 dispatches open/start/stop/shutdown commands. See the comment block in
 belle_main.cpp.
*/

#include "driver_belle.h"
#include "belle_qtglue.h"   /* belle_audio_* hooks */

enum {
    BelleErr_Error   = -1,
    BelleErr_Ok      = 0,
    BelleErr_Uninitialised,
    BelleErr_OpenAudio
};

static int ErrorCode = BelleErr_Ok;
static int Initialised = 0;
static int Playing = 0;

static char *MixBuffer = 0;          /* = MV_MixBuffer[0] from BeginPlayback */
static int MixBufferSize = 0;        /* = MV_BufferSize (256 samples) */
static int MixBufferCount = 0;       /* = MV_NumberOfBuffers (16) */
static void (*MixCallBack)(void) = 0; /* = MV_ServiceVoc */

int BelleDrv_GetError(void)
{
    return ErrorCode;
}

const char *BelleDrv_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber) {
        case BelleErr_Ok:
            return "Belle audio ok.";
        case BelleErr_Uninitialised:
            return "Belle audio uninitialised.";
        case BelleErr_OpenAudio:
            return "Belle audio: error opening audio device.";
        default:
            return "Unknown Belle audio error code.";
    }
}

int BelleDrv_PCM_Init(int *mixrate, int *numchannels, int *samplebits, void *initdata)
{
    (void)initdata;

    if (Initialised) {
        BelleDrv_PCM_Shutdown();
    }

    /* belle_audio_open runs on the Qt main thread, picks a device-supported
       sample rate (falling back from the requested one if needed) and writes
       the ACTUAL rate/channels/bits back -- multivoc re-tunes to those via
       MV_SetMixMode. Returns 0 on success. */
    if (belle_audio_open(mixrate, numchannels, samplebits)) {
        ErrorCode = BelleErr_OpenAudio;
        return BelleErr_Error;
    }

    Initialised = 1;
    ErrorCode = BelleErr_Ok;
    return BelleErr_Ok;
}

void BelleDrv_PCM_Shutdown(void)
{
    if (!Initialised) {
        return;
    }

    if (Playing) {
        BelleDrv_PCM_StopPlayback();
    }

    belle_audio_shutdown();

    Initialised = 0;
    ErrorCode = BelleErr_Ok;
}

int BelleDrv_PCM_BeginPlayback(char *BufferStart, int BufferSize,
                               int NumDivisions, void (*CallBackFunc)(void))
{
    if (!Initialised) {
        ErrorCode = BelleErr_Uninitialised;
        return BelleErr_Error;
    }

    if (Playing) {
        BelleDrv_PCM_StopPlayback();
    }

    MixBuffer = BufferStart;
    MixBufferSize = BufferSize;
    MixBufferCount = NumDivisions;
    MixCallBack = CallBackFunc;

    // NO prime MixCallBack() here (driver_sdl.c did one). The mixer is pumped
    // by the audio worker (mix-on-demand inside BufferToBeFilled), and its
    // produced-block counter starts at zero on START; a prime call here would
    // mix one block the counter doesn't know about and desync the ring read
    // index in belle_main.cpp. MV_StartPlayback has already cleared the ring, so
    // the first worker mix simply overwrites silence.

    if (belle_audio_start(MixBuffer, MixBufferSize, MixBufferCount, MixCallBack)) {
        ErrorCode = BelleErr_OpenAudio;
        return BelleErr_Error;
    }

    Playing = 1;
    return BelleErr_Ok;
}

void BelleDrv_PCM_StopPlayback(void)
{
    if (!Playing) {
        return;
    }

    belle_audio_stop();

    Playing = 0;
}

void BelleDrv_PCM_Lock(void)
{
    belle_audio_lock();
}

void BelleDrv_PCM_Unlock(void)
{
    belle_audio_unlock();
}
