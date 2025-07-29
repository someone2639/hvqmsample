#include <ultra64.h>
#include "system.h"

/*
 * Data area for the HVQ microcode
 */
HVQM2Info hvq_spfifo[HVQ_SPFIFO_SIZE] ALIGNED(0x100);

/*
 * Buffer for RSP task yield
 */
u64 hvq_yieldbuf[NUM_CFBs][HVQM2_YIELD_DATA_SIZE/8] ALIGNED(0x100);

// suitable for bth 4:2:2 and 4:2:1 data
u16 hvqwork[NUM_CFBs][(MAXWIDTH/8)*(MAXHEIGHT/4)*4] ALIGNED(0x100);
/* Image frame buffer */
CFBPix cfb[NUM_CFBs][SCREEN_WD*SCREEN_HT] ALIGNED(0x100);

/*
 *  Buffer for video records (HVQM2 compressed data) read from 
 * the HVQM2 data.
 * (Note) Please locate at a 16byte aligned address with the spec file.
 */
u8 hvqbuf[NUM_CFBs][HVQ_DATASIZE_MAX] ALIGNED(0x100);

u64 system_rdpfifo[RDPFIFO_SIZE] ALIGNED(0x40);
u64 system_rspyield[OS_YIELD_DATA_SIZE / sizeof(u64)] ALIGNED(0x40);


/* end */
