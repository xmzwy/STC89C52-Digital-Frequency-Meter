/*
*******************************************************************************
*                     ����Ƶ�ʼƣ���Ƶ�����ܣ�
*
* �ļ����ƣ�keyboard.c
* ��    �ܣ�4��4���󰴼�����ģ��
* ˵    ��������KST-51�����壬ʹ������ɨ�跽ʽ��ⰴ��
*          ��������ӳ�䣺
*            1    2    3    Up
*            4    5    6    Left
*            7    8    9    Down
*            0    ESC  Enter Right
*******************************************************************************
*/

#include "config.h"

/* ���󰴼���ŵ���׼���̼����ӳ��� */
unsigned char code KeyCodeMap[4][4] = {
    { '1',  '2',  '3', 0x26 },  // ���ּ�1�����ּ�2�����ּ�3���ϼ�
    { '4',  '5',  '6', 0x25 },  // ���ּ�4�����ּ�5�����ּ�6�����
    { '7',  '8',  '9', 0x28 },  // ���ּ�7�����ּ�8�����ּ�9���¼�
    { '0', 0x1B, 0x0D, 0x27 }   // ���ּ�0��ESC����  �س����� �Ҽ�
};

/* ȫ�����󰴼��ĵ�ǰ״̬��ȫ�ֱ����� */
unsigned char pdata KeySta[4][4] = {
    {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1}
};

/* ��������������������main.c�ж��壩 */
extern void KeyAction(unsigned char keycode);

/*
* �������ƣ�KeyDriver
* ��    �ܣ���������������������ⰴ��������������Ӧ��������
* ע    �⣺������ѭ���е���
*/
void KeyDriver()
{
    unsigned char i, j;
    static unsigned char pdata backup[4][4] = {   // ����ֵ������ǰһ�ε�ֵ
        {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1},  {1, 1, 1, 1}
    };

    for (i = 0; i < 4; i++)      // ѭ�����4��4�ľ��󰴼�
    {
        for (j = 0; j < 4; j++)
        {
            if (backup[i][j] != KeySta[i][j])     // ��ⰴ���仯
            {
                if (backup[i][j] != 0)            // ��������ʱִ�ж���
                {
                    KeyAction(KeyCodeMap[i][j]);  // ���ð�����������
                }
                backup[i][j] = KeySta[i][j];      // ˢ��ǰһ�εı���ֵ
            }
        }
    }
}

/*
* �������ƣ�KeyScan
* ��    �ܣ�����ɨ�躯��
* ע    �⣺���ڶ�ʱ�ж��е��ã��Ƽ����ü��1ms
*          ��������ɨ��+�����������жϰ����ȶ�״̬
*/
void KeyScan()
{
    unsigned char i;
    static unsigned char keyout = 0;               // ���󰴼�ɨ���������
    static unsigned char keybuf[4][4] = {          // ���󰴼�ɨ�軺����
        {0xFF, 0xFF, 0xFF, 0xFF},  {0xFF, 0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF, 0xFF},  {0xFF, 0xFF, 0xFF, 0xFF}
    };

    // ��һ�е�4������ֵ���뻺����
    keybuf[keyout][0] = (keybuf[keyout][0] << 1) | KEY_IN_1;
    keybuf[keyout][1] = (keybuf[keyout][1] << 1) | KEY_IN_2;
    keybuf[keyout][2] = (keybuf[keyout][2] << 1) | KEY_IN_3;
    keybuf[keyout][3] = (keybuf[keyout][3] << 1) | KEY_IN_4;

    /* 2次采样防抖 (20ms), 比原来4次(40ms)更灵敏 */
    for (i = 0; i < 4; i++)
    {
        if ((keybuf[keyout][i] & 0x03) == 0x00)
            KeySta[keyout][i] = 0;
        else if ((keybuf[keyout][i] & 0x03) == 0x03)
            KeySta[keyout][i] = 1;
    }

    // ִ����һ�ε�ɨ�����
    keyout++;                // �����������
    keyout &= 0x03;          // ����ֵ�ӵ�4������
    switch (keyout)          // ���������ͷŵ�ǰ����ţ�������һ�������
    {
        case 0: KEY_OUT_4 = 1; KEY_OUT_1 = 0; break;
        case 1: KEY_OUT_1 = 1; KEY_OUT_2 = 0; break;
        case 2: KEY_OUT_2 = 1; KEY_OUT_3 = 0; break;
        case 3: KEY_OUT_3 = 1; KEY_OUT_4 = 0; break;
        default: break;
    }
}
