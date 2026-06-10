/*
*******************************************************************************
*                     数字频率计（测频、测周）
*                     STC89C52RC + 12864液晶 + PCF8591
*
* 文件名称：config.h
* 功    能：系统配置文件，包含类型定义、引脚分配、宏定义
* 说    明：基于KST-51开发板硬件引脚分配
*******************************************************************************
*/

#ifndef _CONFIG_H
#define _CONFIG_H

/* 通用头文件 */
#include <reg52.h>
#include <intrins.h>

/* 补充 reg52.h 可能缺失的 T2 寄存器定义 */
sfr T2MOD = 0xC9;  // 定时器2模式寄存器

/* 数据类型定义 */
typedef signed    char    int8;    //  8位有符号整数
typedef signed    int     int16;   // 16位有符号整数
typedef signed    long    int32;   // 32位有符号整数
typedef unsigned  char    uint8;   //  8位无符号整数
typedef unsigned  int     uint16;  // 16位无符号整数
typedef unsigned  long    uint32;  // 32位无符号整数

/* 全局运行参数 */
#define OSC_FREQ   (11059200)      // 晶振频率值，单位Hz
#define SYS_MCLK   (OSC_FREQ/12)   // 系统机器周期频率，即晶振频率÷12

/* ======================== IO引脚分配定义 ======================== */

/* 矩阵按键引脚 */
sbit KEY_IN_1  = P2^4;  // 按键扫描输入1
sbit KEY_IN_2  = P2^5;  // 按键扫描输入2
sbit KEY_IN_3  = P2^6;  // 按键扫描输入3
sbit KEY_IN_4  = P2^7;  // 按键扫描输入4
sbit KEY_OUT_1 = P2^3;  // 按键扫描输出1
sbit KEY_OUT_2 = P2^2;  // 按键扫描输出2
sbit KEY_OUT_3 = P2^1;  // 按键扫描输出3
sbit KEY_OUT_4 = P2^0;  // 按键扫描输出4

/* 12864液晶引脚（与1602共用接口） */
#define LCD12864_DB  P0      // 12864液晶数据端口
sbit LCD12864_RS = P1^0;     // 12864液晶指令/数据选择引脚
sbit LCD12864_RW = P1^1;     // 12864液晶读写选择引脚
sbit LCD12864_E  = P1^5;     // 12864液晶使能引脚

/* I2C总线引脚（连接PCF8591 DAC/ADC芯片） */
sbit I2C_SCL = P3^7;  // I2C总线时钟线
sbit I2C_SDA = P3^6;  // I2C总线数据线

/* 方波输出引脚（PCF8591 DAC AOUT引脚输出） */
/* PCF8591通过I2C连接: SCL=P3^7, SDA=P3^6, 地址0x48 */

/* 外部频率输入引脚 */
sbit EXT_SIGNAL = P3^4;    // T0计数器输入引脚，接外部信号源

#endif
