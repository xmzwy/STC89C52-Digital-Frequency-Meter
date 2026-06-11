#include "config.h"
#include "LCD12864.h"

unsigned char code SquareWave[] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

unsigned char xdata adcBuf[64];
unsigned char adcIdx;
bit adcReady;

bit flag200ms   = 0;
bit flagMeasure = 0;
bit waveOn      = 1;
bit showWave    = 0;

unsigned char idata mode     = 0;
volatile unsigned char idata waveFreq = 10;
volatile unsigned long xdata measuredFreq;
volatile unsigned long xdata measuredPeriod;
unsigned int  xdata zcCount;
unsigned char idata zcBufCnt;

unsigned char idata sigMin;
unsigned char idata sigMax;
unsigned char idata sigAvg;
unsigned char idata sigThr;

unsigned char idata T0RH, T0RL;
unsigned char idata T1RH, T1RL;
unsigned char idata strBuf[17];
unsigned char xdata yDisp[128];

extern void KeyScan();
extern void KeyDriver();
extern void I2CStart();
extern void I2CStop();
extern bit  I2CWrite(unsigned char dat);
extern unsigned char I2CReadNAK();

void ConfigTimer0();
void ConfigTimer1(uint8 ms);
void ConfigTimer2();
void SetWaveFreq(uint8 freq);
void SetDACOut(uint8 val);
void UpdateDisplay();
void DrawWaveform();
uint8 NumberToString(uint8 *str, uint32 num);
uint8 ReadADC(uint8 chn);

void StrFill(uint8 *buf, uint8 len)
{
    uint8 i;
    for (i = 0; i < len; i++) buf[i] = ' ';
    buf[len] = '\0';
}

void StrCopy(uint8 *dst, uint8 pos, uint8 *src, uint8 maxlen)
{
    uint8 i;
    for (i = 0; src[i] != '\0' && i < maxlen; i++)
        dst[pos + i] = src[i];
}

void StrCopyLit(uint8 *dst, uint8 pos, uint8 *lit)
{
    while (*lit != '\0') dst[pos++] = *lit++;
}

uint8 NumberToString(uint8 *str, uint32 num)
{
    uint8 i = 0, len;
    uint8 buf[12];

    if (num == 0) { *str++ = '0'; *str = '\0'; return 1; }
    do { buf[i++] = num % 10; num /= 10; } while (num > 0);
    len = i;
    while (i-- > 0) *str++ = buf[i] + '0';
    *str = '\0';
    return len;
}

uint8 ReadADC(uint8 chn)
{
    uint8 val;

    I2CStart();
    I2CWrite(0x48 << 1);
    I2CWrite(0x40 | (chn & 0x03));
    I2CStop();

    { uint8 t; for (t = 0; t < 20; t++) _nop_(); }

    I2CStart();
    I2CWrite((0x48 << 1) | 0x01);
    val = I2CReadNAK();
    I2CStop();

    return val;
}

void SetDACOut(uint8 val)
{
    I2CStart();
    if (!I2CWrite(0x48 << 1)) { I2CStop(); return; }
    I2CWrite(0x40);
    I2CWrite(val);
    I2CStop();
}

void SetWaveFreq(uint8 freq)
{
    unsigned long tmp;

    if (freq < 1)  freq = 1;
    if (freq > 60) freq = 60;
    waveFreq = freq;

    tmp = (11059200UL / 12) / ((unsigned long)freq * 32);
    tmp = 65536 - tmp + 36;
    T1RH = (unsigned char)(tmp >> 8);
    T1RL = (unsigned char)tmp;

    TMOD &= 0x0F;
    TMOD |= 0x10;
    TH1 = T1RH; TL1 = T1RL;
    ET1 = 1;
    PT1 = 0;
    if (waveOn) TR1 = 1; else TR1 = 0;
}

void ConfigTimer0(void)
{
    unsigned long tmp;
    tmp = 11059200UL / 12;
    tmp = (tmp * 10) / 1000;
    tmp = 65536 - tmp + 28;
    T0RH = (unsigned char)(tmp >> 8);
    T0RL = (unsigned char)tmp;
    TMOD &= 0xF0;
    TMOD |= 0x01;
    TH0 = T0RH; TL0 = T0RL;
    ET0 = 1;
    PT0 = 0;
    TR0 = 1;
}

void ConfigTimer1(uint8 ms)
{
}

void ConfigTimer2()
{
    T2MOD = 0x00;
    T2CON = 0x00;
    RCAP2H = (65536 - 1382) >> 8;
    RCAP2L = (65536 - 1382);
    TH2 = RCAP2H;
    TL2 = RCAP2L;
    ET2 = 1;
    PT2 = 0;
}

void ISR_Timer0() interrupt 1
{
    static uint8 cnt200ms = 0;
    static uint8 cnt1s    = 0;

    TH0 = T0RH; TL0 = T0RL;
    KeyScan();

    cnt200ms++;  cnt1s++;
    if (cnt200ms >= 20)  { cnt200ms = 0; flag200ms  = 1; }
    if (cnt1s    >= 100) { cnt1s    = 0; flagMeasure = 1; }
}

