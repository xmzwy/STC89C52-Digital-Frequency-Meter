#include "config.h"
#include "LCD12864.h"

void LcdWaitReady()
{
    uint8 sta;

    LCD12864_DB = 0xFF;
    LCD12864_RS = 0;
    LCD12864_RW = 1;
    do {
        LCD12864_E = 1;
        sta = LCD12864_DB;
        LCD12864_E = 0;
    } while (sta & 0x80);
}

void LcdWriteCmd(uint8 cmd)
{
    LcdWaitReady();
    LCD12864_RS = 0;
    LCD12864_RW = 0;
    LCD12864_DB = cmd;
    LCD12864_E  = 1;
    LCD12864_E  = 0;
}

void LcdWriteDat(uint8 dat)
{
    LcdWaitReady();
    LCD12864_RS = 1;
    LCD12864_RW = 0;
    LCD12864_DB = dat;
    LCD12864_E  = 1;
    LCD12864_E  = 0;
}

void LcdShowString(uint8 x, uint8 y, uint8 *str)
{
    uint8 addr;

    x >>= 4;
    y >>= 4;
    if (y >= 2)
    {
        y -= 2;
        x += 8;
    }
    addr = y * 16 + x;

    LcdWriteCmd(0x30);
    LcdWriteCmd(0x80 | addr);
    while (*str != '\0')
    {
        LcdWriteDat(*str);
        str++;
    }
}

void LcdShowImage(uint8 x, uint8 y, uint8 w, uint8 h, uint8 *img)
{
    int16 i;
    uint8 xi, yi;
    uint8 xt, yt;

    x >>= 4;
    w >>= 3;
    i = 0;
    LcdWriteCmd(0x36);
    for (yi = 0; yi < h; yi++)
    {
        yt = y + yi;
        xt = x;
        if (yt >= 32)
        {
            yt -= 32;
            xt += 8;
        }
        LcdWriteCmd(0x80 | yt);
        LcdWriteCmd(0x80 | xt);
        for (xi = 0; xi < w; xi++)
        {
            LcdWriteDat(img[i++]);
        }
    }
}

void LcdClearArea(uint8 x, uint8 y, uint8 w, uint8 h)
{
    uint8 xi, yi;
    uint8 xt, yt;

    x >>= 4;
    w >>= 3;
    LcdWriteCmd(0x36);
    for (yi = 0; yi < h; yi++)
    {
        yt = y + yi;
        xt = x;
        if (yt >= 32)
        {
            yt -= 32;
            xt += 8;
        }
        LcdWriteCmd(0x80 | yt);
        LcdWriteCmd(0x80 | xt);
        for (xi = 0; xi < w; xi++)
        {
            LcdWriteDat(0x00);
        }
    }
}

void InitLcd12864()
{
    uint8 x, y;

    LcdWriteCmd(0x30);
    LcdWriteCmd(0x01);
    LcdWriteCmd(0x02);
    LcdWriteCmd(0x0C);

    LcdWriteCmd(0x34);
    for (y = 0; y < 32; y++)
    {
        LcdWriteCmd(0x80 | y);
        LcdWriteCmd(0x80 | 0);
        for (x = 0; x < 32; x++)
        {
            LcdWriteDat(0x00);
        }
    }
    LcdWriteCmd(0x36);
}
