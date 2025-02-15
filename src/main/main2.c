#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"
#include "timekeeper.h"

OSTask hvqtask;
static OSMesgQueue spMesgQ;
static OSMesg spMesgBuf;
#define VIDEO_DMA_MSG_SIZE 1
static OSIoMesg videoDmaMesgBlock;
static OSMesgQueue videoDmaMessageQ;
static OSMesg videoDmaMessages[VIDEO_DMA_MSG_SIZE];

OSTask hvqtask;     /* RSP task data */
HVQM2Arg hvq_sparg; /* Parameter for the HVQM2 microcode */

u8 hvqm_headerBuf[sizeof(HVQM2Header) + 16];

u8 *get_record(HVQM2Record *headerbuf, void *bodybuf, u16 type, u8 *stream, OSIoMesg *mb,
               OSMesgQueue *mq) {
    u16 record_type;
    u32 record_size;
    s32 pri;

    pri = (type == HVQM2_AUDIO) ? OS_MESG_PRI_HIGH : OS_MESG_PRI_NORMAL;
    for (;;) {
        romcpy(headerbuf, stream, sizeof(HVQM2Record), pri, mb, mq);
        stream += sizeof(HVQM2Record);
        record_type = load16(headerbuf->type);
        record_size = load32(headerbuf->size);
        osSyncPrintf("RECORD {%d %08X},\n", record_type, record_size);
        if (record_type == type)
            break;
        stream += record_size;
    }

    if (record_size > 0) {
        romcpy(bodybuf, stream, record_size, pri, mb, mq);
        stream += record_size;
    }
    return stream;
}