void ISR_Timer1() interrupt 3
{
    static unsigned char dacIdx = 0;

    TH1 = T1RH; TL1 = T1RL;
    if (mode == 0 && waveOn)
    {
        SetDACOut(SquareWave[dacIdx]);
        dacIdx++;
        if (dacIdx >= 32) dacIdx = 0;
    }
}

void ISR_Timer2() interrupt 5
{
    TF2 = 0;

    if (mode == 1)
    {
        if (adcIdx < 64)
        {
            adcBuf[adcIdx] = ReadADC(1);
            adcIdx++;
            if (adcIdx >= 64)
            {
                adcReady = 1;
                TR2 = 0;
            }
        }
    }
}

void main()
{
    uint32 lastFreq = 0;

    EA = 1;

    ConfigTimer0();
    ConfigTimer2();
    InitLcd12864();
    SetWaveFreq(waveFreq);

    measuredFreq   = waveFreq;
    measuredPeriod = 1000000UL / waveFreq;
    sigMin = 127; sigMax = 127;
    sigAvg = 127; sigThr = 127;

    LcdShowString(0, 0, "  Digital Freq  ");
    UpdateDisplay();

    while (1)
    {
        KeyDriver();

        if (adcReady)
        {
            uint8 i;
            uint8 prev, curr;
            uint16 sum;

            adcReady = 0;

            sum = 0;
            sigMin = 255;
            sigMax = 0;
            for (i = 0; i < 64; i++)
            {
                curr = adcBuf[i];
                sum += curr;
                if (curr < sigMin) sigMin = curr;
                if (curr > sigMax) sigMax = curr;
            }
            sigAvg = (uint8)(sum / 64);

            {
                static uint16 thrS = 127 * 8;
                thrS = (thrS * 7 + (uint16)sigAvg * 8) / 8;
                sigThr = (uint8)(thrS / 8);
            }
            if (sigMax <= sigMin) sigMax = sigMin + 1;

            if (mode == 1)
            {
                uint8 edges = 0;
                prev = adcBuf[0];
                for (i = 1; i < 64; i++)
                {
                    curr = adcBuf[i];
                    if (prev < sigThr && curr >= sigThr)
                        edges++;
                    prev = curr;
                }
                zcCount  += edges;
                zcBufCnt++;
            }

            adcIdx = 0; TR2 = 1;
        }

        if (flagMeasure)
        {
            flagMeasure = 0;

            if (mode == 0)
            {
                measuredFreq = waveFreq;
            }
            else if (zcBufCnt > 0)
            {

                measuredFreq = (uint32)zcCount * 125
                             / ((uint16)zcBufCnt * 12);
            }
            else
            {
                measuredFreq = 0;
            }

            if (measuredFreq > 0)
                measuredPeriod = 1000000UL / measuredFreq;
            else
                measuredPeriod = 0;

            zcCount = 0; zcBufCnt = 0;

            if (!showWave)
                UpdateDisplay();
            else if (mode == 1)
                DrawWaveform();
            if (measuredFreq != lastFreq)
                lastFreq = measuredFreq;
        }

        if (flag200ms)
        {
            flag200ms = 0;
            if (!showWave) UpdateDisplay();
        }
    }
}

