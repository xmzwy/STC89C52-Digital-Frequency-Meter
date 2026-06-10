/*
*******************************************************************************
*  Digital Frequency Meter + Oscilloscope
*  MCU: STC89C52RC  XTAL: 11.0592MHz  LCD: 12864 (ST7920)
*  ADC: PCF8591 AIN1 for waveform capture + zero-crossing freq detection
*
*  Wiring:
*    Signal Generator RED  ---> PCF8591 AIN1
*    Signal Generator BLACK ---> Circuit GND  ← MUST connect!
*    PCF8591 SCL ---> P3^7,  SDA ---> P3^6
*    AOUT→AIN1 jumper only for internal square-wave test
*
*  Keys:
*    [0]     Toggle text / waveform display
*    [Enter] Switch internal square-wave / external measurement
*    [Up]    Freq+10Hz  [Down]  Freq-10Hz  (internal mode)
*    [Left]  Freq+1Hz   [Right] Freq-1Hz   (internal mode)
*    [ESC]   Start/stop internal DAC output
*
*  Timers:
*    T0 - 16-bit counter on P3^4  (optional digital input)
*    T1 - 10ms system tick + key scan
*    T2 - ADC sample timer, FIXED 1.5ms (667Hz), 64-sample buffer = 96ms
*
*  v6.4 - Auto-scale, adaptive threshold, per-buffer EMA freq, diagnostics
*******************************************************************************
*/

#include "config.h"
#include "LCD12864.h"

/* ==================== DAC Wave Table (internal square wave) =============== */
unsigned char code SquareWave[] = {
    255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
};

/* ==================== ADC Sample Buffer (XRAM) ==================== */
unsigned char xdata adcBuf[64];    /* 64 samples * 1.5ms = 96ms window */
unsigned char adcIdx;
bit adcReady;
unsigned char adcLive;
unsigned int  xdata adcBufCount;

/* ==================== Global Variables ==================== */

bit flag200ms   = 0;
bit flagMeasure = 0;
bit waveOn      = 1;
bit showWave    = 0;

unsigned char idata mode;           /* 0=internal  1=external */
unsigned char idata waveFreq = 10;  /* default 10Hz (I2C safe: T2>3ms) */
unsigned long xdata measuredFreq;
unsigned long xdata measuredPeriod;
unsigned int  xdata T0overflow;
unsigned int  xdata zcCount;
unsigned char idata zcBufCnt;
unsigned char idata trigOff;

/* Signal statistics for auto-scaling & adaptive threshold */
unsigned char idata sigMin;         /* buffer minimum */
unsigned char idata sigMax;         /* buffer maximum */
unsigned char idata sigAvg;         /* buffer average (DC offset) */
unsigned char idata sigThr;         /* zero-crossing threshold */

/* No separate display min/max needed — use per-buffer sigMin/sigMax directly */

unsigned char idata T1RH, T1RL;
unsigned char idata strBuf[17];

/* Display Y-coordinate buffer (128 pixels, in XRAM) */
unsigned char xdata yDisp[128];

/* ==================== External Functions ==================== */

extern void KeyScan();
extern void KeyDriver();
extern void I2CStart();
extern void I2CStop();
extern bit  I2CWrite(unsigned char dat);
extern unsigned char I2CReadNAK();
extern unsigned char I2CReadACK();

/* ==================== Prototypes ==================== */

void ConfigTimer0();
void ConfigTimer1(uint8 ms);
void ConfigTimer2();
void SetWaveFreq(uint8 freq);
void SetDACOut(uint8 val);
void DisableDAC();
void UpdateDisplay();
void DrawWaveform();
uint8 NumberToString(uint8 *str, uint32 num);
uint8 ReadADC(uint8 chn);
void DumpADC(void);

/* ==================== String Helpers ==================== */

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

/* ==================== PCF8591 ADC ==================== */

uint8 ReadADC(uint8 chn)
{
    uint8 val;

    /* Try bit6=0 (DAC off) to see if internal DAC crosstalk shifts readings */
    I2CStart();
    I2CWrite(0x48 << 1);
    I2CWrite(0x00 | (chn & 0x03));   /* DAC off, single-ended, channel N */
    I2CStop();

    /* PCF8591 conversion time ~90us at 100kHz I2C */
    { uint8 t; for (t = 0; t < 20; t++) _nop_(); }

    /* Read conversion result */
    I2CStart();
    I2CWrite((0x48 << 1) | 0x01);    /* address + read bit */
    val = I2CReadNAK();
    I2CStop();

    return val;
}

/* ==================== Main ==================== */

