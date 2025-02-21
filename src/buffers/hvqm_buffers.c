#include <ultra64.h>
#include "system.h"

/*
 * Data area for the HVQ microcode
 */
HVQM2Info hvq_spfifo[HVQ_SPFIFO_SIZE] ALIGNED(0x100);

/*
 * Buffer for RSP task yield
 */
u64 hvq_yieldbuf[HVQM2_YIELD_DATA_SIZE/8] ALIGNED(0x100);

// suitable for bth 4:2:2 and 4:2:1 data
u16 hvqwork[(MAXWIDTH/8)*(MAXHEIGHT/4)*4] ALIGNED(0x100);
/* Image frame buffer */
CFBPix cfb[NUM_CFBs][SCREEN_WD*SCREEN_HT] ALIGNED(0x100);

/*
 *  Buffer for video records (HVQM2 compressed data) read from 
 * the HVQM2 data.
 * (Note) Please locate at a 16byte aligned address with the spec file.
 */
u8 hvqbuf[HVQ_DATASIZE_MAX] ALIGNED(0x100);


/* end */
