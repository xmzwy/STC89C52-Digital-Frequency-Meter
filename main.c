/*
*******************************************************************************
*                     数字频率计（测频、测周）
*   MCU: STC89C52RC    晶振: 11.0592MHz
*   液晶: 12864 (ST7920控制器, 并口8位)
*   ADC: PCF8591 (I2C接口, AIN1通道)
*
*   【接线】
*   信号发生器 红线 → PCF8591 AIN1
*   信号发生器 黑线 → 电路 GND（必须共地！）
*   PCF8591 SCL → P3^7,  SDA → P3^6
*   AOUT→AIN1 跳线仅在内部方波测试时连接
*
*   【按键】
*   [0]     文字/波形 切换
*   [Enter] 内部方波/外部信号 切换
*   [↑][↓] 频率 ±10Hz   [←][→] 频率 ±1Hz
*   [ESC]   启停内部方波输出
*
*   【定时器】
*   T0 - 外部计数器(P3^4)
*   T1 - 10ms 系统时钟 + 按键扫描
*   T2 - ADC 采样定时器, 1.5ms/次 (667Hz)
*******************************************************************************
*/

#include "config.h"
#include "LCD12864.h"

/* ==================== 内部方波查找表 (32点, 占空比50%) =============== */
unsigned char code SquareWave[] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* ==================== ADC 采样缓冲区 (XRAM) ==================== */
unsigned char xdata adcBuf[64];    /* 64个采样 × 1.5ms = 96ms */
unsigned char adcIdx;              /* 当前写入位置 */
bit adcReady;                      /* 缓冲区满标志 */

/* ==================== 全局变量 ==================== */
bit flag200ms   = 0;               /* 200ms 定时标志 */
bit flagMeasure = 0;               /* 1秒测量标志 */
bit waveOn      = 1;               /* 内部方波开关 */
bit showWave    = 0;               /* 0=文字界面, 1=波形界面 */

unsigned char idata mode     = 0;  /* 0=内部方波, 1=外部信号 */
unsigned char idata waveFreq = 10; /* 内部方波频率 (1~50Hz) */
unsigned long xdata measuredFreq;  /* 测得的频率 */
unsigned long xdata measuredPeriod;/* 测得的周期(us) */
unsigned int  xdata zcCount;       /* 1秒内过零累计 */
unsigned char idata zcBufCnt;      /* 1秒内缓冲区个数 */

/* 信号统计量（每个96ms缓冲区计算一次）*/
unsigned char idata sigMin;        /* 缓冲区最小值 → 波形底部 */
unsigned char idata sigMax;        /* 缓冲区最大值 → 波形顶部 */
unsigned char idata sigAvg;        /* 缓冲区平均值 → 过零检测阈值 */
unsigned char idata sigThr;        /* 过零比较阈值 (=sigAvg) */

unsigned char idata T1RH, T1RL;    /* T1重载值(10ms) */
unsigned char idata strBuf[17];    /* 字符串缓冲区 */
unsigned char xdata yDisp[128];    /* 128列屏幕Y坐标 */

/* ==================== 外部函数声明 ==================== */
extern void KeyScan();
extern void KeyDriver();
extern void I2CStart();
extern void I2CStop();
extern bit  I2CWrite(unsigned char dat);
extern unsigned char I2CReadNAK();

/* ==================== 函数声明 ==================== */
void ConfigTimer0();
void ConfigTimer1(uint8 ms);
void ConfigTimer2();
void SetWaveFreq(uint8 freq);
void SetDACOut(uint8 val);
void UpdateDisplay();
void DrawWaveform();
uint8 NumberToString(uint8 *str, uint32 num);
uint8 ReadADC(uint8 chn);

/* ==================== 字符串工具函数 ==================== */

/* 用空格填满缓冲区 */
void StrFill(uint8 *buf, uint8 len)
{
    uint8 i;
    for (i = 0; i < len; i++) buf[i] = ' ';
    buf[len] = '\0';
}

/* 复制字符串（最多maxlen个字符）*/
void StrCopy(uint8 *dst, uint8 pos, uint8 *src, uint8 maxlen)
{
    uint8 i;
    for (i = 0; src[i] != '\0' && i < maxlen; i++)
        dst[pos + i] = src[i];
}

