#include "game/em_panel_message.h"
#include "game/em_hud.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned u16(const unsigned char *p)
{return (unsigned)p[0]|(unsigned)p[1]<<8;}

int em_panel_message_load(EmPanelMessage *message,const char *path)
{
    if (!message || !path) return 0;
    memset(message,0,sizeof *message);
    FILE *f=fopen(path,"rb");if (!f) return 0;
    unsigned char header[32],records[40];
    if (fread(header,1,32,f)!=32 || memcmp(header,"EMOD",4) ||
        u32(header+4)!=1 || u32(header+8)!=2 || !u32(header+12) ||
        u32(header+12)>64 || u32(header+24)>448 ||
        !u32(header+28) || u32(header+28)>2048 ||
        fread(records,1,40,f)!=40) {fclose(f);return 0;}
    unsigned size=u32(header+28);
    char *text=malloc(size);
    if (!text) {fclose(f);return 0;}
    int valid=fread(text,1,size,f)==size && fgetc(f)==EOF;
    fclose(f);
    for (unsigned i=0;valid && i<2;i++) {
        unsigned char *r=records+i*20;
        unsigned offset=u32(r+8),length=u32(r+12);
        valid=u16(r)==0x18+i && u16(r+4)==0xFFFF && r[6]==255 &&
            r[7]==i && r[16]<=32 && offset<size && length<size-offset;
        if (valid) valid=text[offset+length]==0 && !memchr(text+offset,0,length);
        if (valid) message->lines[i]=(EmOpeningLine){
            .line=(uint16_t)u16(r),.duration=(uint16_t)u16(r+2),.voice=-1,
            .speaker=255,.terminal=r[7],.skew=r[16],.text=text+offset};
    }
    if (!valid) {free(text);memset(message,0,sizeof *message);return 0;}
    message->text=text;
    message->line_height=u32(header+12);message->fill=u32(header+16);
    message->outline=u32(header+20);message->y=u32(header+24);
    em_opening_dialogue_start(&message->dialogue,NULL,0);
    return 1;
}

void em_panel_message_free(EmPanelMessage *message)
{
    if (!message) return;
    free(message->text);memset(message,0,sizeof *message);
}

int em_panel_message_start(EmPanelMessage *message,uint32_t token,uint32_t delay)
{
    if (!message || !message->text || token!=0x80000018 || delay>INT_MAX ||
        message->phase==1) return 0;
    message->phase=1;message->delay=delay;
    em_opening_dialogue_start(&message->dialogue,message->lines,2);
    return 1;
}

int em_panel_message_tick(EmPanelMessage *message,int busy155,int busy156)
{
    if (!message || !message->text) return 0;
    if (message->phase==2) {
        message->phase=0;
        em_opening_dialogue_start(&message->dialogue,NULL,0);
        return 1;
    }
    if (message->phase!=1) return 0;
    if (message->delay) {--message->delay;return 0;}
    if (message->dialogue.active) em_opening_dialogue_tick(&message->dialogue);
    if (!message->dialogue.active && !busy155 && !busy156) message->phase=2;
    return 0;
}

int em_panel_message_done(const EmPanelMessage *message)
{return !message || !message->text ? -1 : message->phase==2;}

void em_panel_message_render(const EmPanelMessage *message,EmGfx *gfx)
{
    if (!message || !message->text || !gfx) return;
    const EmOpeningLine *line=em_opening_dialogue_line(&message->dialogue);
    if (line && line->text && line->text[0])
        em_hud_subtitle(gfx,line->text,(float)message->y,(float)message->line_height,
                        (float)line->skew,message->fill,message->outline);
}
