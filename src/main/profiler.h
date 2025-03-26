#ifndef PROFILER_H
#define PROFILER_H

typedef struct {
    char name[16 + 1];
    char tag[64 + 1];
    u64 start;
    u64 end;
    u64 maxTime;
    u64 minTime;
    double avgTime;
    u32 numSamples;
} Profiler;

#define NUM_PROFILERS 8

// why did i write the api like this.........................
void new_profiler(char *name);
void start_profiler(char *name);
void end_profiler(char *name);
void tag_profiler(char *name, char *tag);
void get_fps_vals(char *cname, char *rname);

u64 get_profiler_time(char *name);
void print_profiler(char *name);
extern int strlen(char *);

#endif // PROFILER_H