/* 复制字符串字面量 */
void StrCopyLit(uint8 *dst, uint8 pos, uint8 *lit)
{
    while (*lit != '\0') dst[pos++] = *lit++;
}

/* 数字转字符串，返回长度 */
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

/* ==================== PCF8591 ADC 读取 ==================== */
/*
 * PCF8591 控制字节格式:
 *   bit7=0, bit6=1(DAC使能), bit5-4=00(单端),
 *   bit3=0, bit2=0, bit1-0=通道号
 * 使用 0x40|通道号 = DAC使能 + 单端输入
 */
uint8 ReadADC(uint8 chn)
{
    uint8 val;

    /* 第1步: 写入控制字节，启动ADC转换 */
    I2CStart();
    I2CWrite(0x48 << 1);                 /* 器件地址 + 写 */
    I2CWrite(0x40 | (chn & 0x03));       /* DAC开, 单端, 通道N */
    I2CStop();

    /* 第2步: 等待转换完成 (PCF8591约需90μs) */
    { uint8 t; for (t = 0; t < 20; t++) _nop_(); }

    /* 第3步: 读取转换结果 */
    I2CStart();
    I2CWrite((0x48 << 1) | 0x01);        /* 器件地址 + 读 */
    val = I2CReadNAK();                   /* 读一字节, 发送NAK */
    I2CStop();

    return val;
}

/* ==================== PCF8591 DAC 输出 ==================== */
/* 内部方波模式: 用DAC输出模拟电压 */
void SetDACOut(uint8 val)
{
    I2CStart();
    if (!I2CWrite(0x48 << 1)) { I2CStop(); return; }
    I2CWrite(0x40);                      /* DAC使能 */
    I2CWrite(val);                       /* DAC输出值 */
    I2CStop();
}

/* ==================== 内部方波频率设置 ==================== */
/*
 * 用T2定时器产生方波。
 * 方波表有32个点, 每个点对应一次DAC输出。
 * T2频率 = 方波频率 × 32
 * 最大50Hz: I2C一次写入约400μs, 再快ISR来不及完成。
 */
void SetWaveFreq(uint8 freq)
{
    unsigned long tmp;

    if (freq < 1)  freq = 1;
    if (freq > 30) freq = 30;            /* I2C速度限制, 保证按键响应 */
    waveFreq = freq;

    /* 计算T2重载值: 11059200/12 / (freq*32) */
    tmp = (11059200UL / 12) / ((unsigned long)freq * 32);
    tmp = 65536 - tmp + 36;

    T2MOD = 0x00;
    T2CON = 0x00;
    RCAP2H = (unsigned char)(tmp >> 8);
    RCAP2L = (unsigned char)tmp;
    TH2 = RCAP2H;
    TL2 = RCAP2L;
    ET2 = 1;
    PT2 = 1;
    if (waveOn) TR2 = 1;
}

/* ==================== 定时器初始化 ==================== */

void ConfigTimer0()                      /* T0: 外部计数器 */
{
    TMOD &= 0xF0;
    TMOD |= 0x05;
    TH0 = 0; TL0 = 0;
    ET0 = 1; TR0 = 1;
}

void ConfigTimer1(uint8 ms)              /* T1: 系统时钟 */
{
    unsigned long tmp;
    tmp = 11059200UL / 12;
    tmp = (tmp * ms) / 1000;
    tmp = 65536 - tmp + 28;
    T1RH = (unsigned char)(tmp >> 8);
    T1RL = (unsigned char)tmp;
    TMOD &= 0x0F;
    TMOD |= 0x10;
    TH1 = T1RH; TL1 = T1RL;
    ET1 = 1; TR1 = 1;
}

/*
 * T2: ADC采样定时器, 固定1.5ms周期 (667Hz)
 * 重载值 = 65536 - 1382 = 64154
 * 奈奎斯特极限 ~333Hz, 推荐测试频率 50~200Hz
 */
void ConfigTimer2()
{
    T2MOD = 0x00;
    T2CON = 0x00;
    RCAP2H = (65536 - 1382) >> 8;
    RCAP2L = (65536 - 1382);
    TH2 = RCAP2H;
    TL2 = RCAP2L;
    ET2 = 1;
    PT2 = 1;
}

