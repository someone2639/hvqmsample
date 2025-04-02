#pragma once

enum MessageIDs {
    MESG_SP_COMPLETE = 100,
    MESG_DP_COMPLETE,
    MESG_VI_VBLANK,
    MESG_START_SPTASK,
};

#define SC_MESG_SIZE 16

typedef struct {
    u8 wait_sp;
    u8 wait_dp;
    void (*sp_callback)();
    void (*dp_callback)();
} Task;

