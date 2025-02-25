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
    int status;
    u16 format;
    CFBPix *cfb;
    CFBPix *drawbuf;
    u64 endtime_us;
} VideoRing;

void load_video_frame(void **streamp, VideoRing *vbuf);
void decode_video(VideoRing *vbuf);
extern u64 playtime_us, disptime_us;

VideoRing vbuffer[NUM_CFBs] = {
    {.next = &vbuffer[1], .prev = &vbuffer[NUM_CFBs - 1]},
    {.next = &vbuffer[2], .prev = &vbuffer[0]},
    {.next = &vbuffer[0], .prev = &vbuffer[1]},
};
VideoRing *currVBuf;

u32 video_remain = 0;
u32 usec_per_frame = 0;
u32 frames_elapsed = 0;

u32 video_playing() {
    return (playtime_us != 0) && (disptime_us != 0) && (frames_elapsed > NUM_CFBs);
}

void init_video(void **streamp, u32 offset) {
    for (int i = 0; i < NUM_CFBs; i++) {
        vbuffer[i].cfb = &cfb[i][0];
        bzero(cfb[i], sizeof(cfb[i]));
        vbuffer[i].drawbuf = &cfb[i][offset];
    }

    for (int i = 0; i < NUM_CFBs - 1; i++) {
        load_video_frame(streamp, &vbuffer[i]);
        decode_video(&vbuffer[i]);
    }

    osViSwapBuffer(vbuffer[0].cfb);

    disptime_us = 0;
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


// Loads the data required to decode a video frame
void load_video_frame(void **streamp, VideoRing *vbuf) {
    HVQM2Record record_header ALIGNED(16);
    // Get the next video record
    u32 record_size = get_record(&record_header, HVQM2_VIDEO, streamp);

    vbuf->format = load16(record_header.format);

    u32 starttime_us = disptime_us;

    u32 skipped_frames = 0;

    // Frameskip
    if (video_playing()) {
        // Only skip if 2 audio frames behind
        if (playtime_us > (starttime_us + (usec_per_frame * 2))) {
            // Skip only as far as needed to sync up again, or to the next keyframe.
            //  Whichever comes first.
            while (playtime_us > starttime_us) {
                skip_record(record_size, streamp);
                starttime_us += usec_per_frame;
                skipped_frames++;
                frames_elapsed++;
                record_size = get_record(&record_header, HVQM2_VIDEO, streamp);
                video_remain--;
                if (record_header.format == HVQM2_VIDEO_KEYFRAME) {
                    osSyncPrintf("(keyframed)\n");
                    break;
                }
                if (video_remain == 0) {
                    break;
                }
            }
            osSyncPrintf("(SKIPPED %d FRAMES)\n", skipped_frames);
        }
        if (video_remain == 0) {
            return;
        } else {
            vbuf->format = load16(record_header.format);
        }
    }
    load_record(record_size, HVQM2_VIDEO, hvqbuf, streamp);

    vbuf->endtime_us = starttime_us + usec_per_frame;
}

// Actually decodes the frame
void decode_video(VideoRing *vbuf) {
    // Decode the compressed image data and expand it in the frame buffer
    if (vbuf->format == HVQM2_VIDEO_HOLD) {
       // do nothing
        vbuf->endtime_us += usec_per_frame;
    } else {
        // Process first half in the CPU
        hvqtask.t.flags = 0;

        vbuf->status = hvqm2DecodeSP1(hvqbuf, vbuf->format, vbuf->drawbuf,
                                vbuf->prev->drawbuf, hvqwork,
                                &hvq_sparg, hvq_spfifo
                                );
        osWritebackDCacheAll();

        // Process last half in the RSP
        if (vbuf->status > 0) {
            osInvalDCache((void *) vbuf->cfb, SCREEN_WD * SCREEN_HT * sizeof(CFBPix));
            osSpTaskStart(&hvqtask);
            osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
        }
    }
}

// void hold_all_frames() {
//     for (int i = 0; i < NUM_CFBs; i++) {
//         vbuffer[i].endtime_us += usec_per_frame;
//     }
// }

void show_next_frame(void **streamp) {
    // if (video_playing()) {
    //     if (disptime_us > (playtime_us + (usec_per_frame * 10))) {
    //         // while (disptime_us > playtime_us) {
    //         osSyncPrintf("HOLDING V%llu A%llu\n", disptime_us, playtime_us);
    //         hold_all_frames();
    //         osYieldThread();
    //         // }
    //     }
    // }
    if (currVBuf->endtime_us <= disptime_us) {
        currVBuf = currVBuf->next;
        load_video_frame(streamp, currVBuf);
        decode_video(currVBuf);
        --video_remain;
        frames_elapsed++;
    }
    if (currVBuf->format != HVQM2_VIDEO_HOLD) {
        osViSwapBuffer(currVBuf->cfb);
    }
}

// Currently just a wrapper
void VideoMain(void **streamp) {
    show_next_frame(streamp);
}
