#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include "system.h"

static OSMesgQueue spMesgQ;
static OSMesg spMesgBuf;

OSTask hvqtask;     // RSP task data
HVQM2Arg hvq_sparg; // Parameter for the HVQM2 microcode

typedef struct VideoRing {
    struct VideoRing *next;
    struct VideoRing *prev;
    // HVQM2Arg arg;
    int status;
    u16 format;
    CFBPix *cfb;
    CFBPix *drawbuf;
    u64 starttime_us;
    u64 endtime_us;
} VideoRing;

void process_video(void **streamp);
extern u64 playtime_us, disptime_us;

VideoRing vbuffer[NUM_CFBs] = {
    {.next = &vbuffer[1], .prev = &vbuffer[NUM_CFBs - 1]},
    {.next = &vbuffer[2], .prev = &vbuffer[0]},
    {.next = &vbuffer[0], .prev = &vbuffer[1]},
};
VideoRing *currVBuf;

u32 video_remain = 0;
u32 usec_per_frame = 0;

void init_video(void **streamp, u32 offset) {
    for (int i = 0; i < NUM_CFBs; i++) {
        vbuffer[i].cfb = &cfb[i][0];
        bzero(cfb[i], sizeof(cfb[i]));
        vbuffer[i].drawbuf = &cfb[i][offset];
        currVBuf = &vbuffer[i];
        process_video(streamp);
    }

    currVBuf = &vbuffer[0];
}

void init_hvqm_task() {
    // Initialize the HVQM2 decoder
    // If using the RSP version of the decoder
    // also setup the RSP task data next
    hvqm2InitSP1(0xff);

    // Acquire an SP event (if using the RSP version of the decoder)
    osCreateMesgQueue(&spMesgQ, &spMesgBuf, 1);
    osSetEventMesg(OS_EVENT_SP, &spMesgQ, NULL);

    hvqtask.t.ucode = (u64 *) hvqm2sp1TextStart;
    hvqtask.t.ucode_size = (int) hvqm2sp1TextEnd - (int) hvqm2sp1TextStart;
    hvqtask.t.ucode_data = (u64 *) hvqm2sp1DataStart;
    hvqtask.t.type = M_HVQM2TASK;
    hvqtask.t.flags = 0;
    hvqtask.t.ucode_boot = (u64 *) rspbootTextStart;
    hvqtask.t.ucode_boot_size = (int) rspbootTextEnd - (int) rspbootTextStart;
    hvqtask.t.ucode_data_size = HVQM2_UCODE_DATA_SIZE;
    hvqtask.t.data_ptr = (u64 *) &hvq_sparg;
    hvqtask.t.yield_data_ptr = (u64 *) hvq_yieldbuf;
    hvqtask.t.yield_data_size = HVQM2_YIELD_DATA_SIZE;
}

void process_video(void **streamp) {
    HVQM2Record record_header ALIGNED(16);
    /*
     * Fetch video record
     */
    get_record(&record_header, hvqbuf, HVQM2_VIDEO, streamp);

    currVBuf->format = load16(record_header.format);
    currVBuf->starttime_us = disptime_us - usec_per_frame;
    // frameskip
    if (playtime_us != 0 && disptime_us != 0) {
        while (playtime_us > (disptime_us + (usec_per_frame * 2))) {
            osSyncPrintf("(FRAMESKIP %lld)\n", disptime_us);
            disptime_us += usec_per_frame;
            get_record(&record_header, hvqbuf, HVQM2_VIDEO, streamp);
            video_remain--;
            if (record_header.format == HVQM2_VIDEO_KEYFRAME) {
                break;
            }
            if (video_remain == 0) {
                break;
            }
        }
        if (video_remain == 0) {
            return;
        } else {
            currVBuf->format = load16(record_header.format);
        }
    }

    currVBuf->endtime_us = disptime_us;


    // Decode the compressed image data and expand it in the frame buffer
    if (currVBuf->format == HVQM2_VIDEO_HOLD) {
       // do nothing
    } else {
        // Process first half in the CPU
        hvqtask.t.flags = 0;

        currVBuf->status = hvqm2DecodeSP1(hvqbuf, currVBuf->format, currVBuf->drawbuf,
                                currVBuf->prev->drawbuf, hvqwork,
                                &hvq_sparg, hvq_spfifo
                                );
        osWritebackDCacheAll();

        // Process last half in the RSP
        if (currVBuf->status > 0) {
            osInvalDCache((void *) currVBuf->cfb, SCREEN_WD * SCREEN_HT * sizeof(CFBPix));
            osSpTaskStart(&hvqtask);
            osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
        }
    }
}

void VideoMain(void *arg) {

}


void show_next_frame() {
    if (currVBuf->format != HVQM2_VIDEO_HOLD) {
        osViSwapBuffer(currVBuf->cfb);
    }
    if (currVBuf->endtime_us <= disptime_us) {
        currVBuf = currVBuf->next;
    }
}
