#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"

enum MessageIDs {
    MESG_SP_COMPLETE = 100,
    MESG_DP_COMPLETE,
    MESG_VI_VBLANK,
    MESG_START_SPTASK,
};

#define SC_MESG_SIZE 16
static OSMesgQueue scMessageQ;
static OSMesg scMessages[SC_MESG_SIZE];

u64 disptime_us = 0;

static OSThread scThread;
static u64 scStack[STACKSIZE / 8];

void SchedMain(void *arg) {
    osSetTime(0);
    while (1) {
        OSMesg msg;
        osRecvMesg(&scMessageQ, &msg, OS_MESG_BLOCK);
        switch ((u32)msg) {
            case MESG_VI_VBLANK:
                disptime_us = OS_CYCLES_TO_USEC(osGetTime());
                break;
        }
    }
}

void scheduler_init() {
    osCreateMesgQueue(&scMessageQ, scMessages, SC_MESG_SIZE);
    osViSetEvent(&scMessageQ, (OSMesg) MESG_VI_VBLANK, 1);
    osSetEventMesg(OS_EVENT_SP, &scMessageQ, (OSMesg) MESG_SP_COMPLETE);
    osSetEventMesg(OS_EVENT_DP, &scMessageQ, (OSMesg) MESG_DP_COMPLETE);

    osCreateThread(&scThread, SCHED_THREAD_ID, SchedMain, 0, scStack + STACKSIZE / 8, 100);
    osStartThread(&scThread);
}
