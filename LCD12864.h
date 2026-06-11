#ifndef _LCD_12864_H
#define _LCD_12864_H

#include "config.h"

void InitLcd12864();
void LcdWriteCmd(uint8 cmd);
void LcdShowString(uint8 x, uint8 y, uint8 *str);
void LcdShowImage(uint8 x, uint8 y, uint8 w, uint8 h, uint8 *img);
void LcdClearArea(uint8 x, uint8 y, uint8 w, uint8 h);

#endif
