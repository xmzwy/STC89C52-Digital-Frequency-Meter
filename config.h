#ifndef _CONFIG_H
#define _CONFIG_H

#include <reg52.h>
#include <intrins.h>

sfr T2MOD = 0xC9;

typedef signed    char    int8;
typedef signed    int     int16;
typedef signed    long    int32;
typedef unsigned  char    uint8;
typedef unsigned  int     uint16;
typedef unsigned  long    uint32;

#define OSC_FREQ   (11059200)
#define SYS_MCLK   (OSC_FREQ/12)

sbit KEY_IN_1  = P2^4;
sbit KEY_IN_2  = P2^5;
sbit KEY_IN_3  = P2^6;
sbit KEY_IN_4  = P2^7;
sbit KEY_OUT_1 = P2^3;
sbit KEY_OUT_2 = P2^2;
sbit KEY_OUT_3 = P2^1;
sbit KEY_OUT_4 = P2^0;

#define LCD12864_DB  P0
sbit LCD12864_RS = P1^0;
sbit LCD12864_RW = P1^1;
sbit LCD12864_E  = P1^5;

sbit I2C_SCL = P3^7;
sbit I2C_SDA = P3^6;

sbit EXT_SIGNAL = P3^4;

#endif
