#include <stdint.h>
#include <math.h>

/* =========================================================
   HK32F103 / STM32F103-compatible register definitions
   ========================================================= */
#define REG32(addr) (*(volatile uint32_t *)(addr))

#define RCC_APB1ENR   REG32(0x4002101CUL)
#define RCC_APB1RSTR  REG32(0x40021010UL)
#define RCC_APB2ENR   REG32(0x40021018UL)
#define GPIOA_CRH     REG32(0x40010804UL)
#define GPIOB_CRL     REG32(0x40010C00UL)
#define GPIOB_CRH     REG32(0x40010C04UL)
#define GPIOB_BSRR    REG32(0x40010C10UL)
#define GPIOB_BRR     REG32(0x40010C14UL)

#define USART1_SR     REG32(0x40013800UL)
#define USART1_DR     REG32(0x40013804UL)
#define USART1_BRR    REG32(0x40013808UL)
#define USART1_CR1    REG32(0x4001380CUL)
#define I2C_CR1       REG32(0x40005400UL)
#define I2C_CR2       REG32(0x40005404UL)
#define I2C_OAR1      REG32(0x40005408UL)
#define I2C_DR        REG32(0x40005410UL)
#define I2C_SR1       REG32(0x40005414UL)
#define I2C_SR2       REG32(0x40005418UL)
#define I2C_CCR       REG32(0x4000541CUL)
#define I2C_TRISE     REG32(0x40005420UL)
#define SYSTICK_CTRL  REG32(0xE000E010UL)
#define SYSTICK_LOAD  REG32(0xE000E014UL)
#define SYSTICK_VAL   REG32(0xE000E018UL)

#define USART_TXE     (1U << 7)
#define USART_UE      (1U << 13)
#define USART_TE      (1U << 3)
#define I2C_PE        (1U << 0)
#define I2C_START     (1U << 8)
#define I2C_STOP      (1U << 9)
#define I2C_ACK       (1U << 10)
#define I2C_SB        (1U << 0)
#define I2C_ADDR      (1U << 1)
#define I2C_BTF       (1U << 2)
#define I2C_RXNE      (1U << 6)
#define I2C_TXE       (1U << 7)
#define I2C_AF        (1U << 10)

#define TIMEOUT       100000U
#define MPU_ADDR      0x68U
#define BMP_ADDR_1    0x76U
#define BMP_ADDR_2    0x77U

#define CAL_SAMPLES   501
static int16_t cal_x[CAL_SAMPLES], cal_y[CAL_SAMPLES], cal_z[CAL_SAMPLES];

uint16_t dig_T1, dig_P1;
int16_t  dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
float    t_fine;

/* I2C1 uses PB6/PB7; USART1 transmits on PA9. */
/* UI pins: PB12-PB14 are RGB LEDs and PB15 drives the buzzer. */
#define LED_R_PIN (1U << 12)
#define LED_G_PIN (1U << 13)
#define LED_B_PIN (1U << 14)
#define BUZZ_PIN  (1U << 15)

/* =========================================================
   AXIS REMAPPING HELPER
   ========================================================= */
static void remap_axes(int16_t *x, int16_t *y, int16_t *z) {
    int16_t temp_x = *x;
    int16_t temp_y = *y;
    int16_t temp_z = *z;

    /* Swapped X and Y to fix Roll/Pitch reversal */
    *x = temp_y;  /* New Forward (was Right) */
    *y = temp_z;  /* New Right (was Forward) */
    *z = temp_x;  /* New Up */
}

/* =========================================================
   Helpers & System
   ========================================================= */
static void delay_ms(uint32_t ms) {
    while (ms--) {
        for (volatile uint32_t i = 0; i < 800U; i++) __asm volatile ("nop");
    }
}

/* =========================================================
   UI Engine (RGB & Buzzer)
   ========================================================= */
static void ui_init(void) {
    RCC_APB2ENR |= (1U << 3);
    GPIOB_CRH &= ~(0xFFFF0000UL);
    GPIOB_CRH |=  (0x33330000UL);
    GPIOB_BRR = (LED_R_PIN | LED_G_PIN | LED_B_PIN | BUZZ_PIN);
}