void KeyAction(unsigned char keycode)
{
    if (keycode == '0')
    {
        showWave = !showWave;
        LcdWriteCmd(0x30); LcdWriteCmd(0x01);
        LcdClearArea(0, 0, 128, 64);
        if (showWave)
            DrawWaveform();
        else
        {
            LcdShowString(0, 0, "  Digital Freq  ");
            UpdateDisplay();
        }
        return;
    }

    if (keycode == 0x0D)
    {
        if (mode == 0)
        {
            uint8 t;
            mode = 1; waveOn = 0;
            TR1 = 0; TR2 = 0;

            for (t = 0; t < 3; t++)
            {
                I2CStart();
                I2CWrite(0x48 << 1);
                I2CWrite(0x00);
                I2CStop();
            }
            ConfigTimer2();

            for (t = 0; t < 5; t++)
                ReadADC(1);
            adcIdx = 0; adcReady = 0;
            measuredFreq = 0; measuredPeriod = 0;
            zcCount = 0; zcBufCnt = 0;
            TR2 = 1;
        }
        else
        {
            mode = 0; waveOn = 1;
            TR2 = 0; TR1 = 0;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x1B)
    {
        if (mode == 0)
        {
            waveOn = !waveOn;
            if (waveOn) SetWaveFreq(waveFreq);
            else        TR1 = 0;
        }
    }
    else if (keycode == 0x26 && mode == 0 && waveFreq <= 50)
        { waveFreq += 10; SetWaveFreq(waveFreq);
          measuredFreq = waveFreq; measuredPeriod = 1000000UL/waveFreq; }
    else if (keycode == 0x28 && mode == 0 && waveFreq > 10)
        { waveFreq -= 10; SetWaveFreq(waveFreq);
          measuredFreq = waveFreq; measuredPeriod = 1000000UL/waveFreq; }
    else if (keycode == 0x25 && mode == 0 && waveFreq < 60)
        { waveFreq += 1;  SetWaveFreq(waveFreq);
          measuredFreq = waveFreq; measuredPeriod = 1000000UL/waveFreq; }
    else if (keycode == 0x27 && mode == 0 && waveFreq > 1)
        { waveFreq -= 1;  SetWaveFreq(waveFreq);
          measuredFreq = waveFreq; measuredPeriod = 1000000UL/waveFreq; }

    if (showWave) DrawWaveform();
    else          UpdateDisplay();
}

void UpdateDisplay()
{
    uint8 numStr[12];

    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Freq: ");
    NumberToString(numStr, measuredFreq);
    StrCopy(strBuf, 6, numStr, 6);
    StrCopyLit(strBuf, 13, " Hz");
    LcdShowString(0, 16, strBuf);

    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Peri: ");
    if (measuredPeriod == 0)
        StrCopyLit(strBuf, 6, "----");
    else if (measuredPeriod >= 1000000UL)
    {
        NumberToString(numStr, measuredPeriod / 1000);
        StrCopy(strBuf, 6, numStr, 6);
        StrCopyLit(strBuf, 13, " ms");
    }
    else
    {
        NumberToString(numStr, measuredPeriod);
        StrCopy(strBuf, 6, numStr, 6);
        StrCopyLit(strBuf, 13, " us");
    }
    LcdShowString(0, 32, strBuf);

    StrFill(strBuf, 16);
    if (mode == 0)
    {
        StrCopyLit(strBuf, 0, "Mode: Square F=");
        NumberToString(numStr, waveFreq);
        StrCopy(strBuf, 14, numStr, 2);
    }
    else
    {

        StrCopyLit(strBuf, 0, "L");
        NumberToString(numStr, sigMin);
        StrCopy(strBuf, 1, numStr, 3);
        StrCopyLit(strBuf, 4, " A");
        NumberToString(numStr, sigAvg);
        StrCopy(strBuf, 6, numStr, 3);
        StrCopyLit(strBuf, 9, " H");
        NumberToString(numStr, sigMax);
        StrCopy(strBuf, 11, numStr, 3);
    }
    LcdShowString(0, 48, strBuf);
}

void DrawWaveform()
{
    uint8 col, row, i;
    uint8 buf[16];
    uint8 numStr[12];
    uint8 ya, yb, yt;
    uint16 adcRange;

    if (mode == 0)
    {
        uint8 half;
        uint8 pos = 0;
        uint8 level = 255;

        if (waveFreq > 0)
            half = 333 / waveFreq;
        else
            half = 32;

        if (half < 2) half = 2;
        if (half > 32) half = 32;

        while (pos < 64)
        {
            uint8 n;
            for (n = 0; n < half && pos < 64; n++)
                adcBuf[pos++] = level;
            level = (level == 255) ? 0 : 255;
        }
        sigMin = 0; sigMax = 255;
    }

    {
        uint8 dMin, dMax;
        uint16 pad = ((uint16)sigMax - (uint16)sigMin) / 16;

        dMin = (sigMin >= pad) ? (sigMin - pad) : 0;
        dMax = sigMax + pad;
        if (dMax < sigMax || dMax > 253) dMax = 255;

        adcRange = (uint16)dMax - (uint16)dMin;
        if (adcRange < 2) adcRange = 2;

        for (i = 0; i < 64; i++)
        {
            uint8 adc = adcBuf[i];
            if (adc <= dMin)
                yDisp[i * 2] = 63;
            else if (adc >= dMax)
                yDisp[i * 2] = 16;
            else
                yDisp[i * 2] = 63 - (uint8)((uint16)(adc - dMin) * 47 / adcRange);
        }
    }

    for (i = 0; i < 63; i++)
    {
        uint8 ya = yDisp[i * 2];
        uint8 yb = yDisp[(i + 1) * 2];
        int16 d = (int16)ya - (int16)yb;
        if (d < 0) d = -d;

        if (d > 20)
            yDisp[i * 2 + 1] = ya;
        else
            yDisp[i * 2 + 1] = (uint8)((uint16)(ya + yb) / 2);
    }
    yDisp[127] = yDisp[126];

    LcdClearArea(0, 16, 128, 48);

    for (row = 16; row <= 63; row++)
    {
        for (col = 0; col < 16; col++) buf[col] = 0;

        for (col = 0; col < 127; col++)
        {
            ya = yDisp[col];
            yb = yDisp[col + 1];
            if (ya > yb) { yt = ya; ya = yb; yb = yt; }

            if (row >= ya && row <= yb)
                buf[col / 8] |= (0x80 >> (col % 8));
        }

        LcdShowImage(0, row, 128, 1, buf);
    }

    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "F:");
    NumberToString(numStr, measuredFreq);
    StrCopy(strBuf, 2, numStr, 4);
    StrCopyLit(strBuf, 6, " L");
    NumberToString(numStr, sigMin);
    StrCopy(strBuf, 8, numStr, 3);
    StrCopyLit(strBuf, 11, " H");
    NumberToString(numStr, sigMax);
    StrCopy(strBuf, 13, numStr, 3);
    LcdShowString(0, 0, strBuf);
    LcdWriteCmd(0x36);
}
