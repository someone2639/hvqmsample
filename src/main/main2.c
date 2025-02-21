#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"

OSTask hvqtask;
static OSMesgQueue spMesgQ;
static OSMesg spMesgBuf;

#define VI_MSG_SIZE 2
OSMesgQueue viMessageQ;
static OSMesg viMessages[VI_MSG_SIZE];

OSTask hvqtask;     // RSP task data
HVQM2Arg hvq_sparg; // Parameter for the HVQM2 microcode

HVQM2Header hvqm_header __attribute__((aligned(16)));

static OSThread audThread;
static u64 audThreadStack[STACKSIZE / 8];

extern void AudioMain(void *arg);

char *htable[] = {
    [HVQM2_AUDIO] = "AUDIO",
    [HVQM2_VIDEO] = "VIDEO",
};

u64 disptime_us = 0;

void Main(void *video) {
    int h_offset, v_offset; // Position of image display
    int screen_offset;      // Number of pixels from start of frame buffer to display position

    // Acquire an SP event (if using the RSP version of the decoder)
    osCreateMesgQueue(&spMesgQ, &spMesgBuf, 1);
    osSetEventMesg(OS_EVENT_SP, &spMesgQ, NULL);

    // Acquire retrace event
    osCreateMesgQueue(&viMessageQ, viMessages, VI_MSG_SIZE);
    osViSetEvent(&viMessageQ, 0, 1);

    init_dma();

    // Initialize the HVQM2 decoder
    // If using the RSP version of the decoder
    // also setup the RSP task data next
    hvqm2InitSP1(0xff);
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


    // Initialize the frame buffer (clear buffer contents and status flag)
    // osViSwapBuffer(cfb[NUM_CFBs - 1]);

    // Fetch the HVQM2 header
    dma_copy(&hvqm_header, video, sizeof(HVQM2Header));

    u32 total_frames = load32(hvqm_header.total_frames);
    u32 usec_per_frame = load32(hvqm_header.usec_per_frame);
    u32 total_audio_records = load32(hvqm_header.total_audio_records);

    void *video_streamP = video + sizeof(HVQM2Header);
    u32 video_remain = total_frames;

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

    /*
     * Determine video display position
     * (adjust offset so a small image is expanded in the center of the
     *  frame buffer)
     */
    h_offset = (SCREEN_WD - hvqm_header.width) / 2;
    v_offset = (SCREEN_HT - hvqm_header.height) / 2;
    screen_offset = SCREEN_WD * v_offset + h_offset;

    // Setup the HVQM2 image decoder
    hvqm2SetupSP1(&hvqm_header, SCREEN_WD);

    // Repetitive playback loop
    int prev_bufno = -1;
    int bufno = 0;

    int i = 0;

    while (video_remain > 0) {
        // osSyncPrintf("vremain %d\n", video_remain);

        u8 header_buffer[sizeof(HVQM2Record) + 16] ALIGNED(16);
        HVQM2Record *record_header = header_buffer;
        u16 frame_format;

        /*
         * Fetch video record
         */
        video_streamP = get_record(record_header, hvqbuf, HVQM2_VIDEO, video_streamP);

        // frameskip
        extern u64 playtime_us;
        // osSyncPrintf("(DISPTIME %lld)\n", disptime_us);
        if (playtime_us != 0 && disptime_us != 0) {
            while (playtime_us > (disptime_us + (usec_per_frame * 2))) {
                osSyncPrintf("(FRAMESKIP %lld)\n", disptime_us);
                disptime_us += usec_per_frame;
                video_streamP = get_record(record_header, hvqbuf, HVQM2_VIDEO, video_streamP);
                video_remain--;
                i++;
                if (record_header->format == HVQM2_VIDEO_KEYFRAME) {
                    break;
                }
                if (video_remain == 0) {
                    break;
                }
            }
            if (video_remain == 0) {
                break;
            }
        }

        // Decode the compressed image data and expand it in the frame buffer
        frame_format = load16(record_header->format);

        if (frame_format == HVQM2_VIDEO_HOLD) {
           // do nothing
        } else {
            int status;
            // Process first half in the CPU
            hvqtask.t.flags = 0;

            status = hvqm2DecodeSP1(hvqbuf, frame_format, &cfb[bufno][screen_offset],
                                    &cfb[prev_bufno][screen_offset], hvqwork
                                    , &hvq_sparg,
                                    hvq_spfifo
                                    );
            osWritebackDCacheAll();

            // Process last half in the RSP
            if (status > 0) {
                osInvalDCache((void *) cfb[bufno], sizeof(cfb[bufno]));
                osSpTaskStart(&hvqtask);
                osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
            }
        }

        osWritebackDCacheAll();

        if (frame_format != HVQM2_VIDEO_HOLD) {
            osViSwapBuffer(cfb[bufno]);
        
            prev_bufno = bufno++;
            if (bufno >= NUM_CFBs) {
                bufno = 0;
            }
        }

        osRecvMesg(&viMessageQ, NULL, OS_MESG_BLOCK);
        // Go to the process for next frame
        disptime_us += usec_per_frame;
        --video_remain;
        i++;
    }

    osSyncPrintf("VID PLAYBACK COMPLETE %d frames\n", i);

    while (1) { ; }
}