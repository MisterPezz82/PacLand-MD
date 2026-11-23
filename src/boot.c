#include <genesis.h>

void renderer_font(void);
extern const u32 boot_tiles[];
extern const u32 boot_pac_tiles[];
extern const u8 boot_chomp[];

static void centre(const char *text, u16 row) {
    VDP_drawText(text, (40 - strlen(text)) / 2, row);
}

/* SEGA-style lettering and source-only notice. No arcade resources are needed.
 * The mandatory console TMSS handshake remains in the startup code. */
void boot_screen(void) {
    const u16 colours[16] = {0x000,0xEE0,0xEC0,0xE80,0xE40,0xE00,0xA00,0xEE8,0xEEE,0,0x0EE,0x000,0,0,0,0};
    u16 x,y,t,eaten=0;bool skipped=FALSE;
    VDP_setEnable(FALSE);
    VDP_setScrollingMode(HSCROLL_PLANE,VSCROLL_PLANE);
    VDP_clearPlane(BG_A,TRUE);VDP_clearPlane(BG_B,TRUE);
    VDP_setHorizontalScroll(BG_A,0);VDP_setHorizontalScroll(BG_B,0);
    VDP_loadTileData(boot_tiles,128,224,CPU);
    VDP_loadTileData(boot_pac_tiles,352,384,CPU);
    VDP_setSpriteFull(0,0,-64,SPRITE_SIZE(4,4),0,0);VDP_updateSprites(1,CPU);
    for(y=0;y<7;y++)for(x=0;x<32;x++)
        VDP_setTileMapXY(BG_A,TILE_ATTR_FULL(PAL0,0,0,0,128+y*32+x),4+x,7+y);
    renderer_font();
    VDP_setTextPalette(PAL0);VDP_setTextPriority(TRUE);
    /* SGDK's font uses pen 15. */
    PAL_setColor(15,0xEEE);
    centre("AN UNOFFICIAL FAN PROJECT",17);
    centre("NOT AFFILIATED WITH OR ENDORSED BY",19);
    centre("BANDAI NAMCO OR SEGA",20);
    centre("PAC-LAND (C) BANDAI NAMCO",22);
    centre("NOT FOR RESALE",24);
    PAL_setColors(0,(u16 *)palette_black,64,CPU);
    VDP_setEnable(TRUE);
    {u16 pal[16];memcpy(pal,colours,sizeof pal);pal[15]=0xEEE;PAL_fadeIn(0,15,pal,16,FALSE);}
    for(t=0;t<50;t++){
        SYS_doVBlankProcess();
        if(JOY_readJoypad(JOY_1)&BUTTON_START){skipped=TRUE;break;}
    }
    if(!skipped){
        SND_PCM_startPlay(boot_chomp,3072,SOUND_PCM_RATE_16000,SOUND_PAN_CENTER,TRUE);
        for(t=0;t<104;t++){
            s16 sx=(s16)(t*4)-64;
            s16 chew=sx; /* centre of Pac-Man minus logo's x=32 */
            u16 frame=t%6,i;
            if(JOY_readJoypad(JOY_1)&BUTTON_START)break;
            while(chew>=(s16)(eaten*8)&&eaten<32){
                VDP_clearTileMapRect(BG_A,4+eaten,7,1,7);eaten++;
            }
            for(i=0;i<4;i++)VDP_setSpriteFull(i,sx+(i&1)*32,52+(i>>1)*32,
                SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL0,1,0,0,352+frame*64+i*16),i==3?0:i+1);
            VDP_updateSprites(4,CPU);SYS_doVBlankProcess();
        }
        SND_PCM_stopPlay();
    }
    VDP_setSpriteFull(0,0,-64,SPRITE_SIZE(4,4),0,0);VDP_updateSprites(1,CPU);
    PAL_fadeOut(0,15,16,FALSE);
    VDP_setTextPriority(FALSE);
}
