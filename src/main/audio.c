#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"

int next_pcmbufno = 0;
int pcm_mod_samples = 0;

#define AI_MSG_SIZE 2
static OSMesgQueue aiMessageQ;
static OSMesg aiMessages[AI_MSG_SIZE];

static ADPCMstate adpcm_state;

typedef struct AudioRing {
    struct AudioRing *next;
    u32 len;
    u32 open;
    s16 (*samples)[PCMBUF_SIZE];
} AudioRing;

AudioRing rbuffer[] = {
    {.next = &rbuffer[1]},
    {.next = &rbuffer[2]},
    {.next = &rbuffer[3]},
    {.next = &rbuffer[4]},
    {.next = &rbuffer[5]},
    {.next = &rbuffer[6]},
    {.next = &rbuffer[7]},
    {.next = &rbuffer[8]},
    {.next = &rbuffer[9]},
    {.next = &rbuffer[10]},
    {.next = &rbuffer[11]},
    {.next = &rbuffer[12]},
    {.next = &rbuffer[13]},
    {.next = &rbuffer[14]},
    {.next = &rbuffer[15]},
    {.next = &rbuffer[0]},
};

AudioRing *currBuf;

u64 playtime_us = 0;
u32 real_frequency = 0;

static u32 next_audio_record(void **streamp, void *pcmbuf) {
    HVQM2Record record_header __attribute__((aligned(16)));
    HVQM2Audio *audio_headerP;
    u32 samples;

    get_record(&record_header, adpcmbuf, HVQM2_AUDIO, streamp);

    audio_headerP = (HVQM2Audio *) adpcmbuf;
    samples = load32(audio_headerP->samples);
    adpcmDecode(&audio_headerP[1], (u32) load16(record_header.format), samples, pcmbuf, 1,
                &adpcm_state);

    return samples;
}

void init_audio(void **streamp) {
    // TODO: init ring buffer and perform first 3 conversions
    osCreateMesgQueue(&aiMessageQ, aiMessages, AI_MSG_SIZE);
    osSetEventMesg(OS_EVENT_AI, &aiMessageQ, (OSMesg *) 1);

    bzero(pcmbuf, sizeof(pcmbuf));

    for (int i = 0; i < NUM_PCMBUFs; i++) {
        rbuffer[i].samples = &pcmbuf[i];
        rbuffer[i].len = next_audio_record(streamp, rbuffer[i].samples);
    }

    currBuf = &rbuffer[0];
}

void process_audio(void **streamp) {
    osWritebackDCacheAll();

    int result = osAiSetNextBuffer(currBuf->samples, ALIGN(currBuf->len * 2 * sizeof(u16), 0x100));

    if (result == 0) {
        playtime_us += (((f32)currBuf->len / (f32)real_frequency) * 1000000.0f);
        currBuf = currBuf->next;
        currBuf->len = next_audio_record(streamp, currBuf->samples);
        osRecvMesg(&aiMessageQ, NULL, OS_MESG_BLOCK);
    }
}

void AudioMain(void *arg) {
    AudThreadParams *args = arg;
    void *streamp = args->streamp;
    register u32 audio_remain = args->remain;
    // WARNING: If sample rate is lower than 32000, emulators will slow down!
    real_frequency = osAiSetFrequency(args->samples_per_sec);

    init_audio(&streamp);
    
    while (1) {
        if (audio_remain != 0) {
            // osSyncPrintf("    ");
            // osSyncPrintf("aremain %d\n", audio_remain);
            // osSyncPrintf("PLAYTIME %lld\n", playtime_us);
            process_audio(&streamp);
            audio_remain--;
        } else {
            break;
        }
    }

    osSyncPrintf("AUD PLAYBACK DONE\n");

    while (1);
}
