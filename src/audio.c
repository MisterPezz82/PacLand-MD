#include <genesis.h>
#include "music.h"
/* Sound is the arcade's own music and effects on the YM2612 and PSG
 * (src/music.c), started by the original game's sound requests. */
void audio_init(void){music_init();}
void audio_start(void){}
void audio_stop(void){music_stop_all();}
