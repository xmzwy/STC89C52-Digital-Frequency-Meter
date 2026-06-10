/*
*******************************************************************************
*                     数字频率计（测频、测周）
*
* 文件名称：I2C.c
* 功    能：I2C总线驱动模块（软件模拟）
* 说    明：用于驱动PCF8591 DAC/ADC芯片，产生波形
*          I2C总线引脚：SCL-P3^7, SDA-P3^6
*******************************************************************************
*/

#include "config.h"

#define I2CDelay()  {_nop_();_nop_();_nop_();_nop_();}

/* 产生I2C总线起始信号 */
void I2CStart()
{
    I2C_SDA = 1;        // 首先确保SDA和SCL都是高电平
    I2C_SCL = 1;
    I2CDelay();
    I2C_SDA = 0;        // 先拉低SDA
    I2CDelay();
    I2C_SCL = 0;        // 再拉低SCL
}

/* 产生I2C总线停止信号 */
void I2CStop()
{
    I2C_SCL = 0;        // 首先确保SDA和SCL都是低电平
    I2C_SDA = 0;
    I2CDelay();
    I2C_SCL = 1;        // 先拉高SCL
    I2CDelay();
    I2C_SDA = 1;        // 再拉高SDA
    I2CDelay();
}

/* I2C总线写操作，dat-待写入字节，返回值-从机应答位的值 */
bit I2CWrite(unsigned char dat)
{
    bit ack;             // 暂存应答位的值
    unsigned char mask;  // 探测字节内某一位值的掩码

    for (mask = 0x80; mask != 0; mask >>= 1)  // 从高位到低位依次发送
    {
        if ((mask & dat) == 0)   // 该位的值输出到SDA
            I2C_SDA = 0;
        else
            I2C_SDA = 1;
        I2CDelay();
        I2C_SCL = 1;             // 拉高SCL
        I2CDelay();
        I2C_SCL = 0;             // 拉低SCL，完成一个位传输
    }
    I2C_SDA = 1;   // 8位数据发送完后，释放SDA以检测从机应答
    I2CDelay();
    I2C_SCL = 1;   // 拉高SCL
    ack = I2C_SDA; // 读取此时的SDA值，即为从机应答值
    I2CDelay();
    I2C_SCL = 0;   // 拉低SCL，应答位结束并锁定总线

    return (~ack); // 应答值取反以符合通常的逻辑：
                   // 0=从机忙或写失败，1=从机空闲或写成功
}

/* I2C总线读操作——发送非应答信号，返回值-读到的字节 */
unsigned char I2CReadNAK()
{
    unsigned char mask;
    unsigned char dat;

    I2C_SDA = 1;  // 首先确保主机释放SDA
    for (mask = 0x80; mask != 0; mask >>= 1)  // 从高位到低位依次接收
    {
        I2CDelay();
        I2C_SCL = 1;       // 拉高SCL
        if (I2C_SDA == 0)  // 读取SDA的值
            dat &= ~mask;  // 为0时dat中对应位清零
        else
            dat |= mask;   // 为1时dat中对应位置1
        I2CDelay();
        I2C_SCL = 0;       // 拉低SCL，使从机发送出下一位
    }
    I2C_SDA = 1;    // 8位数据接收完毕后，拉高SDA发送非应答信号
    I2CDelay();
    I2C_SCL = 1;    // 拉高SCL
    I2CDelay();
    I2C_SCL = 0;    // 拉低SCL完成非应答位并锁定总线

    return dat;
}

/* I2C总线读操作——发送应答信号，返回值-读到的字节 */
unsigned char I2CReadACK()
{
    unsigned char mask;
    unsigned char dat;

    I2C_SDA = 1;  // 首先确保主机释放SDA
    for (mask = 0x80; mask != 0; mask >>= 1)  // 从高位到低位依次接收
    {
        I2CDelay();
        I2C_SCL = 1;       // 拉高SCL
        if (I2C_SDA == 0)  // 读取SDA的值
            dat &= ~mask;  // 为0时dat中对应位清零
        else
            dat |= mask;   // 为1时dat中对应位置1
        I2CDelay();
        I2C_SCL = 0;       // 拉低SCL，使从机发送出下一位
    }
    I2C_SDA = 0;    // 8位数据接收完毕后，拉低SDA发送应答信号
    I2CDelay();
    I2C_SCL = 1;    // 拉高SCL
    I2CDelay();
    I2C_SCL = 0;    // 拉低SCL完成应答位并锁定总线

    return dat;
}
