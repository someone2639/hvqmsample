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

/***********************************************************************
 *
 * mainproc - Main thread procedure
 *
 ***********************************************************************/
static void mainproc(void *arg) {
    /* To main function */
    Main(arg);

    /* To idle state */
    osSetThreadPri(0, 0);
    for (;;)
        ;
    /* NOT REACHED */
}

/***********************************************************************
 *
 * idle - Idle thread procedure
 *
 ***********************************************************************/
static void idle(void *arg) {
    /* Start PI manager */
    osCreatePiManager((OSPri) OS_PRIORITY_PIMGR, &PiMessageQ, PiMessages, PI_COMMAND_QUEUE_SIZE);

    /*
     * Start VI manager, initialize video mode
     */
    osCreateViManager((OSPri) OS_PRIORITY_VIMGR);
    osViSetMode(&osViModeTable[VIMODE]);
    osViSetXScale(1.0f);
    osViSetYScale(1.0f);
    osViSetSpecialFeatures(VIFEAT);
    osViSetSpecialFeatures(OS_VI_GAMMA_OFF);

    #define VIDEO(x) (_ ## x ## SegmentRomStart)
    #define EXTERN_VIDEO(x) extern u8 (_ ## x ## SegmentRomStart)[];

    EXTERN_VIDEO(hvqmdata);

    crash_screen_init(0);

    /* Start main thread */
    osCreateThread(&mainThread, MAIN_THREAD_ID, mainproc, VIDEO(hvqmdata), mainThreadStack + STACKSIZE / 8,
                   MAIN_PRIORITY);
    osStartThread(&mainThread);

    /* Become idle */
    osSetThreadPri(0, 0);
    while (1);
}

/***********************************************************************
 *
 * boot - Boot code
 *
 ***********************************************************************/
void boot() {
    osInitialize();
    osInitialize_isv();
    osCreateThread(&idleThread, IDLE_THREAD_ID, idle, NULL, idleThreadStack + STACKSIZE / 8,
                   IDLE_PRIORITY);
    osStartThread(&idleThread);
    /* NOT REACHED */
}
