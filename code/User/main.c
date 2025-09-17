#include "debug.h"

#define NPIXELS 16

volatile u8 framebuffer[NPIXELS] = {0};

volatile u8 pwm_step = 0;

volatile u16 tx_word = 0;

static u16 rng_state = 24635U;

void spi_init();
void pwm_timer_init();
void adc_init();
void choose_pixels();
void update_framebuffer();

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    spi_init();
    pwm_timer_init();
    adc_init();

    choose_pixels();

    while (1) {
        update_framebuffer();
        Delay_Ms(5);
    }
}

void adc_init() {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    ADC_InitTypeDef  ADC_InitStructure  = {0};

    // Enable GPIOC and ADC1 clocks
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div8); // ADC clock = PCLK2/8

    // Configure PC4 (ADC channel 2) as analog input
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // ADC configuration
    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    // Enable ADC
    ADC_Cmd(ADC1, ENABLE);

    // Calibration
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
}

uint16_t adc_read(uint8_t channel)
{
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_30Cycles);

    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));

    return ADC_GetConversionValue(ADC1);
}

void spi_init() {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_SPI1, ENABLE);

    // PC4 = LATCH
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PC5 = CLK
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PC6 = MOSI
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PD0 = EN
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // SPI setup: 16-bit, 1-line Tx
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_16b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_64;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);

    // Enable SPI TXE interrupt
    NVIC_InitStructure.NVIC_IRQChannel = SPI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    SPI_Cmd(SPI1, ENABLE);
    SPI_I2S_ITConfig(SPI1, SPI_I2S_IT_TXE, ENABLE);

    // EN low, LATCH reset
    GPIO_WriteBit(GPIOD, GPIO_Pin_0, Bit_RESET);
    GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_RESET);
}

void timer2_init(void)
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseInitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);

    // 48 MHz / 48 = 1 MHz -> 1 ?s per tick
    TIM_TimeBaseInitStructure.TIM_Period        = 0xFFFF;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = 48 - 1;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseInitStructure);

    TIM_Cmd(TIM1, ENABLE);
}

uint32_t timer_now(void)
{
    // return current 16-bit counter as 32-bit
    return (uint32_t)TIM_GetCounter(TIM1);
}

void pwm_timer_init() {
    TIM_TimeBaseInitTypeDef TIM_InitStructure = {0};
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    // 48 MHz clock: 48e6 / 51200 ¡Ö 937
    TIM_InitStructure.TIM_Period = 936;
    TIM_InitStructure.TIM_Prescaler = 0;
    TIM_InitStructure.TIM_ClockDivision = 0;
    TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) {
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        pwm_step++;

        uint16_t mask = 0;
        for (int i = 0; i < NPIXELS; i++) {
            if (framebuffer[i] > pwm_step) {
                mask |= (1 << i);
            }
        }
        tx_word = mask;
    }
}

void SPI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SPI1_IRQHandler(void) {
    if (SPI_I2S_GetITStatus(SPI1, SPI_I2S_IT_TXE) != RESET) {
        SPI_I2S_SendData(SPI1, tx_word);

        GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_SET);
        GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_RESET);
    }
}

void rng_seed_from_hardware(void){
    uint32_t s = 0;
    for(int i = 0; i < 16; ++i){
        // give ADC a little time between reads if needed
        uint16_t a = adc_read(ADC_Channel_2);
        uint32_t t = timer_now();
        // mix ADC bits and timer LSBs
        s ^= ((uint32_t)a << (i & 3)) ^ (t & 0xFFFF);
    }
    // avoid zero state
    if(s == 0) s = 0xA5A5A5A5U;
    rng_state = s;
}

// xorshift32 PRNG (fast, non-crypto)
uint32_t xorshift32(void){
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x ? x : 0x1234U;
    return rng_state;
}

// helper to get uniform 0..n-1
uint32_t rand_range(uint32_t n){
    return xorshift32() % n;
}

volatile u8 fade_dir = 1;
volatile u8 fade_val = 0;
volatile u8 set_pixels[NPIXELS] = {0};

void choose_pixels() {
    for (int i = 0; i < NPIXELS; i++) {
        set_pixels[i] = rand_range(rand_range(10)) == 0;
    }
}

void update_framebuffer() {
    for (int i = 0; i < NPIXELS; i++) {
        if (set_pixels[i] == 1) {
            framebuffer[i] = fade_val;
        } else {
            framebuffer[i] = 0;
        }
    }

    fade_val += fade_dir;
    if (fade_val >= 255) { fade_dir = -1; }
    if (fade_val <= 0)   { 
        fade_dir =  1;
        choose_pixels();
    }
}

