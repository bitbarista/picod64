/* the same script as diff.mjs, on glibc: one line per step */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "script.h"
#include "sim.c"
static uint32_t hash(void){const uint8_t*p=sim_fb();uint32_t h=2166136261u;for(int i=0;i<8*132;i++){h^=p[i];h*=16777619u;}return h;}
int main(int ac,char**av){
    int ascii=ac>1;
    sim_card_reset();
    uint32_t ms=1000;
    for(const step_t*s=script;s->op;s++){
        switch(s->op){
        case 'P':sim_power(s->arg,ms);break;
        case 'T':sim_turn(s->arg,ms);break;
        case 'K':sim_card(s->arg,ms);break;
        case 'R':sim_rom(s->arg);break;
        case 'F':sim_font_file(s->arg);break;
        case 'W':{int pushed=s->pushed;for(int i=0;i<s->arg;i+=10){ms+=10;sim_tick(ms,pushed);}break;}
        }
        ms+=10;sim_tick(ms,s->pushed);
        printf("%c %d %08x %d %d %s|%s\n",s->op,s->arg,hash(),sim_online(),sim_screen(),sim_mounted(),sim_dir());
        if(ascii&&s->note){printf("--- %s\n",s->note);const uint8_t*p=sim_fb();for(int y=0;y<64;y+=2){for(int x=2;x<130;x++){int t=(p[(y>>3)*132+x]>>(y&7))&1,b=(p[((y+1)>>3)*132+x]>>((y+1)&7))&1;fputs(t&&b?"█":t?"▀":b?"▄":" ",stdout);}puts("");}}
    }
    return 0;
}
