/* Original host tooling, not a hardware implementation. Disk writes occur
 * only for explicitly enabled, offline replay captures. See the preparation
 * tool for the exact instrumentation diff and provenance. */
#include "game/em_spu2_observer_capture.h"
#include "game/em_frame.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static struct {
    int checked, enabled;
    char directory[4096];
    FILE *events, *rows, *blobs;
    uint64_t sequence, blob_bytes, row_count, rendered[2], consumed[2], render_start;
    unsigned generation[2];
    const EmSfxRegistry *registry;
    uint8_t *exported;
    struct { int valid; uint64_t index; int16_t history[4]; } sfx[48];
    struct { int fresh; int16_t previous[3], block[28]; } stream[48];
} o;

static void die(const char *why)
{
    fprintf(stderr, "SPU2 private observer: %s\n", why);
    abort();
}
static void path(char result[8192], const char *name)
{
    if (snprintf(result, 8192, "%s/%s", o.directory, name) >= 8192) die("path too long");
}
static void finish(void)
{
    if (!o.enabled) return;
    if (fclose(o.events) || fclose(o.rows) || fclose(o.blobs)) die("close failed");
    char file[8192]; path(file, "capture.json");
    FILE *f = fopen(file, "w");
    if (!f) die("metadata open failed");
    fprintf(f, "{\"schema\":\"spu2-native-observer-v1\",\"complete\":true,"
        "\"rate\":48000,\"voice_record_bytes\":64,\"voice_records\":%" PRIu64 ","
        "\"blob_bytes\":%" PRIu64 ",\"events\":%" PRIu64 ","
        "\"frames\":[%" PRIu64 ",%" PRIu64 "],\"output_frames\":[%" PRIu64 ",%" PRIu64 "],"
        "\"generations\":[%u,%u],\"domains\":[\"sfx\",\"stream\"],"
        "\"clock\":\"domain-local modeled 48k; register writes affect before_frame; mix_range maps to offline drain\","
        "\"row_lifetime\":\"legacy active voices only; full source and DMA retained for independent lifetime\","
        "\"initial_state\":\"native model init/calloc, not measured hardware reset\","
        "\"master\":null,\"mmix\":null,\"dry_wet_routing\":null,"
        "\"unknown_reason\":\"shared native model does not retain/apply complete original core routing/master setup\","
        "\"files\":{\"events\":\"events.jsonl\",\"voices\":\"voice-frames.bin\",\"sources\":\"sources.bin\"}}\n",
        o.row_count, o.blob_bytes, o.sequence, o.rendered[0], o.rendered[1],
        o.consumed[0], o.consumed[1], o.generation[0], o.generation[1]);
    if (fclose(f)) die("metadata close failed");
    free(o.exported);
}
int a2_enabled(void)
{
    if (o.checked) return o.enabled;
    o.checked = 1;
    const char *dir = getenv("EM_SPU2_OBSERVER_DIR");
    if (!dir || !*dir) return 0;
    const char *offline = getenv("EM_REPLAY_AUDIO");
    if (!offline || !*offline) die("requires EM_REPLAY_AUDIO (single game-thread offline drain)");
    if (strlen(dir) >= sizeof o.directory) die("directory too long");
    strcpy(o.directory, dir);
    if (mkdir(dir, 0700) && errno != EEXIST) die("directory creation failed");
    char file[8192];
    path(file, "events.jsonl"); o.events = fopen(file, "wb");
    path(file, "voice-frames.bin"); o.rows = fopen(file, "wb");
    path(file, "sources.bin"); o.blobs = fopen(file, "wb");
    if (!o.events || !o.rows || !o.blobs) die("capture file open failed");
    setvbuf(o.events, NULL, _IOFBF, 1u << 20);
    setvbuf(o.rows, NULL, _IOFBF, 1u << 20);
    setvbuf(o.blobs, NULL, _IOFBF, 1u << 20);
    o.enabled = 1;
    if (atexit(finish)) die("atexit registration failed");
    return 1;
}
static void event(const char *type, unsigned domain, uint64_t frame)
{
    fprintf(o.events, "{\"type\":\"%s\",\"domain\":%u,\"frame\":%" PRIu64
        ",\"seq\":%" PRIu64 ",\"counter\":%u,\"vblank\":%" PRIu64,
        type, domain, frame, o.sequence++, em_frame_counter(), em_frame_vblanks());
}
static uint64_t blob(const void *data, size_t size)
{
    uint64_t offset = o.blob_bytes;
    if (fwrite(data, 1, size, o.blobs) != size) die("blob write failed");
    o.blob_bytes += size;
    return offset;
}
static void u16(uint8_t *p, uint16_t x) { p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); }
static void u32(uint8_t *p, uint32_t x) { u16(p,(uint16_t)x); u16(p+2,(uint16_t)(x>>16)); }
static void u64(uint8_t *p, uint64_t x) { u32(p,(uint32_t)x); u32(p+4,(uint32_t)(x>>32)); }
static void f32(uint8_t *p, float x) { uint32_t n; memcpy(&n,&x,4); u32(p,n); }
static void row(unsigned domain, uint64_t frame, unsigned voice, const int16_t h[4],
                uint16_t fraction, uint16_t envx, const uint16_t volume[2],
                uint16_t adsr1, uint16_t adsr2, float legacy, uint64_t index,
                uint32_t source)
{
    uint8_t b[64] = {0};
    u64(b,frame); u32(b+8,domain); u32(b+12,voice);
    for (unsigned i=0;i<4;i++) u16(b+16+2*i,(uint16_t)h[i]);
    u16(b+24,fraction); u16(b+26,envx); u16(b+28,volume[0]); u16(b+30,volume[1]);
    u16(b+32,adsr1); u16(b+34,adsr2);
    f32(b+36,em_sfx_volume_gain(volume[0])); f32(b+40,em_sfx_volume_gain(volume[1]));
    f32(b+44,legacy); u64(b+48,index); u32(b+56,source);
    if (fwrite(b,1,sizeof b,o.rows)!=sizeof b) die("voice row write failed");
    o.row_count++;
}
static void register_event(void *ctx, uint64_t frame, EmIopOutputWriteKind kind,
                           uint16_t reg, uint32_t value)
{
    (void)ctx;
    static const char *const kinds[]={"param","switch","addr"};
    event("stream_register",1,frame);
    fprintf(o.events,",\"kind\":\"%s\",\"reg\":%u,\"value\":%u}\n",kinds[kind],reg,value);
}
static void command_event(void *ctx, const EmIopOutputCommandEvent *e)
{
    (void)ctx;
    event("stream_command",1,e->sample);
    fprintf(o.events,",\"tick\":%" PRIu64 ",\"words\":[%u,%u,%u,%u],\"forwarded\":%d}\n",
            e->tick,e->command[0],e->command[1],e->command[2],e->command[3],e->forwarded);
}
static void frame_end(void *ctx, uint64_t frame)
{
    (void)ctx; o.rendered[1]=frame+1;
}
void a2_stream_created(EmIopStream *s)
{
    if (!a2_enabled()) return;
    o.generation[1]++;
    memset(o.stream,0,sizeof o.stream);
    event("stream_initial",1,0);
    const uint64_t off=blob(em_iop_stream_spu_ram(s),EM_SPU_RAM_SIZE);
    fprintf(o.events,",\"generation\":%u,\"ram_offset\":%" PRIu64 ",\"ram_bytes\":%u,"
                    "\"state_origin\":\"native_calloc_not_hardware\",\"voices\":[",o.generation[1],off,EM_SPU_RAM_SIZE);
    for (unsigned i=0;i<48;i++) {
        const EmIopSpuVoice *p=em_iop_stream_spu_voice(s,(int)i);
        fprintf(o.events,"%s{\"voice\":%u,\"on\":%u,\"pitch\":%u,\"adsr1\":%u,\"adsr2\":%u,"
                "\"volume\":[%u,%u],\"ssa\":%u,\"lsa\":%u,\"nax\":%u,\"envx\":%d}",
                i?",":"",i,p->on,p->pitch,p->adsr1,p->adsr2,p->vol[0],p->vol[1],p->ssa,p->lsa,p->nax,p->env.level);
    }
    fprintf(o.events,"]}\n");
    EmIopOutputObservers obs={NULL,command_event,register_event,frame_end};
    em_iop_stream_set_output_observers(s,&obs);
}
void a2_stream_key_on(int voice)
{
    if (!a2_enabled()) return;
    memset(&o.stream[voice],0,sizeof o.stream[voice]);
    o.stream[voice].fresh=1;
}
void a2_stream_block(uint64_t frame, int voice, const EmIopSpuVoice *p, int32_t h1, int32_t h2)
{
    if (!a2_enabled()) return;
    if (!o.stream[voice].fresh)
        memcpy(o.stream[voice].previous,o.stream[voice].block+25,6);
    o.stream[voice].fresh=0;
    memcpy(o.stream[voice].block,p->block,sizeof p->block);
    const uint64_t off=blob(p->block,sizeof p->block);
    event("stream_block",1,frame);
    fprintf(o.events,",\"voice\":%d,\"address\":%u,\"lsa\":%u,\"flags\":%u,"
        "\"history_in\":[%d,%d],\"history_out\":[%d,%d],\"pcm_offset\":%" PRIu64 ",\"pcm_samples\":28}\n",
        voice,p->nax,p->lsa,p->flags,h1,h2,p->s1,p->s2,off);
}
void a2_stream_voice(uint64_t frame, int voice, const EmIopSpuVoice *p)
{
    if (!a2_enabled()) return;
    int16_t h[4];
    for (int j=0;j<4;j++) {
        const int at=(int)p->pos-3+j;
        h[j]=at<0?o.stream[voice].previous[at+3]:p->block[at];
    }
    row(1,frame,(unsigned)voice,h,(uint16_t)p->counter,(uint16_t)p->env.level,p->vol,
        p->adsr1,p->adsr2,(float)p->block[p->pos],(uint64_t)p->nax*28/16+p->pos,0);
}
void a2_stream_dma(uint64_t frame, uint64_t tick, int channel, uint32_t address,
                   const uint8_t *data, uint32_t bytes)
{
    if (!a2_enabled()) return;
    const uint64_t off=blob(data,bytes);
    event("stream_dma",1,frame);
    fprintf(o.events,",\"tick\":%" PRIu64 ",\"channel\":%d,\"address\":%u,\"offset\":%" PRIu64 ",\"bytes\":%u}\n",
            tick,channel,address,off,bytes);
}
void a2_stream_field(EmIopStream *s)
{
    if (!a2_enabled()) return;
    uint64_t h,t,n; em_iop_stream_clock(s,&h,&t,&n);
    event("clock",1,n);
    fprintf(o.events,",\"half_lines\":%" PRIu64 ",\"ticks\":%" PRIu64 "}\n",h,t);
}
static uint32_t sample_id(const EmSfxSample *s)
{
    return s && o.registry ? (uint32_t)(s-o.registry->samples) : UINT32_MAX;
}
static void source(const EmSfxSample *s, uint64_t frame)
{
    const uint32_t id=sample_id(s);
    if (id==UINT32_MAX || id>=o.registry->sample_count || o.exported[id]) return;
    o.exported[id]=1;
    const uint64_t total=s->frames+(s->loop_start==UINT32_MAX?0:s->frames-s->loop_start);
    const uint64_t off=blob(s->pcm,(size_t)total*2);
    event("sfx_source",0,frame);
    fprintf(o.events,",\"source_id\":%u,\"frames\":%u,\"loop_start\":",id,s->frames);
    if (s->loop_start==UINT32_MAX) fputs("null",o.events); else fprintf(o.events,"%u",s->loop_start);
    fprintf(o.events,",\"offset\":%" PRIu64 ",\"samples\":%" PRIu64 ",\"format\":\"s16le\",\"base_rate\":48000}\n",off,total);
}
void a2_sfx_init(const EmSfxDriver *d)
{
    if (!a2_enabled()) return;
    free(o.exported); o.registry=d->registry;
    o.exported=calloc(o.registry->sample_count,1);
    if (!o.exported && o.registry->sample_count) die("sample map allocation failed");
    memset(o.sfx,0,sizeof o.sfx); o.generation[0]++;
    event("sfx_initial",0,0);
    fprintf(o.events,",\"generation\":%u,\"state_origin\":\"native_driver_init_not_hardware\",\"voices\":[",o.generation[0]);
    for (unsigned i=0;i<48;i++) {
        const EmSfxVoice *p=&d->voices[i];
        fprintf(o.events,"%s{\"voice\":%u,\"on\":%u,\"pitch\":%u,\"adsr1\":%u,\"adsr2\":%u,"
                "\"volume\":[%u,%u],\"envx\":%d}",i?",":"",i,p->on,p->pitch,p->adsr1,p->adsr2,
                p->volume[0],p->volume[1],p->envelope.level);
    }
    fprintf(o.events,"]}\n");
}
uint64_t a2_sfx_effective(const EmSfxDriver *d, unsigned command, uint32_t a, uint32_t b)
{
    if (!a2_enabled() || (command!=0xA && command!=0xB)) return 0;
    const uint64_t mask=(uint64_t)(a&0xFFFFFF)|((uint64_t)(b&0xFFFFFF)<<24);
    uint64_t result=0;
    for (unsigned i=0;i<48;i++)
        if ((mask>>i&1) && (command==0xA ? d->voices[i].next_sample!=NULL
                : d->voices[i].on && d->voices[i].frames!=0)) result|=1ull<<i;
    return result;
}
void a2_sfx_command(const EmSfxDriver *d, uint64_t frame, const uint32_t words[4], uint64_t mask)
{
    if (!a2_enabled()) return;
    event("sfx_command",0,frame);
    fprintf(o.events,",\"words\":[%u,%u,%u,%u],\"effective_mask\":%" PRIu64 "}\n",
            words[0],words[1],words[2],words[3],mask);
    if (words[0]==5 && words[1]<48) source(d->voices[words[1]].next_sample,frame);
    if (words[0]==0xA)
        for (unsigned i=0;i<48;i++) if (mask>>i&1) {
            memset(&o.sfx[i],0,sizeof o.sfx[i]); source(d->voices[i].sample,frame);
        }
}
void a2_sfx_render(uint64_t frame, unsigned count)
{
    if (!a2_enabled()) return;
    o.render_start=frame; o.rendered[0]=frame+count;
    event("render_range",0,frame); fprintf(o.events,",\"count\":%u}\n",count);
}
static int16_t source_at(const EmSfxSample *s, uint64_t at)
{
    if (at<s->frames) return s->pcm[at];
    if (s->loop_start==UINT32_MAX) return 0;
    return s->pcm[s->frames+(at-s->frames)%(s->frames-s->loop_start)];
}
void a2_sfx_voice(unsigned voice, unsigned offset, const EmSfxVoice *p,
                  uint64_t index, float fraction, float legacy)
{
    if (!a2_enabled()) return;
    const EmSfxSample *s=p->sample;
    if (!o.sfx[voice].valid) {
        memset(o.sfx[voice].history,0,sizeof o.sfx[voice].history);
        o.sfx[voice].history[3]=source_at(s,index);
        o.sfx[voice].valid=1; o.sfx[voice].index=index;
    } else {
        uint64_t at=o.sfx[voice].index;
        while (at!=index) {
            at++;
            if (s->loop_start!=UINT32_MAX && at>=2ull*s->frames-s->loop_start) at-=s->frames-s->loop_start;
            memmove(o.sfx[voice].history,o.sfx[voice].history+1,6);
            o.sfx[voice].history[3]=source_at(s,at);
        }
        o.sfx[voice].index=index;
    }
    row(0,o.render_start+offset,voice,o.sfx[voice].history,(uint16_t)(fraction*4096.0f),
        (uint16_t)p->envelope.level,p->volume,p->adsr1,p->adsr2,legacy,index,sample_id(s));
}
void a2_mix_range(unsigned domain, uint64_t source_frame, uint64_t count, unsigned requested)
{
    if (!a2_enabled()) return;
    event("mix_range",domain,source_frame);
    fprintf(o.events,",\"source_frame\":%" PRIu64 ",\"count\":%" PRIu64
            ",\"output_frame\":%" PRIu64 ",\"requested\":%u}\n",source_frame,count,o.consumed[domain],requested);
    o.consumed[domain]+=requested;
}
