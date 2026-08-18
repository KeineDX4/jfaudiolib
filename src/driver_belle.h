/*
 Copyright (C) 2026 JFSW -> Symbian Belle port

 Sound output driver for the Symbian Belle (CMMFDevSound) target.
 The PCM half of this driver is wired into jfaudiolib's SoundDrivers[] table
 (drivers.c) as ASS_Belle; the actual audio device lives on the Qt main
 thread in belle_main.cpp, reached through the belle_audio_* extern "C" hooks.
*/

#ifndef DRIVER_BELLE_H
#define DRIVER_BELLE_H

int  BelleDrv_GetError(void);
const char *BelleDrv_ErrorString(int ErrorNumber);

int  BelleDrv_PCM_Init(int *mixrate, int *numchannels, int *samplebits, void *initdata);
void BelleDrv_PCM_Shutdown(void);
int  BelleDrv_PCM_BeginPlayback(char *BufferStart, int BufferSize,
                                int NumDivisions, void (*CallBackFunc)(void));
void BelleDrv_PCM_StopPlayback(void);
void BelleDrv_PCM_Lock(void);
void BelleDrv_PCM_Unlock(void);

#endif /* DRIVER_BELLE_H */
