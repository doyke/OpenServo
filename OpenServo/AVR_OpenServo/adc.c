/*
    Copyright (c) 2006 Michael P. Thompson <mpthompson@gmail.com>

    Permission is hereby granted, free of charge, to any person 
    obtaining a copy of this software and associated documentation 
    files (the "Software"), to deal in the Software without 
    restriction, including without limitation the rights to use, copy, 
    modify, merge, publish, distribute, sublicense, and/or sell copies 
    of the Software, and to permit persons to whom the Software is 
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be 
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.

    $Id$
*/

#include <inttypes.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "openservo.h"
#include "config.h"
#include "adc.h"
#include "timer.h"

//
// ATtiny45/85
// ===========
//
// The 10-bit Analog to Digital Converter (ADC) on the ATtinyX5 MCU is used
// to provide power and position feedback from the servo circuitry.  The
// analog inputs are assigned as follows:
//
//  ADC0 (PB5) - Power input
//  ADC3 (PB3) - Position input
//
//
// ATmega8
// =======
//
// The 10-bit Analog to Digital Converter (ADC) on the ATmega MCU is used
// to provide power and position feedback from the servo circuitry.  The
// analog inputs are assigned as follows:
//
//  ADC0 (PC0) - Power input
//  ADC3 (PC1) - Position input
//
// NOTE: Unlike the ATtiny45/85 and ATmega168 the ATmega8 ADC sampling
// cannot be directly started from a timer compare.  Therefore, we have
// the timer interrupt kick off an ADC sample.
//
//
// ATmega168
// =========
//
// The 10-bit Analog to Digital Converter (ADC) on the ATmega MCU is used
// to provide power and position feedback from the servo circuitry.  The
// analog inputs are assigned as follows:
//
//  ADC0 (PC0) - Power input
//  ADC3 (PC1) - Position input
//

// Defines for the power and position channels.
#define ADC_CHANNEL_POWER       0
#define ADC_CHANNEL_POSITION    1

// Globals used to maintain ADC state and values.
volatile uint8_t adc_channel;
volatile uint8_t adc_power_ready;
volatile uint16_t adc_power_value;
volatile uint8_t adc_position_ready;
volatile uint16_t adc_position_value;

