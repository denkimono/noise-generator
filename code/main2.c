#define F_CPU 1000000UL  /* 1 MHz internal oscillator */

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <inttypes.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define TRUE	1
#define FALSE	0

#define BATTERY_THRESHOLD	70 // reduce this if you get the "bL" display with 

#define ENABLE_A(line)	PORTA |= line
#define DISABLE_A(line)	PORTA &= ~line
#define ENABLE_B(line)	PORTB |= line
#define DISABLE_B(line)	PORTB &= ~line

// IO

// DISPLAY
#define SEG_A  _BV(PB0)
#define SEG_B  _BV(PB1)
#define SEG_C  _BV(PB3)
#define SEG_D  _BV(PB2)

#define SEG_E  _BV(PA7)
#define SEG_F  _BV(PA6)
#define SEG_G  _BV(PA5)
#define SEG_DP _BV(PA4)

#define DIG_2  _BV(PA3)
#define DIG_1  _BV(PA0)

// misc
#define PUMP  _BV(PA2)
#define TX	  _BV(PA5)

// display modes
#define DISPLAY_ANIMATION	0
#define DISPLAY_DIGIT		1
#define DISPLAY_BATTERY		2

// voltage booster stuff
volatile unsigned char pump_toggle=0;

// display stuff
volatile unsigned char display_on=FALSE;
volatile char screen_display_value=0;
volatile unsigned char current_digit=0;
volatile unsigned char display_mode=DISPLAY_ANIMATION;

// random number stuff
volatile char bitcount=0;
volatile unsigned char random_byte=0;
volatile unsigned char max_adc=0;
volatile char got_byte=FALSE;

// digit bit patterns
const unsigned char digits[] PROGMEM =
{
	0x10,// 0 - b'0001 0000'
	0xf1,// 1 - b'1111 0001'*
	0x24,// 2 - b'0010 0100'*
	0x60,// 3 - b'0110 0000'*
	0xc1,// 4 - b'1100 0001'*
	0x42,// 5 - b'0100 0010'
	0x02,// 6 - b'0000 0010'*
	0xf8,// 7 - b'1111 1000'*
	0x00,// 8 - b'0000 0000'*
	0x40,// 9 - b'0100 0000'
	0xef, // '-' - b'1011 1111'
	0xff, // ' ' - b'1111 1111'*
};

#define LED_DASH	0x0A
#define LED_BLANK	0x0B
#define LED_L		0x1f// 'L' - b'0001 1011'
#define LED_B		0x0b// 'b' - b'0000 1011'

// animation bit patterns
const unsigned char spinner[] PROGMEM =
{
	0xfe,// b'1111 1110'
	0xfd,// b'1111 1101'
	0xfb,// b'1111 1011'
	0x7f,// b'0111 1111'
	0xbf,// b'1011 1111'
	0xdf,// b'1101 1111'
};
#define FRAMES	0x06

void set_segments(unsigned char pattern)
{
	// set new display value
	PORTB&=0xF0;
	PORTB|=pattern&0x0F;

	PORTA&=0x0F;
	PORTA|=pattern&0xF0;
}

// display the appropriate 'numeral' on 'digit'
void display_digit(unsigned char numeral,char digit)
{
	// clear display
	DISABLE_A(DIG_1);
	DISABLE_A(DIG_2);
	
	// get digit bit pattern (will fail if numeral is >9 or <0)
	unsigned char pattern = pgm_read_byte(&digits[numeral]);
	
	// set new display value
	set_segments(pattern);
	
	// enable digit
	ENABLE_A(digit);
}

void display_multiplexer(void)
{
	if(display_on==FALSE)
	{
		// clear display
		DISABLE_A(DIG_1);
		DISABLE_A(DIG_2);
		return;
	}
	
	// display multiplexer

	if(display_mode==DISPLAY_DIGIT)
	{
		// handle out of range
		if(screen_display_value>99 || screen_display_value<-9)
		{
			display_digit(LED_DASH,current_digit?DIG_1:DIG_2);
			current_digit=~current_digit;
			return;
		}

		if(current_digit)
		{
			if(screen_display_value>=0)
			{
				// suppress leading zero
				if(screen_display_value>=10)
					display_digit(screen_display_value/10,DIG_1);
				else
					display_digit(LED_BLANK,DIG_1);
			}
			else// show minus
			{
				display_digit(LED_DASH,DIG_1);
			}
		}
		else
		{
			if(screen_display_value>=0)
			{
				display_digit(screen_display_value%10,DIG_2);
			}
			else
			{
				if(screen_display_value>=-9)
				{
					display_digit(abs(screen_display_value)%10,DIG_2);
				}
			}
		}
	}

	if(display_mode==DISPLAY_ANIMATION)
	{
		// clear display
		DISABLE_A(DIG_1);
		DISABLE_A(DIG_2);
		
		// set new display value
		set_segments(screen_display_value);
	
		// enable digit
		ENABLE_A(current_digit?DIG_1:DIG_2);
	}
	
	if(display_mode==DISPLAY_BATTERY)
	{
		// clear display
		DISABLE_A(DIG_1);
		DISABLE_A(DIG_2);

		if(current_digit)
			set_segments(LED_B);
		else
			set_segments(LED_L);

		// enable digit
		ENABLE_A(current_digit?DIG_1:DIG_2);
	}
	
	current_digit=~current_digit;
}

// multiplexer interrupt
ISR(TIM0_COMPA_vect)
{
	display_multiplexer();
	
	if(pump_toggle&0x01)
		ENABLE_A(PUMP);
	else
		DISABLE_A(PUMP);
		
	pump_toggle++;
}

