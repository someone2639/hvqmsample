#include <ultra64.h>
#include <HVQM2File.h>
#include <hvqm2dec.h>
#include <adpcmdec.h>
#include "system.h"
#include "timekeeper.h"

int next_pcmbufno = 0;
int pcm_mod_samples = 0;

#define AUDIO_DMA_MSG_SIZE 1
static OSIoMesg audioDmaMesgBlock;
static OSMesgQueue audioDmaMessageQ;
static OSMesg audioDmaMessages[AUDIO_DMA_MSG_SIZE];

#define AI_MSG_SIZE 2
static OSMesgQueue aiMessageQ;
static OSMesg aiMessages[AI_MSG_SIZE];

static ADPCMstate adpcm_state;


typedef struct {
    struct AudioRing *next;
    u32 len;
    u32 open;
    u32 *samples;
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

static u32 next_audio_record(void **streamp, void *pcmbuf) {
    HVQM2Record record_header __attribute__((aligned(16)));
    HVQM2Audio *audio_headerP;
    u32 samples;

    *streamp = get_record(&record_header, adpcmbuf, HVQM2_AUDIO, *streamp, &audioDmaMesgBlock,
                               &audioDmaMessageQ);

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
    osCreateMesgQueue(&audioDmaMessageQ, audioDmaMessages, AUDIO_DMA_MSG_SIZE);

    bzero(pcmbuf, sizeof(pcmbuf));

    for (int i = 0; i < NUM_PCMBUFs; i++) {
        rbuffer[i].samples = &pcmbuf[i];
        rbuffer[i].len = next_audio_record(streamp, rbuffer[i].samples);
    }

    currBuf = &rbuffer[0];
}

void process_audio(void **streamp) {
    osWritebackDCacheAll();

    int result = osAiSetNextBuffer(currBuf->samples, PCMBUF_SIZE * sizeof(u16));

    if (result == 0) {
        currBuf = currBuf->next;
        currBuf->len = next_audio_record(streamp, currBuf->samples);
        osRecvMesg(&aiMessageQ, NULL, OS_MESG_BLOCK);
    }
}

AudThreadParams localparms ALIGNED(8);

void AudioMain(void *arg) {
    localparms = *(AudThreadParams*)arg;
    AudThreadParams *args = &localparms;
    init_audio(&args->streamp);
    
    while (1) {
        if (args->remain != 0) {
            osSyncPrintf("    aremain %d\n", args->remain);
            process_audio(&args->streamp);
            args->remain--;
        } else {
            break;
        }
    }

    osSyncPrintf("AUD PLAYBACK DONE\n");

    while (1);
}
