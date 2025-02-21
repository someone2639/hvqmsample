#include <ultra64.h>
#include "system.h"

/*
 * Buffer for audio records (ADPCM data) read from the HVQM2 data.
 * (Note) Please locate at a 16byte aligned address with the spec file. 
 */
u8 adpcmbuf[AUDIO_RECORD_SIZE_MAX] ALIGNED(0x100);


/* PCM data buffer
 * 
 * (Note) pcmbuf[i] must have 8byte alignment.
 * Please set to an 8byte aligned address with the spec file.
 * (PCMBUF_SIZE is aligned with system.h)
 */
s16 pcmbuf[NUM_PCMBUFs][PCMBUF_SIZE] ALIGNED(0x100);