static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    if(r) GPIOB_BSRR = LED_R_PIN; else GPIOB_BRR = LED_R_PIN;
    if(g) GPIOB_BSRR = LED_G_PIN; else GPIOB_BRR = LED_G_PIN;
    if(b) GPIOB_BSRR = LED_B_PIN; else GPIOB_BRR = LED_B_PIN;
}

static void buzz(uint8_t state) {
    if(state) GPIOB_BSRR = BUZZ_PIN; else GPIOB_BRR = BUZZ_PIN;
}

static void beep(uint32_t ms) {
    buzz(1); delay_ms(ms); buzz(0);
}

static void drone_startup_sound(void) {
    led_set(1, 0, 0); beep(150); delay_ms(150);
    led_set(0, 0, 1); beep(150); delay_ms(150);
    led_set(1, 0, 1); beep(150); delay_ms(150);
    led_set(1, 1, 1); beep(600); delay_ms(200);
    led_set(0, 0, 0);
}

static void error_alarm(void) {
    while(1) {
        led_set(1, 0, 0); buzz(1); delay_ms(150);
        led_set(0, 0, 0); buzz(0); delay_ms(150);
    }
}

static void ready_to_fly_sound(void) {
    led_set(0, 1, 0); beep(60); delay_ms(40);
    led_set(0, 0, 0); beep(60); delay_ms(40);
    led_set(0, 1, 0); beep(150); delay_ms(200);
    led_set(0, 0, 0);
}

/* =========================================================
   UART & Strings
   ========================================================= */
static void uart_init(void) {
    RCC_APB2ENR |= (1U << 2) | (1U << 14);
    GPIOA_CRH &= ~(0xFU << 4);
    GPIOA_CRH |=  (0xBU << 4);
    USART1_BRR = 0x0341U;
    USART1_CR1 = USART_UE | USART_TE;
}

static void tx(char c) {
    while (!(USART1_SR & USART_TXE)) {}
    USART1_DR = (uint8_t)c;
}

static void str(const char *s) { while (*s) tx(*s++); }

static void num(int16_t value) {
    char buf[7]; int i = 0; int32_t v = value;
    if (v < 0) { tx('-'); v = -v; }
    do { buf[i++] = (char)('0' + (v % 10)); v /= 10; } while (v && i < 6);
    while (i) tx(buf[--i]);
}

static void uart_float(float v) {
    if (v < 0) { tx('-'); v = -v; }
    int int_part = (int)v;
    float frac = v - (float)int_part;
    int frac_part = (int)(frac * 1000.0f);
    num(int_part); tx('.');
    if (frac_part < 10) tx('0');
    if (frac_part < 100) tx('0');
    num(frac_part);
}

/* =========================================================
   I2C Drivers
   ========================================================= */
static void i2c_init(void) {
    RCC_APB2ENR |= (1U << 3);
    RCC_APB1ENR |= (1U << 21);
    GPIOB_CRL &= ~((0xFU << 24) | (0xFU << 28));
    GPIOB_CRL |=  ((0xFU << 24) | (0xFU << 28));
    RCC_APB1RSTR |=  (1U << 21);
    RCC_APB1RSTR &= ~(1U << 21);
    I2C_CR1 = 0; I2C_CR2 = 8U; I2C_CCR = 40U; I2C_TRISE = 9U;
    I2C_OAR1 = (1U << 14);
    I2C_CR1 = I2C_PE | I2C_ACK;
}

static int waitflag(uint32_t flag) {
    uint32_t t = TIMEOUT;
    while (t--) {
        if (I2C_SR1 & I2C_AF) { I2C_SR1 &= ~I2C_AF; return 0; }
        if (I2C_SR1 & flag) return 1;
    }
    return 0;
}

static int wr(uint8_t dev, uint8_t reg, uint8_t val) {
    I2C_CR1 |= I2C_START;
    if (!waitflag(I2C_SB)) goto fail;
    I2C_DR = (dev << 1);
    if (!waitflag(I2C_ADDR)) goto fail;
    (void)I2C_SR1; (void)I2C_SR2;
    I2C_DR = reg;
    if (!waitflag(I2C_TXE)) goto fail;
    I2C_DR = val;
    if (!waitflag(I2C_BTF)) goto fail;
    I2C_CR1 |= I2C_STOP; return 1;
fail:
    I2C_CR1 |= I2C_STOP; return 0;
}