void main()
{
    uint32 lastFreq = 0;

    EA = 1;

    ConfigTimer0();
    ConfigTimer1(10);
    ConfigTimer2();
    InitLcd12864();
    SetWaveFreq(waveFreq);

    measuredFreq   = waveFreq;
    measuredPeriod = 1000000UL / waveFreq;

    /* Init signal stats to mid-scale */
    sigMin = 127; sigMax = 127; sigAvg = 127; sigThr = 127;

    LcdShowString(0, 0, "  Digital Freq  ");
    UpdateDisplay();

    while (1)
    {
        KeyDriver();

        /* ---- ADC buffer filled ---- */
        if (adcReady)
        {
            uint8 i;
            uint8 prev, curr;
            uint16 sum;

            adcReady = 0;
            adcBufCount++;

            /*
             * Compute signal statistics from ALL 64 samples.
             * (PCF8591 pipeline was primed by dummy read at mode switch,
             * so adcBuf[0] is valid for all buffers after the first.)
             */
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
            sigThr = sigAvg;

            /* Avoid degenerate range */
            if (sigMax <= sigMin) sigMax = sigMin + 1;

            /*
             * Find trigger point: first rising edge through sigThr.
             * This aligns the waveform display to a consistent phase.
             */
            trigOff = 0;
            for (i = 1; i < 64; i++)
            {
                if (adcBuf[i-1] < sigThr && adcBuf[i] >= sigThr)
                {
                    trigOff = i;
                    break;
                }
            }

            /*
             * Count rising edges in this buffer.
             * Accumulate over 1 second for precise frequency measurement.
             */
            if (mode == 1)
            {
                uint8 edges;

                edges = 0;
                prev = adcBuf[0];
                for (i = 1; i < 64; i++)
                {
                    curr = adcBuf[i];
                    if (prev < sigThr && curr >= sigThr)
                        edges++;
                    prev = curr;
                }

                zcCount  += edges;   /* total edges in this second */
                zcBufCnt++;           /* number of buffers this second */
            }

            /* Redraw waveform if showing */
            if (showWave) DrawWaveform();

            /* Restart ADC capture (fixed period, NO adaptive sampling) */
            adcIdx = 0;
            TR2 = 1;
        }

        /* ---- 1-second measurement update ---- */
        if (flagMeasure)
        {
            flagMeasure = 0;

            if (mode == 0)
            {
                measuredFreq = waveFreq;
            }
            else if (zcBufCnt > 0 && zcCount > 0)
            {
                /*
                 * 1-second accumulation: sums edges from ~10 buffers.
                 * Each buffer = 64 × 1.5ms = 96ms exactly.
                 * freq = zcCount / (zcBufCnt × 0.096)
                 *      = zcCount × 1000 / (zcBufCnt × 96)
                 *      = zcCount × 125   / (zcBufCnt × 12)
                 */
                measuredFreq = (uint32)zcCount * 125 / ((uint16)zcBufCnt * 12);
            }
            else
            {
                measuredFreq = 0;
            }

            if (measuredFreq > 0)
                measuredPeriod = 1000000UL / measuredFreq;
            else
                measuredPeriod = 0;

            /* Reset for next second */
            zcCount  = 0;
            zcBufCnt = 0;

            if (measuredFreq != lastFreq)
            {
                lastFreq = measuredFreq;
                if (!showWave) UpdateDisplay();
                else if (mode == 1) DrawWaveform();
            }
        }

        /* ---- 200ms text refresh ---- */
        if (flag200ms)
        {
            flag200ms = 0;
            if (!showWave) UpdateDisplay();
        }
    }
}

/* ==================== Key Action ==================== */

