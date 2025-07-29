#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include "system.h"
#include "profiler.h"

OSMesgQueue spMesgQ;
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

void hvqm_drawHLE(void *buf);

// #define HVQM_DRAW(cfb) hvqm_drawHLE(cfb)
#define HVQM_DRAW(cfb) osViSwapBuffer(cfb)

u32 video_playing() {
    return (playtime_us != 0) && (disptime_us != 0) && (frames_elapsed > NUM_CFBs);
}

void init_video(void **streamp, u32 offset) {
    new_profiler("HVQM Part1 (CPU)");
    new_profiler("HVQM Part2 (RSP)");

    for (int i = 0; i < NUM_CFBs; i++) {
        vbuffer[i].cfb = &cfb[i][0];
        bzero(cfb[i], sizeof(cfb[i]));
        vbuffer[i].drawbuf = &cfb[i][offset];
        vbuffer[i].endtime_us = 0;
    }

    for (int i = 0; i < NUM_CFBs; i++) {
        load_video_frame(streamp, &vbuffer[i]);
        decode_video(&vbuffer[i]);
    }

    HVQM_DRAW(vbuffer[0].cfb);

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

    // A frame is scheduled for this many us past the start of the video
    u64 starttime_us = (frames_elapsed * usec_per_frame);

    if (starttime_us > usec_per_frame) {
        starttime_us -= usec_per_frame;
    }

    u32 skipped_frames = 0;

    // Frameskip
    if (video_playing()) {
        // Only skip if 2 audio frames behind
        if (playtime_us > (starttime_us + (usec_per_frame * 20))) {
            // Skip only as far as needed to sync up again, or to the next keyframe.
            //  Whichever comes first.
            while (playtime_us > starttime_us) {
                // if (record_header.format == HVQM2_VIDEO_KEYFRAME) {
                //     // load this keyframe because we need this info
                //     load_record(record_size, HVQM2_VIDEO, hvqbuf, streamp);
                //     decode_video(currVBuf);
                // } else {
                    skip_record(record_size, streamp);
                // }
                // load_record(record_size, HVQM2_VIDEO, hvqbuf, streamp);
                starttime_us += usec_per_frame;
                skipped_frames++;
                frames_elapsed++;
                record_size = get_record(&record_header, HVQM2_VIDEO, streamp);
                video_remain--;
                if (record_header.format == HVQM2_VIDEO_KEYFRAME) {
                    osSyncPrintf("(keyframed)\n");
                    // skup further if we're REALLY far behind
                    if (playtime_us > (starttime_us + (usec_per_frame * 2))) {
                        osSyncPrintf("(we're REALLY far behind)\n");
                        continue;
                    } else {
                        break;
                    }
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

    vbuf->endtime_us = starttime_us;
}

// Actually decodes the frame
void decode_video(VideoRing *vbuf) {
    // Decode the compressed image data and expand it in the frame buffer
    if (vbuf->format == HVQM2_VIDEO_HOLD) {
       // do nothing
        vbuf->endtime_us += usec_per_frame;
        osSyncPrintf("This is a hold frame");
    } else {
        // Process first half in the CPU
        hvqtask.t.flags = 0;

        start_profiler("HVQM Part1 (CPU)");
        vbuf->status = hvqm2DecodeSP1(hvqbuf, vbuf->format, vbuf->drawbuf,
                                vbuf->prev->drawbuf, hvqwork,
                                &hvq_sparg, hvq_spfifo
                                );
        end_profiler("HVQM Part1 (CPU)");
        if (vbuf->format == HVQM2_VIDEO_KEYFRAME) {
            tag_profiler("HVQM Part1 (CPU)", "HVQM2_VIDEO_KEYFRAME");
        } else {
            tag_profiler("HVQM Part1 (CPU)", "HVQM2_VIDEO_PREDICT");
        }
        print_profiler("HVQM Part1 (CPU)");
        osWritebackDCacheAll();

        // Process last half in the RSP
        if (vbuf->status > 0) {
            osInvalDCache((void *) vbuf->cfb, SCREEN_WD * SCREEN_HT * sizeof(CFBPix));
            // if (wait)start_profiler("HVQM Part2 (RSP)");
            osSpTaskStart(&hvqtask);
            osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
        }
    }
}

void hold_all_frames(u32 count) {
    for (int i = 0; i < NUM_CFBs; i++) {
        vbuffer[i].endtime_us += (usec_per_frame * count);
    }
}

void show_next_frame(void **streamp) {
    if (video_playing()) {
        if (disptime_us > (playtime_us + (usec_per_frame * 10))) {
            u32 count = (disptime_us - playtime_us) / (usec_per_frame);
            osSyncPrintf("HOLD %d\n", count);
            hold_all_frames(count);
            osYieldThread();
        }
    }
    if (currVBuf->endtime_us <= playtime_us) {
        currVBuf = currVBuf->next;
        load_video_frame(streamp, currVBuf);
        decode_video(currVBuf);
        video_remain--;
        frames_elapsed++;
    }
    if (currVBuf->format != HVQM2_VIDEO_HOLD) {
        HVQM_DRAW(currVBuf->cfb);
        osYieldThread();
    }
}

void reset_video(void **streamp, u32 remainbase, u32 offset) {
    video_remain = remainbase;
    frames_elapsed = 0;
    osWritebackDCacheAll();
    init_video(streamp, offset);
    disptime_us = 0;
}

// Currently just a wrapper
void VideoMain(void **streamp) {
    show_next_frame(streamp);

    if (video_remain == 0) {
        fault_setcfb(currVBuf->cfb);
        crash_screen_draw_rect(0, 0, 200, 50);
        fault_get_fps_vals("HVQM Part1 (CPU)", "HVQM Part2 (RSP)");
        HVQM_DRAW(currVBuf->cfb);
    }
}
