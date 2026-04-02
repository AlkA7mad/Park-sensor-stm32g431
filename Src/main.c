

#include <stdint.h>
#include "stm32g431xx.h"


/**
 * HC-SR04 ultrasonic distance sensor on STM32G431RB (Nucleo)
 * - TIM3 input capture on PA6 (echo)
 * - Trigger pulse on PA7
 * - Buzzer on PB11, beep rate varies with distance
 * - Bare Metal, no HAL
 */

// Defines and macros

#define TRIG_PORT			GPIOA
#define TRIG_PIN			7U

#define TRIG_PULSE_US		15U

#define ECHO_PORT			GPIOA
#define ECHO_PIN			6U
#define ECHO_AF				2U

#define ECHO_TIMEOUT_US		30000U

#define BUZZER_PORT			GPIOB
#define BUZZER_PIN			11U


typedef enum {
	INIT,
	IDLE,
	WAIT_RISING,
	WAIT_FALLING,
	DONE,
	ERROR
} hc_state_t;


// Global variables

static volatile hc_state_t hc_state = INIT;

static volatile uint32_t t_rise;
static volatile uint32_t t_fall;

static volatile uint32_t g_ms = 0;

// Clocks enable

static void gpio_init(void)
{
	// Enable GPIOA/B clock

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;

	// Set TRIG pin

	TRIG_PORT->MODER &= ~(3U << (TRIG_PIN * 2U));
	TRIG_PORT->MODER |= (1U << (TRIG_PIN * 2U));		// Output mode

	// Set ECHO pin

	ECHO_PORT->MODER &= ~(3U << (ECHO_PIN * 2U));
	ECHO_PORT->MODER |= (2U << (ECHO_PIN * 2U));		// AF mode

	uint32_t idx   = ECHO_PIN / 8U;
	uint32_t shift = (ECHO_PIN % 8U) * 4U;

	ECHO_PORT->AFR[idx] &= ~(0xFU << shift);
	ECHO_PORT->AFR[idx] |= ((uint32_t)ECHO_AF << shift);  // AF2

	// Set Buzzer pin

	BUZZER_PORT->MODER &= ~(3U << (BUZZER_PIN * 2U));
	BUZZER_PORT->MODER |= (1U << (BUZZER_PIN * 2U));
	BUZZER_PORT->BSRR = (1U << (BUZZER_PIN + 16U));
}

static void tim3_init(void)
{
	// Enable TIM3 clock
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM3EN;

	// prescaler | 24 MHz / (23 + 1) = 1 MHz -> 1 us per tick
	TIM3->PSC = 23U;

	// Period
	TIM3->ARR = 0xFFFFU;

	// Generates update event immediately (updated PSC and ARR)
	TIM3->EGR = TIM_EGR_UG;

	// Capture/compare 1 selection (0b01 = CC1 channel as input, tim_ic1 mapped on tim_ti1)
	TIM3->CCMR1 &= ~TIM_CCMR1_CC1S_Msk;

	TIM3->CCMR1 |= (1U << TIM_CCMR1_CC1S_Pos);

	// Capture/compare 1 output Polarity (rising edge)
	TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP);

	TIM3->CCER |= TIM_CCER_CC1E;

	// Capture/Compare 1 interrupt enable
	TIM3->DIER |= TIM_DIER_CC1IE;

	// clear capture/compare 1 interrupt flag
	TIM3->SR &= ~(TIM_SR_CC1IF);

	// Starts timer
	TIM3->CR1 |= TIM_CR1_CEN;
}

static void nvic_enable_interrupt(void)
{
	NVIC_SetPriority(TIM3_IRQn, 2);
	NVIC_EnableIRQ(TIM3_IRQn);
	__enable_irq();
}

static void systick_init_1ms(void)
{
	SysTick->LOAD = 24000U - 1U;
	SysTick->VAL = 0U;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk
				  | SysTick_CTRL_TICKINT_Msk
				  | SysTick_CTRL_ENABLE_Msk;
}

