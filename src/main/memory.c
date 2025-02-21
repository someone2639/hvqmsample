#include <ultra64.h>
#include <HVQM2File.h>
#include "system.h"

OSPiHandle *cartrom_hd;

#define DMA_MSG_SIZE 3
static OSIoMesg dmaIOMesg;
static OSIoMesg audioIOMesg;
static OSMesgQueue dmaMessageQ;
static OSMesg dmaMessages[DMA_MSG_SIZE];

void init_dma() {
    osCreateMesgQueue(&dmaMessageQ, dmaMessages, DMA_MSG_SIZE);
    cartrom_hd = osCartRomInit();
}

u8 *get_record(HVQM2Record *headerbuf, void *bodybuf, u16 type, u8 *stream) {
    u16 record_type;
    u32 record_size;
    s32 pri;

    pri = (type == HVQM2_AUDIO) ? OS_MESG_PRI_HIGH : OS_MESG_PRI_NORMAL;
    for (;;) {
        dma_copy(headerbuf, stream, sizeof(HVQM2Record));
        stream += sizeof(HVQM2Record);
        record_type = load16(headerbuf->type);
        record_size = load32(headerbuf->size);
        if (record_type == type)
            break;
        stream += record_size;
    }

    if (record_size > 0) {
        dma_copy(bodybuf, stream, record_size);
        stream += record_size;
    }
    return stream;
}

void dma_copy(void *dest, void *src, u32 len) {
    osSyncPrintf("    [ROMCPY] %08X <- [%08X, %08X]\n", dest, src, len);
    // Zero out the region being DMA'd to
    bzero(dest, len);
    // Invalidate the data cache for the region being DMA'd to
    osInvalDCache(dest, len); 

    // Set up the intro segment DMA
    dmaIOMesg.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIOMesg.hdr.retQueue = &dmaMessageQ;
    dmaIOMesg.dramAddr = dest;
    dmaIOMesg.devAddr = (u32)src;
    dmaIOMesg.size = (u32)len;

    // Start the DMA
    osEPiStartDma(cartrom_hd, &dmaIOMesg, OS_READ);
    osRecvMesg(&dmaMessageQ, NULL, OS_MESG_BLOCK);
}
