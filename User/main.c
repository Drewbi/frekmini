#include "debug.h"

#define NPIXELS 16

volatile uint8_t framebuffer[NPIXELS] = {0};

volatile uint8_t pwm_step = 0;

volatile uint16_t TxWord = 0;

void shift_spi_init(void);
void pwm_timer_init(void);

void shift_spi_init() {
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
        TxWord = mask;
    }
}

void SPI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SPI1_IRQHandler(void) {
    if (SPI_I2S_GetITStatus(SPI1, SPI_I2S_IT_TXE) != RESET) {
        SPI_I2S_SendData(SPI1, TxWord);

        GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_SET);
        GPIO_WriteBit(GPIOC, GPIO_Pin_4, Bit_RESET);
    }
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_1);
    SystemCoreClockUpdate();
    Delay_Init();

    shift_spi_init();
    pwm_timer_init();

    int dir = 1;
    int val = 0;

    while (1) {
        for (int i = 0; i < NPIXELS; i++) {
            framebuffer[i] = val;
        }

        val += dir;
        if (val >= 255) { dir = -1; }
        if (val <= 0)   { dir =  1; }

        Delay_Ms(10);
    }
}