static int rd(uint8_t dev, uint8_t reg, uint8_t *val) {
    I2C_CR1 |= I2C_ACK | I2C_START;
    if (!waitflag(I2C_SB)) goto fail;
    I2C_DR = (dev << 1);
    if (!waitflag(I2C_ADDR)) goto fail;
    (void)I2C_SR1; (void)I2C_SR2;
    I2C_DR = reg;
    if (!waitflag(I2C_TXE) || !waitflag(I2C_BTF)) goto fail;
    I2C_CR1 |= I2C_START;
    if (!waitflag(I2C_SB)) goto fail;
    I2C_DR = (dev << 1) | 1U;
    if (!waitflag(I2C_ADDR)) goto fail;
    I2C_CR1 &= ~I2C_ACK;
    (void)I2C_SR1; (void)I2C_SR2;
    I2C_CR1 |= I2C_STOP;
    if (!waitflag(I2C_RXNE)) goto fail;
    *val = (uint8_t)I2C_DR;
    I2C_CR1 |= I2C_ACK; return 1;
fail:
    I2C_CR1 |= I2C_STOP; I2C_CR1 &= ~I2C_PE; I2C_CR1 = I2C_PE | I2C_ACK;
    return 0;
}

static int rd16_be(uint8_t dev, uint8_t reg, int16_t *val) {
    uint8_t h, l;
    if (!rd(dev, reg, &h) || !rd(dev, reg + 1, &l)) return 0;
    *val = (int16_t)(((uint16_t)h << 8) | l);
    return 1;
}

static int rd16_le(uint8_t dev, uint8_t reg, uint16_t *val) {
    uint8_t l, h;
    if (!rd(dev, reg, &l) || !rd(dev, reg + 1, &h)) return 0;
    *val = (uint16_t)(((uint16_t)h << 8) | l);
    return 1;
}

static int rd24_adc(uint8_t dev, uint8_t reg, int32_t *val) {
    uint8_t msb, lsb, xlsb;
    if (!rd(dev, reg, &msb) || !rd(dev, reg+1, &lsb) || !rd(dev, reg+2, &xlsb)) return 0;
    *val = (int32_t)((msb << 12) | (lsb << 4) | (xlsb >> 4));
    return 1;
}

/* =========================================================
   Math & BMP280 Compensation
   ========================================================= */
static void sort(int16_t *arr, int n) {
    for (int i = 1; i < n; i++) {
        int16_t key = arr[i]; int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; j--; }
        arr[j + 1] = key;
    }
}
static int16_t median(int16_t *arr, int n) { sort(arr, n); return arr[n / 2]; }

static float compensate_T(int32_t adc_T) {
    float var1 = (((float)adc_T)/16384.0f - ((float)dig_T1)/1024.0f) * ((float)dig_T2);
    float var2 = ((((float)adc_T)/131072.0f - ((float)dig_T1)/8192.0f) *
                  (((float)adc_T)/131072.0f - ((float)dig_T1)/8192.0f)) * ((float)dig_T3);
    t_fine = var1 + var2;
    return (t_fine * 5120.0f) / 51200.0f;
}

