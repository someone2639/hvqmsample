#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"

int next_pcmbufno = 0;
int pcm_mod_samples = 0;
u32 samples_elapsed = 0;

#define AI_MSG_SIZE 2
static OSMesgQueue aiMessageQ;
static OSMesg aiMessages[AI_MSG_SIZE];

static ADPCMstate adpcm_state;

typedef struct AudioRing {
    struct AudioRing *next;
    struct AudioRing *prev;
    u32 len;
    u64 endtime_us;
    s16 (*samples)[PCMBUF_SIZE];
} AudioRing;

AudioRing rbuffer[NUM_PCMBUFs] = {
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
u32 num_channels = 1;

extern u64 disptime_us;

static u32 audio_playing() {
    return (playtime_us != 0) && (disptime_us != 0) && (samples_elapsed > NUM_CFBs);
}

static u32 samples2usec(AudioRing *buf) {
    return (((f32)buf->len / (f32)real_frequency) * 1000000.0f);
}


u32 get_usec() {
    if (currBuf) {
        return samples2usec(currBuf);
    } else {
        return 0;
    }
}

static u32 next_audio_record(void **streamp, void *pcmbuf) {
    HVQM2Record record_header __attribute__((aligned(16)));
    HVQM2Audio *audio_headerP;
    u32 samples;

    u32 size = get_record(&record_header, HVQM2_AUDIO, streamp);
    load_record(size, HVQM2_AUDIO, adpcmbuf, streamp);

    audio_headerP = (HVQM2Audio *) adpcmbuf;
    samples = load32(audio_headerP->samples);
    adpcmDecode(&audio_headerP[1], (u32) load16(record_header.format), samples, pcmbuf, 1,
                &adpcm_state);

    return samples;
}

void ring_update(void **streamp, AudioRing *abuf) {
    abuf->len = next_audio_record(streamp, abuf->samples);
    abuf->endtime_us = playtime_us + samples2usec(abuf)/2;
}

void init_audio(void **streamp) {
    // TODO: init ring buffer and perform first 3 conversions
    osCreateMesgQueue(&aiMessageQ, aiMessages, AI_MSG_SIZE);
    osSetEventMesg(OS_EVENT_AI, &aiMessageQ, (OSMesg *) 1);
    osSendMesg(&aiMessageQ, (OSMesg)0, OS_MESG_NOBLOCK);

    bzero(pcmbuf, sizeof(pcmbuf));

    for (int i = 0; i < NUM_PCMBUFs; i++) {
        rbuffer[(i + 1) % NUM_PCMBUFs].prev = &rbuffer[i];
        rbuffer[i].samples = &pcmbuf[i];
        ring_update(streamp, &rbuffer[i]);
        playtime_us += samples2usec(&rbuffer[i]) / 2;
    }

    playtime_us = 0;
    currBuf = &rbuffer[0];
}

void process_audio(void **streamp) {
    osWritebackDCacheAll();

    osRecvMesg(&aiMessageQ, NULL, OS_MESG_BLOCK);
    int result = osAiSetNextBuffer(currBuf->samples, ALIGN(currBuf->len * 2 * sizeof(u16), 8));

    if (playtime_us > currBuf->endtime_us) {
        currBuf = currBuf->next;
        samples_elapsed++;
        // osSyncPrintf("AUD %d\n", samples_elapsed);
        ring_update(streamp, currBuf->prev);
    }

    playtime_us += samples2usec(currBuf);
}

void AudioMain(void *arg) {
    AudThreadParams *args = arg;
    void *streamp = args->streamp;
    register u32 audio_remain = args->remain;
    num_channels = args->num_channels;
    // WARNING: If sample rate is lower than 32000, emulators will slow down!
    // TODO: Turn into an audio task using aspMain to resample all audio to 32k
    real_frequency = osAiSetFrequency(args->samples_per_sec);

    init_audio(&streamp);
    
    while (1) {
        if (audio_remain != 0) {
            process_audio(&streamp);
            audio_remain--;
        } else {
            break;
        }
    }

    osSyncPrintf("AUD PLAYBACK DONE\n");

    while (1);
}