void Main(void *argument) {
    HVQM2Header *hvqm_header;
    int h_offset, v_offset; /* Position of image display */
    int screen_offset;      /* Number of pixels from start of frame buffer to display position */

    hvqm_header = OS_DCACHE_ROUNDUP_ADDR(hvqm_headerBuf);

    /*
     * Acquire an SP event (if using the RSP version of the decoder)  */
    osCreateMesgQueue(&spMesgQ, &spMesgBuf, 1);
    osSetEventMesg(OS_EVENT_SP, &spMesgQ, NULL);
    /*
     * Create DMA message queue for the reading in of video records
     */
    osCreateMesgQueue(&videoDmaMessageQ, videoDmaMessages, VIDEO_DMA_MSG_SIZE);

    /*
     * Create the timekeeper thread
     */
    // createTimekeeper();

    /*
     * Initialize the HVQM2 decoder
     */
    /* If using the RSP version of the decoder */
    /* also setup the RSP task data next */
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


    /*
     * Initialize the frame buffer (clear buffer contents and status flag)
     */
    init_cfb();
    osViSwapBuffer(cfb[NUM_CFBs - 1]);

    /*
     * Fetch the HVQM2 header
     */
    romcpy(hvqm_header, _hvqmdataSegmentRomStart, sizeof(HVQM2Header), OS_MESG_PRI_NORMAL,
           &videoDmaMesgBlock, &videoDmaMessageQ);

    /*
     * Print the HVQM2 header information to the debugger
     */
    // print_hvqm_info(hvqm_header);

    u32 total_frames = load32(hvqm_header->total_frames);
    u32 usec_per_frame = load32(hvqm_header->usec_per_frame);
    u32 total_audio_records = load32(hvqm_header->total_audio_records);

    /*
     * Determine video display position
     * (adjust offset so a small image is expanded in the center of the
     *  frame buffer)
     */
    h_offset = (SCREEN_WD - hvqm_header->width) / 2;
    v_offset = (SCREEN_HT - hvqm_header->height) / 2;
    screen_offset = SCREEN_WD * v_offset + h_offset;

    /*
     * Setup the HVQM2 image decoder
     */
    hvqm2SetupSP1(hvqm_header, SCREEN_WD);

    /*
     * Repetitive playback loop
     */
    int prev_bufno = -1;
    u32 video_remain = total_frames;
    u64 disptime = 0;
    void *video_streamP = _hvqmdataSegmentRomStart + sizeof(HVQM2Header);
    while (video_remain > 0) {
        osSyncPrintf("vremain %d\n", video_remain);

        /*
         * Release all frame buffers
         */
        release_all_cfb();


        u8 header_buffer[sizeof(HVQM2Record) + 16];
        HVQM2Record *record_header;
        u16 frame_format;
        int bufno;

        /*
         * Fetch video record
         */
        record_header = OS_DCACHE_ROUNDUP_ADDR(header_buffer);
        video_streamP = get_record(record_header, hvqbuf, HVQM2_VIDEO, video_streamP,
                                   &videoDmaMesgBlock, &videoDmaMessageQ);

        {
            /*
             *   This block is an example of how to force the video to play
             * in sync with the audio.
             *
             *   The video and audio is synchronized and played back by the
             * timekeeper thread, but this assumes the video (frame buffer)
             * is always completed and sent to the timekeeper thread before
             * its scheduled display time.
             *
             *   But with mixed I/O the compressed data can be read late and
             * decoding can take more time due to the increased burden on the
             * CPU, leading to a situation where the video can become delayed
             * relative to the audio.
             *
             *   One way to counter this is to skip to the next keyframe
             * whenever the frame to be decoded is late by an amount of
             * time equal to 2 or more frames.
             *
             */
            if (disptime > 0) { /* Excluding the first frame */
                if (tkGetTime() > (disptime + (usec_per_frame * 2))) {
                    release_all_cfb();
                    do {
                        disptime += usec_per_frame;
                        if (--video_remain == 0)
                            break;
                        video_streamP =
                            get_record(record_header, hvqbuf, HVQM2_VIDEO, video_streamP,
                                       &videoDmaMesgBlock, &videoDmaMessageQ);
                    } while (load16(record_header->format) != HVQM2_VIDEO_KEYFRAME
                             || tkGetTime() > disptime);
                    if (video_remain == 0)
                        break;
                }
            }
        }

        /*
         * Decode the compressed image data and expand it in the frame buffer
         */
        frame_format = load16(record_header->format);

        if (frame_format == HVQM2_VIDEO_HOLD) {
            /*
             *   Just like when frame_format != HVQM2_VIDEO_HOLD you
             * could call hvqm2Decode*() and decode in a new frame
             * buffer (in this case, just copying from the buffer of
             * the preceding frame).  But here we make use of the
             * preceding frame's buffer for the next frame in order
             * to speed up the process.
             */
        } else {
            // bufno = get_cfb(); /* Get the frame buffer */

            /* If using the RSP version of the decoder */
            {
                int status;

                /*
                 * Process first half in the CPU
                 */
                hvqtask.t.flags = 0;
                status = hvqm2DecodeSP1(hvqbuf, frame_format, &cfb[bufno][screen_offset],
                                        &cfb[prev_bufno][screen_offset], hvqwork, &hvq_sparg,
                                        hvq_spfifo);
                osWritebackDCacheAll();

                /*
                 * Process last half in the RSP
                 */
                if (status > 0) {
                    osInvalDCache((void *) cfb[bufno], sizeof cfb[bufno]);
                    osSpTaskStart(&hvqtask);
                    osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
                }
            }
        }
        /*
         * Don't need the previous frame anymore, so release it
         */
        osViSwapBuffer(cfb[bufno]);
        
        if (frame_format != HVQM2_VIDEO_HOLD) {
            prev_bufno = bufno++;
            if (bufno >= NUM_CFBs) {
                bufno = 0;
            }
        }

        /*
         * Go to the process for next frame
         */
        disptime += usec_per_frame;
        --video_remain;
    }

    osSyncPrintf("PLAYBACK COMPLETE\n");

    while (1) { ; }
}