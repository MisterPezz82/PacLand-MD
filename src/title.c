/* Arcade attract-mode title and high-score screens, played back from the
 * stream recorded by tools/prepare_title.py (tilemap writes per frame, scroll
 * values and the sprite list). Arcade fg layer -> plane A, bg -> plane B; the
 * arcade's 64x32 tilemaps map 1:1 onto the 64x32 planes (arcade row r at plane
 * row r-2), and its 288-pixel view sits 16 pixels in from the left. */
#pragma GCC optimize ("O2")
#include "game.h"
#include "title.h"
#include "options.h"
extern const TileSet font_default;

static u16 pos, frame;
static s16 hs_a[28], hs_b[28];
static void high_score_text(void){
 char digits[9];u16 i,start=0;
 if(game_high_score[0]==0&&game_high_score[1]==8&&game_high_score[2]==0&&game_high_score[3]==0)return;
 for(i=0;i<4;i++){digits[i*2]='0'+(game_high_score[i]>>4);digits[i*2+1]='0'+(game_high_score[i]&15);}digits[8]=0;
 while(start<7&&digits[start]=='0')start++;
 VDP_clearTileMapRect(BG_A,15,1,8,1);VDP_clearTileMapRect(BG_B,15,1,8,1);
 VDP_setTextPalette(PAL0);VDP_setTextPriority(TRUE);VDP_drawText(digits+start,15+start,1);VDP_setTextPriority(FALSE);
}
u8 title_cycle_done(void){return frame==0;}

void title_start(void) {
    u16 i,budget;
    DMA_clearQueue(); /* a departed scene must not overwrite the new screen */
    VDP_setEnable(FALSE); VDP_setWindowOff();
    budget=DMA_getMaxTransferSize();DMA_setMaxTransferSize(0);
    VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
    VDP_setScrollingMode(HSCROLL_TILE, VSCROLL_PLANE);
    VDP_setVerticalScroll(BG_A, 0); VDP_setVerticalScroll(BG_B, 0);
    PAL_setColors(0, title_pal, 64, CPU);
    VDP_setBackgroundColor(0);
    VDP_loadTileData(title_tiles, title_tile_base, title_tile_count, CPU);
    renderer_font();
    for (i = 0; i < 28; i++) { hs_a[i] = -8; hs_b[i] = -8; }
    VDP_setSpriteFull(0, 0, 240, SPRITE_SIZE(1, 1), 0, 0); VDP_updateSprites(1, CPU);
    pos = 0; frame = 0;
    /* Frames0..8 are the captured arcade initialisation: two full-plane
     * redraws followed by their finishing cells. Present only the completed
     * image, with its own scroll and sprite table already committed. */
    for(i=0;i<9;i++){title_frame();SYS_doVBlankProcess();}
    VDP_waitDMACompletion();VDP_waitFIFOEmpty();DMA_setMaxTransferSize(budget);
    VDP_waitVInt(); /* disabled display reports VBlank even during active lines */
    VDP_setEnable(TRUE);
}

void title_frame(void) {
    const u16 *s = title_stream + pos;
    u16 nw = s[0], nspr = s[3], i;
    s16 fa = (s16)s[1], fb = (s16)s[2];
    s += 4;
    /* direct VDP port writes, re-addressing only between runs of consecutive
     * cells: the arcade's full-screen redraws (frames 6-7, ~4300 cells) then
     * take about one frame instead of ten */
    if (nw) {
        vu16 *data = (vu16 *)VDP_DATA_PORT; vu32 *ctrl = (vu32 *)VDP_CTRL_PORT;
        u16 next = 0xFFFF;
        VDP_setAutoInc(2);
        for (i = 0; i < nw; i++, s += 2) {
            u16 c = s[0], a = (u16)(((c & 0x8000) ? VDP_BG_B : VDP_BG_A) + ((c & 0x7FF) << 1));
            if (a != next) *ctrl = VDP_WRITE_VRAM_ADDR((u32)a);
            *data = s[1]; next = (u16)(a + 2);
        }
    }
    /* fg: arcade rows 5..28 scroll (plane rows 3..26), the rest are fixed */
    for (i = 3; i < 27; i++) hs_a[i] = fa;
    for (i = 0; i < 28; i++) hs_b[i] = fb;
    hs_a[26]=hs_b[26]=0; /* the port's menu prompt has its own fixed row */
    if(game_high_score[0]||game_high_score[1]!=8||game_high_score[2]||game_high_score[3])hs_a[1]=hs_b[1]=0;
    VDP_setHorizontalScrollTile(BG_A, 0, hs_a, 28, DMA_QUEUE);
    VDP_setHorizontalScrollTile(BG_B, 0, hs_b, 28, DMA_QUEUE);
    {
        VDPSprite *spr = vdpSpriteCache;
        if (nspr > 79) nspr = 79;
        for (i = 0; i < nspr; i++, s += 4, spr++) {
            spr->y = (s16)s[0]; spr->size = (u8)(s[1] >> 8); spr->link = (u8)(i + 1);
            spr->attribut = s[2]; spr->x = (s16)s[3];
        }
        if (!nspr) { spr->y = 0; spr->size = 0; spr->link = 0; spr->attribut = 0; spr->x = 0; nspr = 1; }
        else spr[-1].link = 0;
        VDP_updateSprites(nspr, DMA_QUEUE);
    }
    pos = (u16)(s - title_stream);
    high_score_text();
    /* The recorded arcade title also writes this row on both planes. Clear
     * those cells: transparent font pixels must not expose old attract text. */
    VDP_clearTileMapRect(BG_A,0,26,40,1);VDP_clearTileMapRect(BG_B,0,26,40,1);
    VDP_setTextPalette(PAL0);VDP_setTextPriority(TRUE);
    VDP_drawText("START: PLAY   B: OPTIONS",8,26);VDP_setTextPriority(FALSE);
    /* VRAM fill is asynchronous and does not halt the 68000. Finish each
     * clear before another VDP command or the next demo can reuse its state. */
    if (++frame == title_frames) {
        frame = 0; pos = 0; VDP_setEnable(FALSE);
        VDP_clearPlane(BG_A, TRUE); VDP_clearPlane(BG_B, TRUE);
        if(!opt_attract)title_start(); /* hide the redraw even with demos disabled */
    }
}
