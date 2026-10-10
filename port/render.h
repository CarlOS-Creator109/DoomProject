#pragma once
#include <stdint.h>
#include "wad.h"
#include "d_player.h"
#define RV_WIDTH 640
#define RV_VIEWH 403
void RV_Init(WAD *w);
void RV_Level(void);
void RV_SetPalette(const uint8_t *p768);
void RV_Render(player_t *pl,double x,double y,double z,double ang);
void RV_Compose(uint32_t *out,int outw,int outh,const uint8_t *scr,int show3d);
