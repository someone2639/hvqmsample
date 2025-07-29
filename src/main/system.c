/*
 *  N64-HVQM2 library  Sample program
 *
 *  FILE : system.c (boot program/system utility)
 *
 *  Copyright (C) 1998,1999 NINTENDO Co.,Ltd.
 *
 */

/* 1998-12-15 */

#include <ultra64.h>
#include "system.h"

/***********************************************************************
 * Boot code stack
 ***********************************************************************/
u64 bootStack[STACKSIZE / 8];

/***********************************************************************
 * Idle thread
 ***********************************************************************/
static OSThread idleThread;
static u64 idleThreadStack[STACKSIZE / 8];

/***********************************************************************
 * Main thread
 ***********************************************************************/
static OSThread mainThread;
static u64 mainThreadStack[STACKSIZE / 8];

/***********************************************************************
 * PI command message queue
 ***********************************************************************/
static OSMesgQueue PiMessageQ;
static OSMesg PiMessages[PI_COMMAND_QUEUE_SIZE];

OSThread hvqmThread;
static u64 hvqmThreadStack[STACKSIZE / 8];

u32 gfxselect = 0;
Gfx gfxbuf[NUM_CFBs][1000];
Gfx *video_glistp;

extern OSMesgQueue spMesgQ;

static void select_gfx_pool() {
    video_glistp = gfxbuf[++gfxselect];
}

static void render_multi_image(u8 *image, s32 x, s32 y, s32 width, s32 height) {
    s32 posW, posH, imW, imH;
    s32 i = 0;
    s32 num = 256;
    s32 maskW = 1;
    s32 maskH = 1;

    gDPSetCycleType(video_glistp++, G_CYC_COPY);
    gDPSetRenderMode(video_glistp++, G_RM_NOOP, G_RM_NOOP2);

    // Find how best to seperate the horizontal. Keep going until it finds a whole value.
    while (TRUE) {
        f32 val = (f32) width / (f32) num;

        if ((s32) val == val && (s32) val >= 1) {
            imW = num;
            break;
        }
        num /= 2;
        if (num == 1) {
            return;
        }
    }
    // Find the tile height
    imH = 64 / (imW / 32); // This gets the vertical amount.

    num = 2;
    // Find the width mask
    while (TRUE) {
        if ((s32) num == imW) {
            break;
        }
        num *= 2;
        maskW++;
        if (maskW == 9) {
            return;
        }
    }
    num = 2;
    // Find the height mask
    while (TRUE) {
        if ((s32) num == imH) {
            break;
        }
        num *= 2;
        maskH++;
        if (maskH == 9) {
            return;
        }
    }
    num = height;
    // Find the height remainder
    s32 peakH = height - (height % imH);
    s32 cycles = (width * peakH) / (imW * imH);

    // Pass 1
    for (i = 0; i < cycles; i++) {
        posW = 0;
        posH = i * imH;
        while (posH >= peakH) {
            posW += imW;
            posH -= peakH;
        }

        gDPLoadSync(video_glistp++);
        gDPLoadTextureTile(video_glistp++, image, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, height, posW,
                           posH, ((posW + imW) - 1), ((posH + imH) - 1), 0, (G_TX_NOMIRROR | G_TX_WRAP),
                           (G_TX_NOMIRROR | G_TX_WRAP), maskW, maskH, 0, 0);
        gSPScisTextureRectangle(video_glistp++, ((x + posW) << 2), ((y + posH) << 2),
                                (((x + posW + imW) - 1) << 2), (((y + posH + imH) - 1) << 2),
                                G_TX_RENDERTILE, 0, 0, (4 << 10), (1 << 10));
    }
    // If there's a remainder on the vertical side, then it will cycle through that too.
    if (height - peakH != 0) {
        posW = 0;
        posH = peakH;
        for (i = 0; i < (width / imW); i++) {
            posW = i * imW;
            gDPLoadSync(video_glistp++);
            gDPLoadTextureTile(video_glistp++, image, G_IM_FMT_RGBA, G_IM_SIZ_16b, width, height, posW,
                               posH, ((posW + imW) - 1), (height - 1), 0, (G_TX_NOMIRROR | G_TX_WRAP),
                               (G_TX_NOMIRROR | G_TX_WRAP), maskW, maskH, 0, 0);
            gSPScisTextureRectangle(video_glistp++, (x + posW) << 2, (y + posH) << 2,
                                    ((x + posW + imW) - 1) << 2, ((y + posH + imH) - 1) << 2,
                                    G_TX_RENDERTILE, 0, 0, (4 << 10), (1 << 10));
        }
    }
}

