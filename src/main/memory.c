#include <ultra64.h>
#include <HVQM2File.h>
#include "system.h"

OSPiHandle *cartrom_hd;

#define DMA_MSG_SIZE 1
static OSIoMesg dmaIOMesg;
static OSIoMesg audioIOMesg;

static OSMesgQueue dmaMessageQ;
static OSMesg dmaMessages[DMA_MSG_SIZE];
static OSMesgQueue audDMAMessageQ;
static OSMesg audDMAMessages[DMA_MSG_SIZE];


void init_dma() {
    osCreateMesgQueue(&dmaMessageQ, dmaMessages, DMA_MSG_SIZE);
    osCreateMesgQueue(&audDMAMessageQ, audDMAMessages, DMA_MSG_SIZE);
    cartrom_hd = osCartRomInit();

    dmaIOMesg.hdr.pri = OS_MESG_PRI_NORMAL;
    dmaIOMesg.hdr.retQueue = &dmaMessageQ;

    audioIOMesg.hdr.pri = OS_MESG_PRI_HIGH;
    audioIOMesg.hdr.retQueue = &audDMAMessageQ;
}

void get_record(HVQM2Record *headerbuf, void *bodybuf, u16 type, void **stream) {
    u16 record_type;
    u32 record_size;
    OSIoMesg *mb;

    mb = (type == HVQM2_AUDIO) ? &audioIOMesg : &dmaIOMesg;
    for (;;) {
        dma_copy(headerbuf, *stream, sizeof(HVQM2Record), mb);
        *stream += sizeof(HVQM2Record);
        record_type = load16(headerbuf->type);
        record_size = load32(headerbuf->size);
        if (record_type == type)
            break;
        *stream += record_size;
    }

    if (record_size > 0) {
        dma_copy(bodybuf, *stream, record_size, mb);
        *stream += record_size;
    }
}

void dma_copy(void *dest, void *src, u32 len, OSIoMesg *msg) {
    // osSyncPrintf("    [ROMCPY] %08X <- [%08X, %08X]\n", dest, src, len);
    bzero(dest, len);
    osInvalDCache(dest, len); 

    if (msg == NULL) msg = &dmaIOMesg;

    msg->dramAddr = dest;
    msg->devAddr = (u32)src;
    msg->size = (u32)len;

    // Start the DMA
    osEPiStartDma(cartrom_hd, msg, OS_READ);
    osRecvMesg(msg->hdr.retQueue, NULL, OS_MESG_BLOCK);
}
