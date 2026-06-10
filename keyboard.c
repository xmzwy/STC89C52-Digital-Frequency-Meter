/*
*******************************************************************************
*                     数字频率计（测频、测周）
*
* 文件名称：keyboard.c
* 功    能：4×4矩阵按键驱动模块
* 说    明：基于KST-51开发板，使用行列扫描方式检测按键
*          按键布局映射：
*            1    2    3    Up
*            4    5    6    Left
*            7    8    9    Down
*            0    ESC  Enter Right
*******************************************************************************
*/

#include "config.h"

/* 矩阵按键编号到标准键盘键码的映射表 */
unsigned char code KeyCodeMap[4][4] = {
    { '1',  '2',  '3', 0x26 },  // 数字键1、数字键2、数字键3、上键
    { '4',  '5',  '6', 0x25 },  // 数字键4、数字键5、数字键6、左键
    { '7',  '8',  '9', 0x28 },  // 数字键7、数字键8、数字键9、下键
    { '0', 0x1B, 0x0D, 0x27 }   // 数字键0、ESC键、  回车键、 右键
};

/* 全部矩阵按键的当前状态（全局变量） */
unsigned char pdata KeySta[4][4] = {
    {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1}
};

/* 按键动作函数声明（在main.c中定义） */
extern void KeyAction(unsigned char keycode);

/*
* 函数名称：KeyDriver
* 功    能：按键动作驱动函数，检测按键动作并调用相应动作函数
* 注    意：需在主循环中调用
*/
void KeyDriver()
{
    unsigned char i, j;
    static unsigned char pdata backup[4][4] = {   // 备份值，保存前一次的值
        {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1}
    };

    for (i = 0; i < 4; i++)      // 循环检测4×4的矩阵按键
    {
        for (j = 0; j < 4; j++)
        {
            if (backup[i][j] != KeySta[i][j])     // 检测按键变化
            {
                if (backup[i][j] != 0)            // 按键按下时执行动作
                {
                    KeyAction(KeyCodeMap[i][j]);  // 调用按键动作函数
                }
                backup[i][j] = KeySta[i][j];      // 刷新前一次的备份值
            }
        }
    }
}

/*
* 函数名称：KeyScan
* 功    能：按键扫描函数
* 注    意：需在定时中断中调用，推荐调用间隔1ms
*          利用行列扫描+消抖缓冲区判断按键稳定状态
*/
void KeyScan()
{
    unsigned char i;
    static unsigned char keyout = 0;               // 矩阵按键扫描输出索引
    static unsigned char keybuf[4][4] = {          // 矩阵按键扫描缓冲区
        {0xFF, 0xFF, 0xFF, 0xFF},  {0xFF, 0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF, 0xFF},  {0xFF, 0xFF, 0xFF, 0xFF}
    };

    // 将一行的4个按键值移入缓冲区
    keybuf[keyout][0] = (keybuf[keyout][0] << 1) | KEY_IN_1;
    keybuf[keyout][1] = (keybuf[keyout][1] << 1) | KEY_IN_2;
    keybuf[keyout][2] = (keybuf[keyout][2] << 1) | KEY_IN_3;
    keybuf[keyout][3] = (keybuf[keyout][3] << 1) | KEY_IN_4;

    // 消抖后更新按键状态
    for (i = 0; i < 4; i++)  // 每行4个按键，循环4次
    {
        // 连续4次扫描值都为0，即4×4ms内都是按下状态，可认为按键已稳定按下
        if ((keybuf[keyout][i] & 0x0F) == 0x00)
        {
            KeySta[keyout][i] = 0;
        }
        // 连续4次扫描值都为1，即4×4ms内都是弹起状态，可认为按键已稳定弹起
        else if ((keybuf[keyout][i] & 0x0F) == 0x0F)
        {
            KeySta[keyout][i] = 1;
        }
    }

    // 执行下一次的扫描输出
    keyout++;                // 输出索引递增
    keyout &= 0x03;          // 索引值加到4即归零
    switch (keyout)          // 根据索引释放当前输出脚，拉低下一个输出脚
    {
        case 0: KEY_OUT_4 = 1; KEY_OUT_1 = 0; break;
        case 1: KEY_OUT_1 = 1; KEY_OUT_2 = 0; break;
        case 2: KEY_OUT_2 = 1; KEY_OUT_3 = 0; break;
        case 3: KEY_OUT_3 = 1; KEY_OUT_4 = 0; break;
        default: break;
    }
}