void KeyAction(unsigned char keycode)
{
    if (keycode == '0')
    {
        if (showWave)
        {
            showWave = 0;
            LcdWriteCmd(0x30);  LcdWriteCmd(0x01);
            LcdClearArea(0, 0, 128, 64);
            LcdShowString(0, 0, "  Digital Freq  ");
            UpdateDisplay();
        }
        else
        {
            showWave = 1;
            LcdWriteCmd(0x30);  LcdWriteCmd(0x01);
            LcdClearArea(0, 0, 128, 64);
            DrawWaveform();
        }
        return;
    }

    if (keycode == 0x0D)         /* Enter: switch mode */
    {
        if (mode == 0)
        {
            mode = 1;
            waveOn = 0;
            TR2 = 0;
            DisableDAC();
            ConfigTimer2();

            /*
             * Prime the PCF8591 pipeline: the first read after a
             * mode/channel change returns stale data.  Do one dummy
             * read so the first real sample (adcBuf[0]) is valid.
             */
            ReadADC(1);   /* dummy — discard result, primes pipeline */

            adcIdx = 0;
            adcReady = 0;
            measuredFreq = 0;
            zcCount = 0;
            zcBufCnt = 0;
            TR2 = 1;             /* start ADC capture */
        }
        else
        {
            mode = 0;
            waveOn = 1;
            TR2 = 0;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x1B)    /* ESC: start/stop wave */
    {
        if (mode == 0)
        {
            waveOn = !waveOn;
            if (waveOn) SetWaveFreq(waveFreq);
            else        TR2 = 0;
        }
    }
    else if (keycode == 0x26)    /* Up: +10Hz */
    {
        if (mode == 0 && waveFreq <= 40)
        {
            waveFreq += 10;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x28)    /* Down: -10Hz */
    {
        if (mode == 0 && waveFreq > 10)
        {
            waveFreq -= 10;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x25)    /* Left: +1Hz */
    {
        if (mode == 0 && waveFreq < 50)
        {
            waveFreq += 1;
            SetWaveFreq(waveFreq);
        }
    }
    else if (keycode == 0x27)    /* Right: -1Hz */
    {
        if (mode == 0 && waveFreq > 1)
        {
            waveFreq -= 1;
            SetWaveFreq(waveFreq);
        }
    }

    if (keycode == '9')
    {
        LcdShowString(0, 0, "   DUMPING...   ");
        DumpADC();
        LcdShowString(0, 0, "    DUMP DONE   ");
        return;
    }

    if (showWave) DrawWaveform();
    else          UpdateDisplay();
}

/* ==================== PCF8591 DAC Output ==================== */

void SetDACOut(uint8 val)
{
    I2CStart();
    if (!I2CWrite(0x48 << 1)) { I2CStop(); return; }
    I2CWrite(0x40);             /* DAC enabled (bit6=1) */
    I2CWrite(val);
    I2CStop();
}

/*
 * DisableDAC: Called when switching to external mode.
 * Just writes a neutral control byte — does NOT turn off DAC
 * because PCF8591 ADC needs internal DAC enabled for SAR operation.
 * Since AOUT is not jumpered to AIN1 in external mode, the DAC
 * output on AOUT does not interfere with measurements.
 */
void DisableDAC()
{
    I2CStart();
    I2CWrite(0x48 << 1);
    I2CWrite(0x00);             /* DAC OFF, ch0 */
    I2CStop();
}

/* ==================== Internal Wave Frequency ==================== */

void SetWaveFreq(uint8 freq)
{
    unsigned long tmp;

    if (freq < 1)  freq = 1;
    /*
     * MAX 50Hz internal: I2C DAC write takes ~400us in ISR.
     * At 50Hz with 32-step table, T2 period = 2.7ms, safe margin.
     * Above 60Hz the ISR can't finish before the next T2 fire.
     */
    if (freq > 50) freq = 50;
    waveFreq = freq;

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

/* ==================== Serial Debug Dump ==================== */
/*
 * Hardware UART on P3.1 (TXD) at 9600 baud.
 * Temporarily reconfigures Timer1 for baud rate, sends data,
 * then restores Timer1 for system tick.
 *
 * KST-51 board: P3.1 is already connected to CH340 USB-serial.
 * Just open the same COM port used for STC-ISP programming.
 * Press '9' to dump latest ADC buffer.
 */

static void uart_init(void)
{
    SCON = 0x50;   /* mode 1, 8-bit UART, REN=0 */
    TMOD &= 0x0F;  /* clear Timer1 bits */
    TMOD |= 0x20;  /* Timer1 mode 2 (8-bit auto-reload) */
    TH1 = 0xFD;    /* 9600 baud @ 11.0592MHz */
    TL1 = 0xFD;
    TR1 = 1;       /* start Timer1 */
}

static void uart_putc(uint8 c)
{
    SBUF = c;
    while (!TI);   /* wait for transmit complete */
    TI = 0;
}

static void uart_hex(uint8 v)
{
    uint8 n;
    n = v >> 4;  uart_putc((n < 10) ? ('0'+n) : ('A'+n-10));
    n = v & 15;  uart_putc((n < 10) ? ('0'+n) : ('A'+n-10));
}

static void uart_puts(const uint8 *s)
{
    while (*s) uart_putc(*s++);
}

void DumpADC(void)
{
    uint8 i;
    uint8 save_TH1, save_TL1, save_TMOD_hi;

    /* Save Timer1 state */
    save_TH1 = TH1;
    save_TL1 = TL1;
    save_TMOD_hi = TMOD & 0xF0;
    TR1 = 0;

    EA = 0;   /* disable interrupts during dump */
    uart_init();

    uart_puts("--- ADC Buffer Dump ---\r\n");
    uart_puts("Min=");  uart_hex(sigMin);
    uart_puts(" Max="); uart_hex(sigMax);
    uart_puts(" Avg="); uart_hex(sigAvg);
    uart_puts("\r\nIdx: ");
    for (i = 0; i < 64; i++)
    {
        uart_hex(adcBuf[i]);
        uart_putc(' ');
        if ((i & 15) == 15) uart_puts("\r\n");
    }
    uart_puts("--- End ---\r\n");

    /* Wait for last byte to finish */
    while (!TI);
    TI = 0;

    /* Restore Timer1 for system tick */
    TR1 = 0;
    TH1 = save_TH1;
    TL1 = save_TL1;
    TMOD = (TMOD & 0x0F) | save_TMOD_hi;
    TR1 = 1;
    EA = 1;   /* re-enable interrupts */
}

/* ==================== Timer Config ==================== */

void ConfigTimer0()
{
    TMOD &= 0xF0;
    TMOD |= 0x05;
    TH0 = 0; TL0 = 0;
    ET0 = 1; TR0 = 1;
}

void ConfigTimer1(uint8 ms)
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
* T2: ~1.5ms auto-reload for ADC sampling (~667Hz)
* 921600 * 0.0015 = 1382 -> reload = 65536-1382 = 64154
* PCF8591 over I2C: ~1.4ms per read. T2 must be > I2C time.
* Nyquist limit: max displayable signal ~300Hz.
* Recommended test: 50-200Hz sine/square/triangle wave.
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

/* ==================== ISR ==================== */

void ISR_Timer0() interrupt 1
{
    T0overflow++;
}

void ISR_Timer1() interrupt 3
{
    static uint8 cnt200ms = 0;
    static uint8 cnt1s    = 0;

    TH1 = T1RH; TL1 = T1RL;
    KeyScan();

    cnt200ms++;  cnt1s++;
    if (cnt200ms >= 20) { cnt200ms = 0; flag200ms  = 1; }
    if (cnt1s    >= 100) { cnt1s    = 0; flagMeasure = 1; }
}

/*
* T2 ISR:
*   Internal mode: DAC square wave output
*   External mode: ADC sample from AIN1 -> 64-point buffer
*/
void ISR_Timer2() interrupt 5
{
    static unsigned char dacIdx = 0;

    TF2 = 0;

    if (mode == 0)
    {
        /* Internal: DAC square wave */
        SetDACOut(SquareWave[dacIdx]);
        dacIdx++;
        if (dacIdx >= 32) dacIdx = 0;
    }
    else
    {
        /* External: ADC sampling on AIN1 */
        if (adcIdx < 64)
        {
            adcLive = ReadADC(1);          /* read AIN1 */
            adcBuf[adcIdx] = adcLive;
            adcIdx++;
            if (adcIdx >= 64)
            {
                adcReady = 1;
                TR2 = 0;   /* pause until buffer is processed */
            }
        }
    }
}

/* ==================== Display ==================== */

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

void UpdateDisplay()
{
    uint8 numStr[12];

    /* Line 2: Freq */
    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Freq: ");
    NumberToString(numStr, measuredFreq);
    StrCopy(strBuf, 6, numStr, 6);
    StrCopyLit(strBuf, 13, " Hz");
    LcdShowString(0, 16, strBuf);

    /* Line 3: Period */
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

    /* Line 4: Mode */
    StrFill(strBuf, 16);
    StrCopyLit(strBuf, 0, "Mode: ");
    if (mode == 0)
    {
        StrCopyLit(strBuf, 6, "Square F=");
        NumberToString(numStr, waveFreq);
        StrCopy(strBuf, 14, numStr, 2);
    }
    else
    {
        /* Signal diagnostic: per-buffer min/max + avg */
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

/*
* DrawWaveform: Interpolated oscilloscope display.
*   64 ADC samples → linear interpolation → 128 screen columns.
*   Auto-scales Y: sigMin→bottom(63), sigMax→top(16).
*   Area: y=16..63 (48px), text overlay at y=0.
*/
void DrawWaveform()
{
    uint8 col, row, i;
    uint8 buf[16];
    uint8 numStr[12];
    uint8 ya, yb, yt;
    uint16 adcRange;

    /* Auto-scaling with small 6% padding so waveform doesn't hug edges */
    {
        uint8 dMin, dMax;
        uint16 pad = ((uint16)sigMax - (uint16)sigMin) / 16;
        dMin = (sigMin >= pad) ? (sigMin - pad) : 0;
        dMax = sigMax + pad; if (dMax < sigMax || dMax > 253) dMax = 255;
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

    /* Linear interpolation for odd columns.
     * Skip if Y jump >20px (square edge) → keep vertical transition. */
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

    /* Step 3: Draw */
    for (row = 16; row <= 63; row++)
    {
        for (col = 0; col < 16; col++) buf[col] = 0;

        /* Connected line segments */
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

    /* Text */
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