OSTask gF3DTask = {
    M_GFXTASK,                             /* task type                */
    0,    /* task flags               */
    (u64 *) &rspbootTextStart,             /* boot ucode ptr           */
    0,  /* boot ucode size          */

    (u64 *) &gspF3DEX2_fifoTextStart,     /* ucode ptr                */
    SP_UCODE_SIZE,                         /* ucode size               */
    (u64 *) &gspF3DEX2_fifoDataStart,     /* ucode data ptr           */
    SP_UCODE_DATA_SIZE,                    /* ucode data size          */

    NULL,                                  /* dram stack      (unused) */
    0,                                     /* dram stack size (unused) */
    (u64 *) system_rdpfifo,                /* fifo buffer top          */
    (u64 *) system_rdpfifo + RDPFIFO_SIZE, /* fifo buffer bottom       */

    NULL,                                  /* data ptr      (set later) */
    NULL,                                  /* data size     (unneeded) */
    (u64 *) system_rspyield,               /* yield data ptr           */
    OS_YIELD_DATA_SIZE,                    /* yield data size          */
};

void makeF3DTask() {
    gF3DTask.t.ucode_boot_size = ((u8 *) rspbootTextEnd - (u8 *) rspbootTextStart);
}

OSMesgQueue dpMesgQ;
OSMesg dpMesg;

void hvqm_drawHLE(void *buf) {
    select_gfx_pool();
    // gDPPipeSync(video_glistp++);
    gDPSetColorImage(video_glistp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, buf);
    gDPSetScissor(video_glistp++, G_SC_NON_INTERLACE, 0, 0, 320, 240);

    render_multi_image((u8 *) buf, 0, 0, 320, 240);

    gDPFullSync(video_glistp++);
    gSPEndDisplayList(video_glistp++);

    osWritebackDCacheAll();
    makeF3DTask();
    gF3DTask.t.data_ptr = (u64 *) gfxbuf[gfxselect];
    // gF3DTask.t.data_size = ((u32)video_glistp - (u32)gGfxPool->buffer) * sizeof(Gfx);
    osSpTaskStart(&gF3DTask);
    osRecvMesg(&spMesgQ, NULL, OS_MESG_BLOCK);
    osRecvMesg(&dpMesgQ, NULL, OS_MESG_BLOCK);
}


static void mainproc(void *arg) {
    /* To main function */
    Main(arg);

    /* To idle state */
    osSetThreadPri(0, 0);
    for (;;)
        ;
    /* NOT REACHED */
}

static void idle(void *arg) {
    /* Start PI manager */
    osCreatePiManager((OSPri) OS_PRIORITY_PIMGR, &PiMessageQ, PiMessages, PI_COMMAND_QUEUE_SIZE);
    osCreateViManager((OSPri) OS_PRIORITY_VIMGR);

    osCreateMesgQueue(&dpMesgQ, &dpMesg, 1);
    osSetEventMesg(OS_EVENT_DP, &dpMesgQ, 1);
    osViSetMode(&osViModeTable[VIMODE]);
    osViSetXScale(1.0f);
    osViSetYScale(1.0f);
    osViSetSpecialFeatures(VIFEAT);
    osViSetSpecialFeatures(OS_VI_GAMMA_OFF);

    #define VIDEO(x) (_ ## x ## SegmentRomStart)
    #define EXTERN_VIDEO(x) extern u8 (_ ## x ## SegmentRomStart)[];

    EXTERN_VIDEO(hvqmdata);

#ifdef FAULT
    crash_screen_init(0);
#endif // FAULT

    /* Start main thread */
    osCreateThread(&mainThread, MAIN_THREAD_ID, mainproc, VIDEO(hvqmdata), mainThreadStack + STACKSIZE / 8,
                   MAIN_PRIORITY);
    osStartThread(&mainThread);

    /* Become idle */
    osSetThreadPri(0, 0);
    while (1);
}

void boot() {
    osInitialize();
    osInitialize_isv();
    osCreateThread(&idleThread, IDLE_THREAD_ID, idle, NULL, idleThreadStack + STACKSIZE / 8,
                   IDLE_PRIORITY);
    osStartThread(&idleThread);
    /* NOT REACHED */
}
