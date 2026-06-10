/*
*******************************************************************************
*                     ����Ƶ�ʼƣ���Ƶ�����ܣ�
*
* �ļ����ƣ�I2C.c
* ��    �ܣ�I2C��������ģ�飨����ģ�⣩
* ˵    ������������PCF8591 DAC/ADCоƬ����������
*          I2C�������ţ�SCL-P3^7, SDA-P3^6
*******************************************************************************
*/

#include "config.h"

#define I2CDelay()  {_nop_();_nop_();_nop_();_nop_();}

/* ����I2C������ʼ�ź� */
void I2CStart()
{
    I2C_SDA = 1;        // ����ȷ��SDA��SCL���Ǹߵ�ƽ
    I2C_SCL = 1;
    I2CDelay();
    I2C_SDA = 0;        // ������SDA
    I2CDelay();
    I2C_SCL = 0;        // ������SCL
}

/* ����I2C����ֹͣ�ź� */
void I2CStop()
{
    I2C_SCL = 0;        // ����ȷ��SDA��SCL���ǵ͵�ƽ
    I2C_SDA = 0;
    I2CDelay();
    I2C_SCL = 1;        // ������SCL
    I2CDelay();
    I2C_SDA = 1;        // ������SDA
    I2CDelay();
}

/* I2C����д������dat-��д���ֽڣ�����ֵ-�ӻ�Ӧ��λ��ֵ */
bit I2CWrite(unsigned char dat)
{
    bit ack;             // �ݴ�Ӧ��λ��ֵ
    unsigned char mask;  // ̽���ֽ���ĳһλֵ������

    for (mask = 0x80; mask != 0; mask >>= 1)  // �Ӹ�λ����λ���η���
    {
        if ((mask & dat) == 0)   // ��λ��ֵ�����SDA
            I2C_SDA = 0;
        else
            I2C_SDA = 1;
        I2CDelay();
        I2C_SCL = 1;             // ����SCL
        I2CDelay();
        I2C_SCL = 0;             // ����SCL�����һ��λ����
    }
    I2C_SDA = 1;   // 8λ���ݷ�������ͷ�SDA�Լ��ӻ�Ӧ��
    I2CDelay();
    I2C_SCL = 1;   // ����SCL
    ack = I2C_SDA; // ��ȡ��ʱ��SDAֵ����Ϊ�ӻ�Ӧ��ֵ
    I2CDelay();
    I2C_SCL = 0;   // ����SCL��Ӧ��λ��������������

    return (~ack); // Ӧ��ֵȡ���Է���ͨ�����߼���
                   // 0=�ӻ�æ��дʧ�ܣ�1=�ӻ����л�д�ɹ�
}

/* I2C读取一字节，发送NAK（主机不再读取） */
unsigned char I2CReadNAK()
{
    unsigned char mask;
    unsigned char dat = 0;

    I2C_SDA = 1;                     /* 释放SDA让从机控制 */
    for (mask = 0x80; mask != 0; mask >>= 1)
    {
        I2CDelay();
        I2C_SCL = 1;                 /* 主机拉高SCL，从机送出1位数据 */
        if (I2C_SDA == 0)
            dat &= ~mask;
        else
            dat |= mask;
        I2CDelay();
        I2C_SCL = 0;                 /* 拉低SCL，准备下一位 */
    }
    I2C_SDA = 1;                     /* 主机发送NAK（不应答） */
    I2CDelay();
    I2C_SCL = 1;
    I2CDelay();
    I2C_SCL = 0;

    return dat;
}