/* ==================== 中断服务程序 ==================== */

void ISR_Timer0() interrupt 1            /* T0中断: 外部脉冲计数 */
{
}

void ISR_Timer1() interrupt 3            /* T1中断: 10ms系统时钟 */
{
    static uint8 cnt200ms = 0;
    static uint8 cnt1s    = 0;

    TH1 = T1RH; TL1 = T1RL;             /* 重载初值 */
    KeyScan();                           /* 扫描按键 */

    cnt200ms++;  cnt1s++;
    if (cnt200ms >= 20) { cnt200ms = 0; flag200ms  = 1; }
    if (cnt1s    >= 100) { cnt1s    = 0; flagMeasure = 1; }
}

/*
 * T2中断: 667Hz触发
 *   内部模式: 输出DAC方波
 *   外部模式: 读取ADC采样 → 填满64点缓冲区
 */
void ISR_Timer2() interrupt 5
{
    static unsigned char dacIdx = 0;

    TF2 = 0;                             /* 清除中断标志 */

    if (mode == 0)                       /* 内部方波模式 */
    {
        SetDACOut(SquareWave[dacIdx]);   /* 输出DAC电平 */
        dacIdx++;
        if (dacIdx >= 32) dacIdx = 0;
    }
    else                                 /* 外部信号模式 */
    {
        if (adcIdx < 64)
        {
            adcBuf[adcIdx] = ReadADC(1); /* 读AIN1通道 */
            adcIdx++;
            if (adcIdx >= 64)
            {
                adcReady = 1;            /* 通知主循环处理 */
                TR2 = 0;                 /* 暂停采样 */
            }
        }
    }
}

/* ==================== 主函数 ==================== */
void main()
{
    uint32 lastFreq = 0;

    EA = 1;                              /* 开全局中断 */

    ConfigTimer0();
    ConfigTimer1(10);                    /* 10ms定时 */
    ConfigTimer2();
    InitLcd12864();                      /* 液晶初始化 */
    SetWaveFreq(waveFreq);               /* 默认10Hz方波 */

    measuredFreq   = waveFreq;
    measuredPeriod = 1000000UL / waveFreq;
    sigMin = 127; sigMax = 127;          /* 信号统计初值 */
    sigAvg = 127; sigThr = 127;

    LcdShowString(0, 0, "  Digital Freq  ");
    UpdateDisplay();

    while (1)
    {
        KeyDriver();                     /* 处理按键 */

        /* ---- 缓冲区满, 处理数据 ---- */
        if (adcReady)
        {
            uint8 i;
            uint8 prev, curr;
            uint16 sum;

            adcReady = 0;

            /* 计算最小值、最大值、平均值 */
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
            sigThr = sigAvg;             /* 过零检测用平均值做阈值 */
            if (sigMax <= sigMin) sigMax = sigMin + 1;

            /* 外部模式: 统计过零次数 (用于测频率) */
            if (mode == 1)
            {
                uint8 edges = 0;
                prev = adcBuf[0];
                for (i = 1; i < 64; i++)
                {
                    curr = adcBuf[i];
                    /* 上升沿: 前一个<阈值, 当前>=阈值 */
                    if (prev < sigThr && curr >= sigThr)
                        edges++;
                    prev = curr;
                }
                zcCount  += edges;       /* 累加过零次数 */
                zcBufCnt++;              /* 累加缓冲区个数 */
            }

            if (showWave) DrawWaveform();/* 刷新波形 */

            adcIdx = 0; TR2 = 1;         /* 重启ADC采样 */
        }

        /* ---- 1秒到, 计算频率 ---- */
        if (flagMeasure)
        {
            flagMeasure = 0;

            if (mode == 0)
            {
                measuredFreq = waveFreq; /* 内部模式: 直接取设定值 */
            }
            else if (zcBufCnt > 0)
            {
                /*
                 * 频率计算公式:
                 * 每个缓冲区 = 64×1.5ms = 96ms
                 * 总时间 = zcBufCnt × 0.096秒
                 * 频率 = 过零次数 / 总时间
                 *      = zcCount / (zcBufCnt×0.096)
                 *      = zcCount × 125 / (zcBufCnt×12)
                 */
                measuredFreq = (uint32)zcCount * 125
                             / ((uint16)zcBufCnt * 12);
            }
            else
            {
                measuredFreq = 0;
            }

            /* 计算周期 (微秒) */
            if (measuredFreq > 0)
                measuredPeriod = 1000000UL / measuredFreq;
            else
                measuredPeriod = 0;

            zcCount = 0; zcBufCnt = 0;   /* 重置累加器 */

            if (measuredFreq != lastFreq)
            {
                lastFreq = measuredFreq;
                if (!showWave) UpdateDisplay();
                else if (mode == 1) DrawWaveform();
            }
        }

        /* ---- 200ms 刷新文字 ---- */
        if (flag200ms)
        {
            uint8 numStr[12];
            flag200ms = 0;
            if (!showWave)
                UpdateDisplay();
            else
            {
                /* 波形模式下只刷新顶部频率文字(快) */
                StrFill(strBuf, 16);
                StrCopyLit(strBuf, 0, "F:");
                NumberToString(numStr, measuredFreq);
                StrCopy(strBuf, 2, numStr, 4);
                LcdShowString(0, 0, strBuf);
            }
        }
    }
}

