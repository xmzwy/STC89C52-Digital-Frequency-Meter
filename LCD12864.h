/*
*******************************************************************************
*                     数字频率计（测频、测周）
*
* 文件名称：LCD12864.h
* 功    能：12864图形液晶驱动模块 头文件
* 说    明：ST7920控制器，并口8位模式
*******************************************************************************
*/

#ifndef _LCD_12864_H
#define _LCD_12864_H

#include "config.h"

/* 函数声明 */
void InitLcd12864();                                          // 初始化12864液晶
void LcdWriteCmd(uint8 cmd);                                  // 写命令（0x36开启图形层）
void LcdShowString(uint8 x, uint8 y, uint8 *str);            // 在指定位置显示字符串
void LcdShowImage(uint8 x, uint8 y, uint8 w, uint8 h, uint8 *img);  // 显示图像
void LcdClearArea(uint8 x, uint8 y, uint8 w, uint8 h);       // 清除指定区域

#endif
