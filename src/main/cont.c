#include <ultra64.h>

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

u32 get_button() {
    return contPads[0].button;
}


void ContMain(void *arg) {
    init_controllers();

    while (1) {
        read_controllers();
    }
}