/* ==================== 按键处理 ==================== */
void KeyAction(unsigned char keycode)
{
    if (keycode == '0')                  /* 切换文字/波形 */
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

    if (keycode == 0x0D)                 /* Enter: 切换内部/外部模式 */
    {
        if (mode == 0)                   /* 内部 → 外部 */
        {
            mode = 1; waveOn = 0;
            TR2 = 0;
            /* 关DAC, 设为ADC采样模式 */
            I2CStart();
            I2CWrite(0x48 << 1);
            I2CWrite(0x00);              /* DAC关 */
            I2CStop();
            ConfigTimer2();
            ReadADC(1);                  /* 预热PCF8591流水线 */
            adcIdx = 0; adcReady = 0;
            measuredFreq = 0;
            zcCount = 0; zcBufCnt = 0;
            TR2 = 1;
        }
        else                             /* 外部 → 内部 */
        {
            mode = 0; waveOn = 1;
            TR2 = 0;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x1B)            /* ESC: 启停内部方波 */
    {
        if (mode == 0)
        {
            waveOn = !waveOn;
            if (waveOn) SetWaveFreq(waveFreq);
            else        TR2 = 0;
        }
    }
    else if (keycode == 0x26 && mode == 0 && waveFreq <= 20)   /* ↑ +10Hz */
        { waveFreq += 10; SetWaveFreq(waveFreq); }
    else if (keycode == 0x28 && mode == 0 && waveFreq > 10)   /* ↓ -10Hz */
        { waveFreq -= 10; SetWaveFreq(waveFreq); }
    else if (keycode == 0x25 && mode == 0 && waveFreq < 30)   /* ← +1Hz */
        { waveFreq += 1;  SetWaveFreq(waveFreq); }
    else if (keycode == 0x27 && mode == 0 && waveFreq > 1)    /* → -1Hz */
        { waveFreq -= 1;  SetWaveFreq(waveFreq); }

    if (showWave) DrawWaveform();
    else          UpdateDisplay();
}

/* ==================== 文字界面显示 ==================== */
void UpdateDisplay()
{
    uint8 numStr[12];

    /* 第2行: 频率 */
    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Freq: ");
    NumberToString(numStr, measuredFreq);
    StrCopy(strBuf, 6, numStr, 6);
    StrCopyLit(strBuf, 13, " Hz");
    LcdShowString(0, 16, strBuf);

    /* 第3行: 周期 */
    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Peri: ");
    if (measuredPeriod == 0)
        StrCopyLit(strBuf, 6, "----");
    else if (measuredPeriod >= 1000000UL)        /* >=1秒 → 显示ms */
    {
        NumberToString(numStr, measuredPeriod / 1000);
        StrCopy(strBuf, 6, numStr, 6);
        StrCopyLit(strBuf, 13, " ms");
    }
    else                                        /* <1秒 → 显示us */
    {
        NumberToString(numStr, measuredPeriod);
        StrCopy(strBuf, 6, numStr, 6);
        StrCopyLit(strBuf, 13, " us");
    }
    LcdShowString(0, 32, strBuf);

    /* 第4行: 模式 / 信号统计 */
    StrFill(strBuf, 16);
    if (mode == 0)
    {
        StrCopyLit(strBuf, 0, "Mode: Square F=");
        NumberToString(numStr, waveFreq);
        StrCopy(strBuf, 14, numStr, 2);
    }
    else
    {
        /* L=最小值 A=平均值 H=最大值 (ADC: 0~255) */
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

/* ==================== 波形显示 ==================== */
/*
 * 显示区域: y=16~63 (48像素)
 * 顶部y=0 显示频率和信号范围文字
 *
 * 处理流程:
 *   1. 64个ADC采样 → 计算Y坐标 (自动缩放+小留白)
 *   2. 线性插值填满128列 (方波大跳变不插值, 正弦波插值平滑)
 *   3. 逐行连线绘制
 */
void DrawWaveform()
{
    uint8 col, row, i;
    uint8 buf[16];
    uint8 numStr[12];
    uint8 ya, yb, yt;
    uint16 adcRange;

    /*
     * 内部模式: 没有ADC数据, 合成一个干净的方波
     * 32点/周期 × 2周期 = 64点 (16高+16低+16高+16低)
     */
    if (mode == 0)
    {
        for (i = 0; i < 16; i++) adcBuf[i]      = 255;  /* 高电平 */
        for (i = 16; i < 32; i++) adcBuf[i]      = 0;    /* 低电平 */
        for (i = 32; i < 48; i++) adcBuf[i]      = 255;  /* 高电平 */
        for (i = 48; i < 64; i++) adcBuf[i]      = 0;    /* 低电平 */
        sigMin = 0; sigMax = 255;                        /* 满幅 */
    }

    /*
     * 第1步: 自动缩放
     * sigMin→屏幕底部(y=63), sigMax→屏幕顶部(y=16)
     * 加6%留白, 波形不贴边
     */
    {
        uint8 dMin, dMax;
        uint16 pad = ((uint16)sigMax - (uint16)sigMin) / 16;

        dMin = (sigMin >= pad) ? (sigMin - pad) : 0;
        dMax = sigMax + pad;
        if (dMax < sigMax || dMax > 253) dMax = 255;

        adcRange = (uint16)dMax - (uint16)dMin;
        if (adcRange < 2) adcRange = 2;

        /* 64个采样点 → 偶数列 (0,2,4,...,126) */
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

    /*
     * 第2步: 智能插值 (填充奇数列)
     * 相邻采样Y差≤20 → 线性插值 (正弦波平滑)
     * 相邻采样Y差>20  → 不插值   (方波边沿陡峭)
     */
    for (i = 0; i < 63; i++)
    {
        uint8 ya = yDisp[i * 2];
        uint8 yb = yDisp[(i + 1) * 2];
        int16 d = (int16)ya - (int16)yb;
        if (d < 0) d = -d;

        if (d > 20)
            yDisp[i * 2 + 1] = ya;                          /* 方波: 不插值 */
        else
            yDisp[i * 2 + 1] = (uint8)((uint16)(ya + yb) / 2); /* 平滑: 取中值 */
    }
    yDisp[127] = yDisp[126];

    LcdClearArea(0, 16, 128, 48);

    /*
     * 第3步: 逐行连线绘制
     * 对每一行, 检查相邻列的连线是否经过该行
     */
    for (row = 16; row <= 63; row++)
    {
        for (col = 0; col < 16; col++) buf[col] = 0;

        for (col = 0; col < 127; col++)
        {
            ya = yDisp[col];
            yb = yDisp[col + 1];
            if (ya > yb) { yt = ya; ya = yb; yb = yt; }

            /* 该行在连线范围内 → 点亮该列 */
            if (row >= ya && row <= yb)
                buf[col / 8] |= (0x80 >> (col % 8));
        }

        LcdShowImage(0, row, 128, 1, buf);    /* 写入GDRAM */
    }

    /* 第4步: 顶部文字覆盖 */
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
