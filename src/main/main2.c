#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"

#define VI_MSG_SIZE 2
OSMesgQueue viMessageQ;
static OSMesg viMessages[VI_MSG_SIZE];

HVQM2Header hvqm_header __attribute__((aligned(16)));

static OSThread audThread;
static u64 audThreadStack[STACKSIZE / 8];
static OSThread vidThread;
static u64 vidThreadStack[STACKSIZE / 8];

extern void AudioMain(void *arg);
extern void VideoMain(void *arg);

u64 disptime_us = 0;

void Main(void *video) {
    int h_offset, v_offset; // Position of image display
    int screen_offset;      // Number of pixels from start of frame buffer to display position

    // Acquire retrace event
    osCreateMesgQueue(&viMessageQ, viMessages, VI_MSG_SIZE);
    osViSetEvent(&viMessageQ, 0, 1);

    init_dma();
    init_hvqm_task();

    // Initialize the frame buffer (clear buffer contents and status flag)
    // osViSwapBuffer(cfb[NUM_CFBs - 1]);

    // Fetch the HVQM2 header
    dma_copy(&hvqm_header, video, sizeof(HVQM2Header), NULL);

    u32 total_frames = load32(hvqm_header.total_frames);
    extern u32 usec_per_frame;
    usec_per_frame = load32(hvqm_header.usec_per_frame);
    u32 total_audio_records = load32(hvqm_header.total_audio_records);

    void *video_streamP = video + sizeof(HVQM2Header);
    extern u32 video_remain;
    video_remain = total_frames;

    void *audio_streamP = video + sizeof(HVQM2Header);
    u32 audio_remain = total_audio_records;


    AudThreadParams parms;
    if (total_audio_records != 0) {
        parms.streamp = audio_streamP;
        parms.remain = audio_remain;
        parms.samples_per_sec = hvqm_header.samples_per_sec;
        osCreateThread(&audThread, AUD_THREAD_ID, AudioMain, &parms, audThreadStack + STACKSIZE / 8,
                       AUD_PRIORITY);
        osStartThread(&audThread);
    }

    h_offset = (SCREEN_WD - hvqm_header.width) / 2;
    v_offset = (SCREEN_HT - hvqm_header.height) / 2;
    screen_offset = SCREEN_WD * v_offset + h_offset;

    // Setup the HVQM2 image decoder
    hvqm2SetupSP1(&hvqm_header, SCREEN_WD);
    init_video(&video_streamP, screen_offset);

    while (video_remain > 0) {
        process_video(&video_streamP);

        // osWritebackDCacheAll();

        show_next_frame();

        osRecvMesg(&viMessageQ, NULL, OS_MESG_BLOCK);
        // Go to the process for next frame
        disptime_us += usec_per_frame;
        --video_remain;
    }

    while (1) { ; }
}