void adc_init(void)
// Initialize ADC conversion for reading current monitoring on ADC0 (PB5)
// and position on ADC3 (PB4).  The ADC clock prescaler of 64 is selected
// to yield a 125 KHz ADC clock. The timer/clock 0 is programmed to trigger
// ADC samples every 512 microseconds.
{
    // Read from position first.
    adc_channel = ADC_CHANNEL_POSITION;

    // Initialize flags and values.
    adc_power_ready = 0;
    adc_power_value = 0;
    adc_position_ready = 0;
    adc_position_value = 0;

    //
    // Initialize ADC registers to yield a 125KHz clock.
    //

#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    // Make sure port PB4 (ADC3) and PB5 (ADC0) are set as input.
    PORTB &= ~((1<<PB4) | (1<<PB5));

    // Disable digital input for ADC3 and ADC0 to reduce power consumption.
    DIDR0 |= (1<<ADC3D) | (1<<ADC0D);

    // Set the ADC multiplexer selection register.
    ADMUX = (0<<REFS2) | (0<<REFS1) | (0<<REFS0) |          // Select VCC as voltage reference.
            (0<<MUX3) | (0<<MUX2) | (1<<MUX1) | (1<<MUX0) | // Select ADC3 (PB3), no gain.
            (0<<ADLAR);                                     // Keep high bits right adjusted.

    // Set the ADC control and status register B.
    ADCSRB = (0<<BIN) |                                     // Gain working in unipolar mode.
             (0<<IPR) |                                     // No input polarity reversal.
             (0<<ADTS2) | (1<<ADTS1) | (1<<ADTS0);          // Timer/Counter0 Compare Match A.

    // Set the ADC control and status register A.
    ADCSRA = (1<<ADEN) |                                    // Enable ADC.
             (0<<ADSC) |                                    // Don's start yet, will be auto triggered.
             (1<<ADATE) |                                   // Start auto triggering.
             (1<<ADIE) |                                    // Activate ADC conversion complete interrupt.
             (1<<ADPS2) | (1<<ADPS1) | (0<<ADPS0);          // Prescale to divide input clock by 64.
#endif // __AVR_ATtiny45__ || __AVR_ATtiny85____

#if defined(__AVR_ATmega8__)
    // Make sure port PC0 (ADC0) and PC1 (ADC1) are set low.
    PORTC &= ~((1<<PC1) | (1<<PC0));

    // Set the ADC multiplexer selection register.
    ADMUX = (0<<REFS1) | (1<<REFS0) |                       // Select AVCC as voltage reference.
            (0<<MUX3) | (0<<MUX2) | (0<<MUX1) | (1<<MUX0) | // Select ADC1 (PC1) as analog input.
            (0<<ADLAR);                                     // Keep high bits right adjusted.


    // Set the ADC control and status register A.
    ADCSRA = (1<<ADEN) |                                    // Enable ADC.
             (1<<ADSC) |                                    // Start the first conversion.
             (0<<ADFR) |                                    // Free running disabled.
             (1<<ADIF) |                                    // Clear any pending interrupt.
             (1<<ADIE) |                                    // Activate ADC conversion complete interrupt.
             (1<<ADPS2) | (1<<ADPS1) | (0<<ADPS0);          // Prescale to divide input clock by 64.
#endif // __AVR_ATmega8____

#if defined(__AVR_ATmega168__)
    // Make sure port PC0 (ADC0) and PC1 (ADC1) are set low.
    PORTC &= ~((1<<PC1) | (1<<PC0));

    // Disable digital input for ADC0 and ADC1 to reduce power consumption.
    DIDR0 |= (1<<ADC1D) | (1<<ADC0D);

    // Set the ADC multiplexer selection register.
    ADMUX = (0<<REFS1) | (1<<REFS0) |                       // Select AVCC as voltage reference.
            (0<<MUX3) | (0<<MUX2) | (0<<MUX1) | (1<<MUX0) | // Select ADC1 (PC1) as analog input.
            (0<<ADLAR);                                     // Keep high bits right adjusted.

    // Set the ADC control and status register B.
    ADCSRB = (0<<ADTS2) | (1<<ADTS1) | (1<<ADTS0);          // Timer/Counter0 Compare Match A.

    // Set the ADC control and status register A.
    ADCSRA = (1<<ADEN) |                                    // Enable ADC.
             (0<<ADSC) |                                    // Don's start yet, will be auto triggered.
             (1<<ADATE) |                                   // Start auto triggering.
             (1<<ADIE) |                                    // Activate ADC conversion complete interrupt.
             (1<<ADPS2) | (1<<ADPS1) | (0<<ADPS0);          // Prescale to divide input clock by 64.
#endif // __AVR_ATmega168____

    //
    // Timer/Counter 0 settings.  This timer is used to trigger a ADC sample alternating
    // between the power and position channel every 512 microseconds.
    //

#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    // Set timer/counter0 control register A.
    TCCR0A = (0<<COM0A1) | (0<<COM0A0) |                    // Disconnect OCOA.
             (0<<COM0B1) | (0<<COM0B0) |                    // Disconnect OCOB.
             (1<<WGM01) | (0<<WGM00);                       // Mode 2 - clear timer on compare match.

    // Set timer/counter0 control register B.
    // Note: The clock is 8 MHz / 256 = 31.250 KHz or 32 microseconds per cycle.
    TCCR0B = (0<<FOC0A) | (0<<FOC0B) |                      // No force output compare A or B.
             (0<<WGM02) |                                   // Mode 2 - clear timer on compare match.
             (1<<CS02) | (0<<CS01) | (0<<CS00);             // Prescale divide by 256.

    // Set the timer/counter0 interrupt masks.
    TIMSK = (1<<OCIE0A) |                                   // Interrupt on compare match A.
            (0<<OCIE0B) |                                   // No interrupt on compare match B.
            (0<<TOIE0);                                     // No interrupt on overflow.

    // Set the compare match A value. Note: With a prescale of divide by
    // 256 and a compare match value of 8 an ADC sample is triggered every
    // 256 microseconds. Software below alternates between ADC0 and ADC3,
    // yielding a position sample every 512 microseconds with a power sample
    // interleaved every 512 microseconds.
    OCR0A = 0x08;
#endif // __AVR_ATtiny45__ || __AVR_ATtiny85____

#if defined(__AVR_ATmega8__)
    // Set timer/counter0 control register.
    TCCR0 = (1<<CS02) | (0<<CS01) | (0<<CS00);              // Prescale divide by 256.

    // Clear any pending interrupt.
    TIFR |= (1<<TOV0);                                      // Interrupt on overflow.

    // Set the timer/counter0 interrupt masks.
    TIMSK |= (1<<TOIE0);                                    // Interrupt on overflow.
#endif // __AVR_ATmega8____

#if defined(__AVR_ATmega168__)
    // Set timer/counter0 control register A.
    TCCR0A = (0<<COM0A1) | (0<<COM0A0) |                    // Disconnect OCOA.
             (0<<COM0B1) | (0<<COM0B0) |                    // Disconnect OCOB.
             (1<<WGM01) | (0<<WGM00);                       // Mode 2 - clear timer on compare match.

    // Set timer/counter0 control register B.
    // Note: The clock is 8 MHz / 256 = 31.250 KHz or 32 microseconds per cycle.
    TCCR0B = (0<<FOC0A) | (0<<FOC0B) |                      // No force output compare A or B.
             (0<<WGM02) |                                   // Mode 2 - clear timer on compare match.
             (1<<CS02) | (0<<CS01) | (0<<CS00);             // Prescale divide by 256.

    // Set the timer/counter0 interrupt masks.
    TIMSK0 = (1<<OCIE0A) |                                  // Interrupt on compare match A.
             (0<<OCIE0B) |                                  // No interrupt on compare match B.
             (0<<TOIE0);                                    // No interrupt on overflow.

    // Set the compare match A value. Note: With a prescale of divide by
    // 256 and a compare match value of 8 an ADC sample is triggered every
    // 256 microseconds. Software below alternates between ADC0 and ADC3,
    // yielding a position sample every 512 microseconds with a power sample
    // interleaved every 512 microseconds.
    OCR0A = 0x08;
#endif // __AVR_ATmega168____
}


