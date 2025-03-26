#include <ultra64.h>
#include "profiler.h"

// #define PDEBUG

static Profiler plist[NUM_PROFILERS];
static u32 pidx = 0;

#ifdef PDEBUG
#define DPRINT(fmt, ...) osSyncPrintf(fmt, __VA_ARGS__)
#else
#define DPRINT(fmt, ...)
#endif

static double time2ms(u64 time) {
    return OS_CYCLES_TO_USEC(time) / 1000.0;
}

static double time2fps(u64 time) {
    return (1000.0 / time2ms(time));
}

static int strncpy(char *dst, char *src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (src[i] == 0) {
            break;
        }
        dst[i] = src[i];
    }
}

static int strncmp(char *dst, char *src, u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (dst[i] != src[i]) {
            return dst[i] - src[i];
        }
    }

    return 0;
}

static Profiler *search_profiler(char *name) {
    for (int i = 0; i < pidx; i++) {
        Profiler *p = &plist[i];

        if (strncmp(p->name, name, 16) == 0) {
            return p;
        }
    }

    DPRINT("[ERROR] NO PROFILER CALLED %s!!!\n", name);

    return NULL;
}

void get_fps_vals(char *cname, char *rname) {
    Profiler *cpu = search_profiler(cname);
    Profiler *rsp = search_profiler(rname);

    osSyncPrintf("FPS:\n");
    osSyncPrintf("    Best Case: %lf FPS\n", time2fps(cpu->minTime + rsp->minTime));
    osSyncPrintf("   Worst Case: %lf FPS\n", time2fps(cpu->maxTime + rsp->maxTime));
}

void new_profiler(char *name) {
    if (search_profiler(name)) {
        return;
    }
    DPRINT("Making new profiler called %s...\n", name);
    Profiler *p = &plist[pidx++];
    strncpy(p->name, name, 16);
    p->name[strlen(name)] = 0;
    bzero(p->tag, 64);
    p->maxTime = 0;
    p->minTime = 0xFFFFFFFFFFFFFFFFULL;
}

void tag_profiler(char *name, char *tag) {
    Profiler *p = search_profiler(name);
    strncpy(p->tag, tag, 64);
    p->tag[strlen(tag)] = 0;
}

void start_profiler(char *name) {
    DPRINT("Starting profiler %s...\n", name);
    search_profiler(name)->start = osGetTime();
}

void end_profiler(char *name) {
    search_profiler(name)->end = osGetTime();
}

u64 get_profiler_time(char *name) {
    Profiler *p = search_profiler(name);

    u64 ret = p->end - p->start;

    if (ret < p->minTime) {
        p->minTime = ret;
    }
    if (ret > p->maxTime) {
        p->maxTime = ret;
    }

    return ret;
}

void print_profiler(char *name) {
    Profiler *p = search_profiler(name);

    if (strlen(p->tag) == 0) {
        osSyncPrintf("PROFILE [%16s] - %lf ms\n", name, time2ms(get_profiler_time(name)));
    } else {
        osSyncPrintf("PROFILE [%16s] - %lf ms (%s)\n", name, time2ms(get_profiler_time(name)), p->tag);
    }
}