// ADC complete interrupt
ISR(ADC_vect)
{
	unsigned char val=(unsigned char)ADCH;

	// for battery/system self check
	if(max_adc<val)
		max_adc=val;

	// avoid limits (ADC is clipping)
	if(val<255 && val>0)
	{
		// take a bit
		random_byte<<=1;
		random_byte|=(0x01&val)!=0?1:0;
		bitcount++;

		if(bitcount>=8)
		{
			bitcount=0;
			got_byte=TRUE;
		}
		else
		{
			// start another conversion
			ADCSRA |= (1 << ADSC);
		}
	}
	else
	{
		// start another conversion
		ADCSRA |= (1 << ADSC);
	}
	_delay_ms(1);
}

#define SERIAL_DELAY 100 // 9600
void transmit_byte(unsigned int byte)
{
	int n;

	cli();

	// start bit
	DISABLE_A(TX);
	_delay_us(SERIAL_DELAY);
	
	for(n=0;n<8;n++)
	{
		if(!(byte&0x01))
		{
			DISABLE_A(TX);
		}
		else
		{
			ENABLE_A(TX);
		}
		byte>>=1;
		
		_delay_us(SERIAL_DELAY);
	}

	// stop bit
	ENABLE_A(TX);
	_delay_us(SERIAL_DELAY);

	sei();

}

void send_serial_byte(unsigned char byte)
{
	char buffer[7];
	char *ptr=buffer;

	buffer[2]=byte%10+'0';
	byte/=10;
	buffer[1]=byte%10+'0';
	byte/=10;
	buffer[0]=byte%10+'0';

//	buffer[3]=',';
	buffer[3]=0x0d;
	buffer[4]=0x0a;
	buffer[5]=0;

	while(*ptr!=0)
	{
		transmit_byte(*ptr);
		ptr++;
	}
}

int main (void)
{
	wdt_disable();

	TCCR0A = (1<<WGM01);  // Mode = CTC
	
	// set timer0 prescaler
	TCCR0B = (1<<CS01);// TIMER0 = FCPU/8
	
	//initialize Counter
	TCNT0=0;
	
	// set counter compare value
	OCR0A=100;
	
	// enable output compare interrupt (enable multiplexer)
	TIMSK0=(1<<OCIE1A);

	// set up i/o pins
    DDRA = SEG_E|SEG_F|SEG_G|SEG_DP|DIG_1|DIG_2|PUMP;
    DDRB = SEG_A|SEG_B|SEG_C|SEG_D;

	// setup ADC
	
	// ADC reference voltage (set to VCC)
	ADMUX =0;//(1 << REFS1); 

	// select ADC channel (ADC1)
	ADMUX |=(1<<MUX0);
	
	// only take 8-bit resolution (read ADCH)
	ADCSRB |= (1 << ADLAR); 

	// ADC clock prescaler
	ADCSRA |= (1 << ADPS1) | (1 << ADPS0); 

	// enable ADC interupt
	ADCSRA |= (1 << ADIE); 

	// enable ADC hardware
	ADCSRA |= (1 << ADEN); 

	// start ADC conversion
	ADCSRA |= (1 << ADSC);

	// enable interrupts
	sei();

/*
	// test serial TX
	while(1)
	{
			do
			{
				// start ADC conversion
				got_byte=FALSE;
				ADCSRA |= (1 << ADSC);
				while(got_byte==FALSE);
			}
			while(random_byte>=100);
//			while(random_byte==0 || random_byte==255);
			
			send_serial_byte(random_byte);

	}
*/
	display_on=TRUE;
	unsigned char frame=0;
	char spins=3;

	display_digit(LED_BLANK,DIG_1);
	display_digit(LED_BLANK,DIG_2);

	// do start animation, give charge pump time to pump up
	while(spins>0)
	{
		screen_display_value=pgm_read_byte(&spinner[frame]);
		
		frame++;

		if(frame%FRAMES==0)
		{
			frame%=FRAMES;
			spins--;
		}

		_delay_ms(25);
	}
	display_on=FALSE;

	// check the battery by checking a few numbers
	max_adc=0;
	char count=50;
    while(count)
    {
		while(got_byte==FALSE);
		count--;
		got_byte=FALSE;
		// start ADC conversion again
		ADCSRA |= (1 << ADSC);
	}
	
	// see if max_adc looks sensible
	if(max_adc<BATTERY_THRESHOLD)
	{
		display_mode=DISPLAY_BATTERY;
		display_on=TRUE;
		while(1);
	}

    while(1)
    {
		_delay_ms(10);
		
		if(got_byte==TRUE)
		{
			got_byte=FALSE;
			
			if(random_byte<100 && random_byte>=0)
			{
				display_mode=DISPLAY_DIGIT;
				screen_display_value = random_byte;

				//send_serial_byte(random_byte);
				//transmit_byte(random_byte);

				//enable display
				display_on=TRUE;
				
				// time out after 10 seconds and go to sleep
				int i;
				for(i=0;i<10;i++)
				{
					_delay_ms(1000);
				}

				// turn everything off
				DISABLE_A(DIG_1);
				DISABLE_A(DIG_2);
				set_sleep_mode(SLEEP_MODE_PWR_DOWN);
				sleep_enable();
				cli();
				// night night
				sleep_cpu();

				while(1);
			}

			// start ADC conversion again
			ADCSRA |= (1 << ADSC);
		}
		else
		{
			// taken too long, battery is probably failing
			display_mode=DISPLAY_BATTERY;
			display_on=TRUE;
			while(1);
		}
		
   }
}
