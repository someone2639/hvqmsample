#include <ultra64.h>
#include "system.h"

/*
 * Data area for the HVQ microcode
 */
HVQM2Info hvq_spfifo[HVQ_SPFIFO_SIZE] ALIGNED(16);

/*
 * Buffer for RSP task yield
 */
u64 hvq_yieldbuf[HVQM2_YIELD_DATA_SIZE/8] ALIGNED(16);

/*
 * If 4:2:2 data will (also) be decoded:
 */
u16 hvqwork[(MAXWIDTH/8)*(MAXHEIGHT/4)*4] ALIGNED(16);

/*
 *  Buffer for video records (HVQM2 compressed data) read from 
 * the HVQM2 data.
 * (Note) Please locate at a 16byte aligned address with the spec file.
 */
u8 hvqbuf[HVQ_DATASIZE_MAX] ALIGNED(16);

/* PCM data buffer
 * 
 * (Note) pcmbuf[i] must have 8byte alignment.
 * Please set to an 8byte aligned address with the spec file.
 * (PCMBUF_SIZE is aligned with system.h)
 */
s16 pcmbuf[NUM_PCMBUFs][PCMBUF_SIZE] ALIGNED(64);

/*
 * Buffer for audio records (ADPCM data) read from the HVQM2 data.
 * (Note) Please locate at a 16byte aligned address with the spec file. 
 */
u8 adpcmbuf[AUDIO_RECORD_SIZE_MAX] ALIGNED(64);


/* Image frame buffer */
CFBPix cfb[NUM_CFBs][SCREEN_WD*SCREEN_HT] ALIGNED(64);

/* end */
