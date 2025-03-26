#include <ultra64.h>
#include "system.h"

OSThread contThread;
u64 contStack[STACKSIZE];

#define CONT_MSG_SIZE 2
OSMesgQueue contMessageQ;
static OSMesg contMessages[CONT_MSG_SIZE];

u8 contBits[MAXCONTROLLERS];
OSContStatus contStatuses[MAXCONTROLLERS];
static OSMesg dummy;

void init_controllers() {
    osCreateMesgQueue(&contMessageQ, contMessages, CONT_MSG_SIZE);
    osSetEventMesg(OS_EVENT_SI, &contMessageQ, (OSMesg *) 1);
    osContInit(&contMessageQ, contBits, contStatuses);
    osContStartQuery(&contMessageQ);
    osRecvMesg(&contMessageQ, &dummy, OS_MESG_BLOCK);
    osContGetQuery(contStatuses);
}

OSContPad contPads[MAXCONTROLLERS];

void read_controllers() {
    osContStartReadData(&contMessageQ);
    osRecvMesg(&contMessageQ, &dummy, OS_MESG_BLOCK);
    osContGetReadData(contPads);
}

u32 test_button(u32 button) {
    if (contPads[0].button & button) {
        contPads[0].button &= ~button;
        return 1;
    }
    return 0;
}


void ContMain(void *arg) {
    init_controllers();

    while (1) {
        read_controllers();
    }
}


void start_cont_thread() {
    osCreateThread(&contThread, CONT_THREAD_ID, ContMain, NULL, &contStack[STACKSIZE / 8], CONT_PRIORITY);
    osStartThread(&contThread);
}