#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__) || defined(__AVR_ATmega168__)

SIGNAL(SIG_OUTPUT_COMPARE0A)
// Handles timer/counter0 compare match A.
{
    // Increment the timer when positions are being sampled.
    if (adc_channel == ADC_CHANNEL_POSITION) timer_increment();
}

#endif // __AVR_ATtiny45__ || __AVR_ATtiny85__ || __AVR_ATmega168__

#if defined(__AVR_ATmega8__)

SIGNAL(SIG_OVERFLOW0)
// Handles timer/counter0 overflow.  This interrupts initiates the next
// ADC sample and assumes that the ADC sample will complete before the 
// next timer overflow interrupt.
{
    // Increment the timer when positions are being sampled.
    if (adc_channel == ADC_CHANNEL_POSITION) timer_increment();

    // Initiate an ADC sample.
    ADCSRA = (1<<ADEN) |                                    // Enable ADC.
             (1<<ADSC) |                                    // Start the first conversion.
             (0<<ADFR) |                                    // Free running disabled.
             (1<<ADIF) |                                    // Clear any pending interrupt.
             (1<<ADIE) |                                    // Activate ADC conversion complete interrupt.
             (1<<ADPS2) | (1<<ADPS1) | (0<<ADPS0);          // Prescale to divide input clock by 64.

    // Reset the counter to interrupt in 8 tics.
    TCNT0 = 256 - 8;
}

#endif // __AVR_ATmega8__

SIGNAL(SIG_ADC)
// Handles ADC interrupt.
{
    // Read the 10-bit ADC value.
    uint16_t new_value = ADCW;

    // Is the position or power channel being read?
    if (adc_channel == ADC_CHANNEL_POSITION)
    {
        // Save the new position value.
        adc_position_value = new_value;

        // Flag the position value as ready.
        adc_position_ready = 1;

        // Switch to power for the next reading.
        adc_channel = ADC_CHANNEL_POWER;

#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
        // Set the ADC multiplexer selection register.
        ADMUX = (0<<REFS2) | (0<<REFS1) | (0<<REFS0) |          // Select VCC as voltage reference.
                (0<<MUX3) | (0<<MUX2) | (0<<MUX1) | (0<<MUX0) | // Select ADC0 (PB5), no gain.
                (0<<ADLAR);                                     // Keep high bits right adjusted.
#endif

#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega168__)
        // Set the ADC multiplexer selection register.
        ADMUX = (0<<REFS1) | (0<<REFS0) |                       // Select AVCC as voltage reference.
                (0<<MUX3) | (0<<MUX2) | (0<<MUX1) | (0<<MUX0) | // Select ADC0 (PC0) as analog input.
                (0<<ADLAR);                                     // Keep high bits right adjusted.
#endif
    }
    else
    {
        // Save the new power value.
        adc_power_value = new_value;

        // Flag the power value as ready.
        adc_power_ready = 1;

        // Switch to position for the next reading.
        adc_channel = ADC_CHANNEL_POSITION;

#if defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
        // Set the ADC multiplexer selection register.
        ADMUX = (0<<REFS2) | (0<<REFS1) | (0<<REFS0) |          // Select VCC as voltage reference.
                (0<<MUX3) | (0<<MUX2) | (1<<MUX1) | (1<<MUX0) | // Select ADC3 (PB3), no gain.
                (0<<ADLAR);                                     // Keep high bits right adjusted.
#endif

#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega168__)
        // Set the ADC multiplexer selection register.
        ADMUX = (0<<REFS1) | (1<<REFS0) |                       // Select AVCC as voltage reference.
                (0<<MUX3) | (0<<MUX2) | (0<<MUX1) | (1<<MUX0) | // Select ADC1 (PC1) as analog input.
                (0<<ADLAR);                                     // Keep high bits right adjusted.
#endif
    }
}