static float compensate_P(int32_t adc_P) {
    float var1, var2, p;
    var1 = (t_fine / 2.0f) - 64000.0f;
    var2 = var1 * var1 * ((float)dig_P6) / 32768.0f;
    var2 = var2 + var1 * ((float)dig_P5) * 2.0f;
    var2 = (var2 / 4.0f) + (((float)dig_P4) * 65536.0f);
    var1 = (((float)dig_P3) * var1 * var1 / 524288.0f + ((float)dig_P2) * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * ((float)dig_P1);
    if (var1 == 0.0f) return 0;
    p = 1048576.0f - (float)adc_P;
    p = (p - (var2 / 4096.0f)) * 6250.0f / var1;
    var1 = ((float)dig_P9) * p * p / 2147483648.0f;
    var2 = p * ((float)dig_P8) / 32768.0f;
    p = p + (var1 + var2 + ((float)dig_P7)) / 16.0f;
    return p;
}

/* =========================================================
   Main Application
   ========================================================= */
int main(void) {
    ui_init();
    uart_init();
    i2c_init();
    delay_ms(500);

    drone_startup_sound();

    uint8_t mpu_id = 0, bmp_id = 0, bmp_addr = 0;
    rd(MPU_ADDR, 0x75, &mpu_id);
    if (rd(BMP_ADDR_1, 0xD0, &bmp_id)) bmp_addr = BMP_ADDR_1;
    else if (rd(BMP_ADDR_2, 0xD0, &bmp_id)) bmp_addr = BMP_ADDR_2;

    if (!mpu_id || !bmp_addr) {
        str("INIT ERROR: Missing Sensor!\r\n");
        error_alarm();
    }

    wr(MPU_ADDR, 0x6B, 0x01);
    wr(MPU_ADDR, 0x1A, 0x03);
    wr(MPU_ADDR, 0x19, 0x09);
    wr(MPU_ADDR, 0x1B, 0x00);
    wr(MPU_ADDR, 0x1C, 0x00);

    wr(bmp_addr, 0xF5, 0x10);
    wr(bmp_addr, 0xF4, 0x57);

    rd16_le(bmp_addr, 0x88, &dig_T1); rd16_le(bmp_addr, 0x8A, (uint16_t*)&dig_T2); rd16_le(bmp_addr, 0x8C, (uint16_t*)&dig_T3);
    rd16_le(bmp_addr, 0x8E, &dig_P1); rd16_le(bmp_addr, 0x90, (uint16_t*)&dig_P2); rd16_le(bmp_addr, 0x92, (uint16_t*)&dig_P3);
    rd16_le(bmp_addr, 0x94, (uint16_t*)&dig_P4); rd16_le(bmp_addr, 0x96, (uint16_t*)&dig_P5); rd16_le(bmp_addr, 0x98, (uint16_t*)&dig_P6);
    rd16_le(bmp_addr, 0x9A, (uint16_t*)&dig_P7); rd16_le(bmp_addr, 0x9C, (uint16_t*)&dig_P8); rd16_le(bmp_addr, 0x9E, (uint16_t*)&dig_P9);

    /* --- GYRO CALIBRATION --- */
    str("Calibrating Gyro (Do not move)...\r\n");
    for (int i = 0; i < CAL_SAMPLES; i++) {
        int16_t raw_gx, raw_gy, raw_gz;
        rd16_be(MPU_ADDR, 0x43, &raw_gx);
        rd16_be(MPU_ADDR, 0x45, &raw_gy);
        rd16_be(MPU_ADDR, 0x47, &raw_gz);

        remap_axes(&raw_gx, &raw_gy, &raw_gz);

        cal_x[i] = raw_gx;
        cal_y[i] = raw_gy;
        cal_z[i] = raw_gz;

        if (i % 50 == 0) {
            led_set(1, 1, 0); buzz(1); delay_ms(2); buzz(0); delay_ms(3);
        } else if (i % 50 == 25) {
            led_set(0, 0, 0); delay_ms(5);
        } else {
            delay_ms(5);
        }
    }
    int16_t bx = median(cal_x, CAL_SAMPLES);
    int16_t by = median(cal_y, CAL_SAMPLES);
    int16_t bz = median(cal_z, CAL_SAMPLES);

    str("Establishing Baseline Altitude...\r\n");
    float baseline_pressure = 0.0f;
    for (int i = 0; i < 200; i++) {
        int32_t adc_T, adc_P;
        rd24_adc(bmp_addr, 0xFA, &adc_T);
        rd24_adc(bmp_addr, 0xF7, &adc_P);
        compensate_T(adc_T);
        baseline_pressure += compensate_P(adc_P);

        if (i % 20 == 0) led_set(0, 1, 1);
        if (i % 20 == 10) led_set(0, 0, 0);
        delay_ms(15);
    }
    baseline_pressure /= 200.0f;

    ready_to_fly_sound();

    float current_roll = 0.0f, current_pitch = 0.0f;
    float filtered_alt = 0.0f;
    float ax_f = 0.0f, ay_f = 0.0f, az_f = 0.0f;

    uint32_t blink_timer = 0;

    /* Initialize Initial Angles */
    int16_t ax, ay, az;
    if (rd16_be(MPU_ADDR, 0x3B, &ax) && rd16_be(MPU_ADDR, 0x3D, &ay) && rd16_be(MPU_ADDR, 0x3F, &az)) {
        remap_axes(&ax, &ay, &az);

        ax_f = (float)ax; ay_f = (float)ay; az_f = (float)az;
        current_roll = atan2f(ay_f, az_f) * 57.29578f;
        current_pitch = atan2f(-ax_f, sqrtf(ax_f*ax_f + az_f*az_f)) * 57.29578f;
    }

    SYSTICK_LOAD = 0xFFFFFF; SYSTICK_VAL = 0; SYSTICK_CTRL = 5;
    uint32_t last_tick = SYSTICK_VAL;

    /* MAIN LOOP */
    while (1) {
        int16_t gx, gy, gz;
        int32_t adc_T, adc_P;

        if (rd16_be(MPU_ADDR, 0x3B, &ax) && rd16_be(MPU_ADDR, 0x3D, &ay) && rd16_be(MPU_ADDR, 0x3F, &az) &&
            rd16_be(MPU_ADDR, 0x43, &gx) && rd16_be(MPU_ADDR, 0x45, &gy) && rd16_be(MPU_ADDR, 0x47, &gz) &&
            rd24_adc(bmp_addr, 0xFA, &adc_T) && rd24_adc(bmp_addr, 0xF7, &adc_P))
        {
            remap_axes(&ax, &ay, &az);
            remap_axes(&gx, &gy, &gz);

            buzz(0);
            blink_timer++;

            /* --- UPDATED: 1.5 Second Blink Interval (100 loops * 15ms) --- */
            if (blink_timer == 100) {
                led_set(0, 1, 0);
            } else if (blink_timer >= 105) {
                led_set(0, 0, 0);
                blink_timer = 0;
            } else if (blink_timer < 100) {
                led_set(0, 0, 0);
            }

            uint32_t now = SYSTICK_VAL;
            float dt = (float)((last_tick - now) & 0xFFFFFF) / 8000000.0f;
            last_tick = now;
            if (dt > 0.1f) dt = 0.01f;

            ax_f = (0.8f * ax_f) + (0.2f * (float)ax);
            ay_f = (0.8f * ay_f) + (0.2f * (float)ay);
            az_f = (0.8f * az_f) + (0.2f * (float)az);

            float gx_dps = (float)(gx - bx) / 131.0f;
            float gy_dps = (float)(gy - by) / 131.0f;
            float yaw_rate = (float)(gz - bz) / 131.0f;

            if (fabs(gx_dps) < 1.5f) gx_dps = 0.0f;
            if (fabs(gy_dps) < 1.5f) gy_dps = 0.0f;
            if (fabs(yaw_rate) < 1.5f) yaw_rate = 0.0f;

            float acc_roll = atan2f(ay_f, az_f) * 57.29578f;
            float acc_pitch = atan2f(-ax_f, sqrtf(ax_f*ax_f + az_f*az_f)) * 57.29578f;

            current_roll = 0.98f * (current_roll + gx_dps * dt) + 0.02f * acc_roll;
            current_pitch = 0.98f * (current_pitch + gy_dps * dt) + 0.02f * acc_pitch;

            if (adc_P != 0 && adc_P != 0xFFFFFF) {
                compensate_T(adc_T);
                float pressure = compensate_P(adc_P);

                if (pressure > 50000.0f && pressure < 120000.0f) {
                    float raw_alt = 44330.0f * (1.0f - powf(pressure / baseline_pressure, 0.1903f));
                    float diff = fabs(raw_alt - filtered_alt);

                    if (diff > 4.0f) {
                        raw_alt = filtered_alt;
                    }

                    if (diff > 0.2f) {
                        filtered_alt = (0.60f * filtered_alt) + (0.40f * raw_alt);
                    } else if (diff > 0.05f) {
                        filtered_alt = (0.85f * filtered_alt) + (0.15f * raw_alt);
                    } else {
                        filtered_alt = (0.95f * filtered_alt) + (0.05f * raw_alt);
                    }
                }
            }

            /* R/P are degrees, Y is degrees/second, and Alt is relative metres. */
            str("R:"); uart_float(current_roll);
            str(",P:"); uart_float(current_pitch);
            str(",Y:"); uart_float(yaw_rate);
            str(",Alt:"); uart_float(filtered_alt);
            str("\r\n");

        } else {
            led_set(1, 0, 0);
            buzz(1);
            blink_timer = 0;
            delay_ms(10);
        }

        delay_ms(15);
    }
    return 0;
}
