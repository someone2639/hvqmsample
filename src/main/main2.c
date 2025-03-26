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

extern u64 disptime_us;

void panic() {
    while (1);
}

void verify_hvqm() {
    if (hvqm_header.max_sp_packets > (HVQ_SPFIFO_SIZE / sizeof(HVQM2Info))) {
        osSyncPrintf("HVQ_SPFIFO_SIZE must be at least %d\n", hvqm_header.max_sp_packets);
        panic();
    }

    if (hvqm_header.max_audio_record_size > (AUDIO_RECORD_SIZE_MAX)) {
        osSyncPrintf("AUDIO_RECORD_SIZE_MAX must be at least %d\n", hvqm_header.max_audio_record_size);
        panic();
    }

    if (hvqm_header.max_frame_size > HVQ_DATASIZE_MAX) {
        osSyncPrintf("HVQ_DATASIZE_MAX must be at least %d\n", hvqm_header.max_frame_size);
        panic();
    }
}

void Main(void *video) {
    int h_offset, v_offset; // Position of image display
    int screen_offset;      // Number of pixels from start of frame buffer to display position

    // Acquire retrace event
    osCreateMesgQueue(&viMessageQ, viMessages, VI_MSG_SIZE);
    osViSetEvent(&viMessageQ, 0, 1);

    init_dma();
    init_hvqm_task();
    init_controllers();

    // Initialize the frame buffer (clear buffer contents and status flag)
    osViSwapBuffer(cfb[NUM_CFBs - 1]);

    // Fetch the HVQM2 header
    dma_copy(&hvqm_header, video, sizeof(HVQM2Header), NULL);
    verify_hvqm();

    u32 total_frames = load32(hvqm_header.total_frames);
    extern u32 usec_per_frame;
    usec_per_frame = load32(hvqm_header.usec_per_frame);
    u32 total_audio_records = load32(hvqm_header.total_audio_records);

    void *video_streamP = video + sizeof(HVQM2Header);
    extern u32 video_remain;
    u32 vremain_base = video_remain = total_frames - NUM_CFBs;

    void *audio_streamP = video + sizeof(HVQM2Header);
    u32 audio_remain = total_audio_records;

    AudThreadParams parms;
    if (total_audio_records != 0) {
        parms.streamp = audio_streamP;
        parms.remain = audio_remain;
        parms.samples_per_sec = hvqm_header.samples_per_sec;
        parms.num_channels = hvqm_header.channels;
        osCreateThread(&audThread, AUD_THREAD_ID, AudioMain, &parms, audThreadStack + STACKSIZE / 8,
                       AUD_PRIORITY);
        osStartThread(&audThread);
    }

    osSyncPrintf("EXPECTED TIMES: %d %d\n", total_frames * usec_per_frame, total_audio_records * get_usec());

    h_offset = (SCREEN_WD - hvqm_header.width) / 2;
    v_offset = (SCREEN_HT - hvqm_header.height) / 2;
    screen_offset = SCREEN_WD * v_offset + h_offset;

    // Setup the HVQM2 image decoder
    while (1) {
        hvqm2SetupSP1(&hvqm_header, SCREEN_WD);
        init_video(&video_streamP, screen_offset);

        osSetTime(0);
        while (video_remain > 0) {
            osSyncPrintf("VREMAIN %d\n", video_remain);
            VideoMain(&video_streamP);

            disptime_us = OS_CYCLES_TO_USEC(osGetTime());
            osRecvMesg(&viMessageQ, NULL, OS_MESG_BLOCK);
        }
        reset_video(&video_streamP, video, vremain_base, screen_offset);
        disptime_us = 0;
    }

    while (1) { ; }
}