void TIM3_IRQHandler(void)
{
	uint32_t sr;

	sr = TIM3->SR;
	if (sr & TIM_SR_CC1OF) {
		hc_state = ERROR;
		TIM3->SR &= ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
		(void)TIM3->CCR1;
		return;
	}

	if ((sr & TIM_SR_CC1IF) == 0U) return;

	uint32_t t = TIM3->CCR1;

	if (hc_state == WAIT_RISING) {
		t_rise = t;
		TIM3->CCER |= TIM_CCER_CC1P;
		hc_state = WAIT_FALLING;
		return;
	}

	if (hc_state == WAIT_FALLING) {
		t_fall = t;
		TIM3->CCER &= ~TIM_CCER_CC1P;
		hc_state = DONE;
		return;
	}
}

void SysTick_Handler(void)
{
	g_ms++;
}

static uint32_t millis(void)
{
	return g_ms;
}

static void delay_us(uint32_t us)
{
	uint16_t start = (uint16_t) TIM3->CNT;
	while ((uint16_t)(TIM3->CNT - start) < us) {
		//wait
	}
}

static uint32_t calculateDistance(uint16_t t_rising, uint16_t t_falling)
{

	uint16_t echo_us = (uint16_t)(t_falling - t_rising);
	// speed of sound: 1 cm ~~ 58 us round trip
	return (uint32_t)echo_us / 58U;
}

int main(void)
{
	static volatile uint16_t echo_start_us = 0;
	uint32_t distance_cm = 0;
	static uint8_t buz_on = 0;
	static uint32_t buz_next_toggle_ms = 0;
	static uint32_t buz_period_ms = 0;

	gpio_init();
	tim3_init();
	nvic_enable_interrupt();
	systick_init_1ms();

	hc_state = IDLE;

	uint16_t last_trigger_us = 0;

	while (1){

		uint32_t now_ms = millis();

		if (buz_period_ms == 0U) {
			buz_on = 0;
			BUZZER_PORT->BSRR = (1U << (BUZZER_PIN + 16U));
		} else {
			uint32_t half = buz_period_ms / 2U;

			if ((int32_t)(now_ms - buz_next_toggle_ms) >= 0) {
				buz_on ^= 1U;
				if (buz_on) BUZZER_PORT->BSRR = (1U << BUZZER_PIN);
				else		BUZZER_PORT->BSRR = (1U << (BUZZER_PIN + 16U));
				buz_next_toggle_ms = now_ms + half;
			}
		}

		uint16_t now = TIM3->CNT;

		if (hc_state == IDLE) {
			if ((uint16_t)(now - last_trigger_us) >= 50000U) {

				TIM3->CCER &= ~TIM_CCER_CC1P;		// rising
				TIM3->SR &= ~(TIM_SR_CC1IF | TIM_SR_CC1OF);

				hc_state = WAIT_RISING;

				echo_start_us = (uint16_t) TIM3->CNT;

				TRIG_PORT->BSRR = (1U << TRIG_PIN);
				delay_us(TRIG_PULSE_US);
				TRIG_PORT->BSRR = (1U << (TRIG_PIN + 16U));

				last_trigger_us = (uint16_t)now;
			}
		}

		if (hc_state == DONE) {

			distance_cm = calculateDistance((uint16_t) t_rise, (uint16_t) t_fall);

			if (distance_cm < 20) {
				buz_period_ms = 100U;
			} else if (distance_cm < 40U) {
				buz_period_ms = 200U;
			} else if (distance_cm < 80U) {
				buz_period_ms = 350U;
			} else {
				buz_period_ms = 0;
			}

			hc_state = IDLE;
		}

		if ((hc_state == WAIT_RISING) || (hc_state == WAIT_FALLING)) {
			uint16_t elapsed = (uint16_t)((uint16_t) TIM3->CNT - echo_start_us);
			if (elapsed >= (uint16_t)ECHO_TIMEOUT_US) {
				hc_state = ERROR;
			}
		}

		if (hc_state == ERROR) {

			// Re-arm capture
			TIM3->SR &= ~(TIM_SR_CC1IF | TIM_SR_CC1OF);
			TIM3->CCER &= ~TIM_CCER_CC1P;		// rising
			hc_state = WAIT_RISING;
		}

	}

}
