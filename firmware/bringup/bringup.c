// SPDX-License-Identifier: MIT
// Copyright (c) 2026 noknok
/*
 * noknok PicoHub (MOD-0011) rev 1.0 - bring-up firmware v0.0.2 (DEV-117)
 * MCU: CH32X035G8U6 (QFN-28), default clock (HSI 48 MHz), ch32fun.
 *
 * NOT the product firmware. It answers the bring-up questions before the
 * power-manager firmware (DEV-85) is written:
 *
 *  1. BOOT / warm-reset test (runs by itself at power-on).
 *     PC17 is I2C SDA_A *and* the X035 BOOT-detect pin: high at reset may
 *     enter the factory bootloader. At a cold boot SDA_A's pull-up (R23 on
 *     +3V3_A) is still unpowered; at a warm reset it is not. Sequence, driven
 *     by the reset-cause flags so it needs no memory between resets:
 *        power-on   -> LED 1 blink x3, then a SOFTWARE reset
 *        software   -> LED 2 blinks x3, then a WATCHDOG reset (IWDG left unfed, ~5.6 s)
 *        watchdog   -> LED 3 blinks x3, then the heartbeat = PASS
 *     LED goes dark and stays dark after a reset = stuck in the bootloader = FAIL.
 *     If PC17 is LOW before a reset the test would prove nothing (bus A not
 *     powered, e.g. board fed only from the flash clamp): the LED fast-blinks
 *     and the test waits until PC17 is high.
 *
 *  2. Hardware default states: nothing below touches EN_A / EN_B / EN_USB /
 *     MEZZ_EN / EN_BUCK / PICO_RUN until a command is typed. Measure first.
 *
 *  3. Monitor (after the test): every second prints V_IN, V_5V, I_IN, CC sense,
 *     board temperature (NTC TH1 next to U2) and the digital lines.
 *     Commands over the debug link (`minichlink -T`, type a key + Enter):
 *        a  toggle EN_A (bus A 3.3 V)       b  toggle EN_B (bus B 3.3 V)
 *        u  toggle EN_USB (J2 VBUS)         m  toggle MEZZ_EN (mezzanine 5 V)
 *        k  toggle EN_BUCK (whole 5 V rail incl. the Pico!)
 *        p  pulse PICO_RUN low 100 ms (resets the Pico)
 *        0  release every control line (back to hardware defaults)
 *        d  raw dump of ADC channels 0..15   r  re-run the reset test   h  help
 *
 * Pin map = the board AS FABRICATED (read from hardware/kicad/MOD-0011.kicad_pcb,
 * U1 pads, 17 Oct 2026). docs/spec.md section 3 was rewritten from it on 18 Oct 2026.
 *   pin  3 PC0  A10  CC1_H_SENSE  J2 CC1 via 47k/47k        (A10/A13 to be
 *   pin  4 PC3  A13  CC2_H_SENSE  J2 CC2 via 47k/47k         confirmed: 'd')
 *   pin  5 PA0  A0   V_IN         100k/10k (x11)
 *   pin  9 PA4  A4   V_5V         100k/10k (x11)
 *   pin 10 PA5  A5   I_IN         INA180A2 on 10 mohm: 0.5 V per A
 *   pin 16 PB1  A9   THERMAL      NTC 10k B3950 (TH1) to GND, 10k (R37) to 3V3_MCU
 *   pin 14 PB3       PICO_RUN     open-drain ONLY (Pico RUN has its own pull-up)
 *   pin 15 PB4       MEZZ_EN      R30 100k pull-DOWN -> default OFF, drive high = on
 *   pin 17 PB6       EN_B         R10 100k pull-up   -> default ON, drive low = off
 *   pin 18 PB7       EN_BUCK      C35 22 nF, no pull -> buck self-starts; low = off
 *   pin 19 PB8       MEZZ_PRESENT pull-up, low = mezzanine takes the USB uplink
 *   pin 20 PB9       MEZZ_FAULT   pull-up, low = mezzanine fault
 *   pin 21 PB10      EN_USB       R11 100k pull-DOWN -> default OFF, drive high = on
 *   pin 22 PB11      EN_A         R9 100k pull-up    -> default ON, drive low = off
 *   pin 23 PB12      LED_K        status LED cathode, active LOW
 *   pin 27 PC17      SDA_A        read only here (BOOT-detect pin)
 */
#include "ch32fun.h"
#include <stdio.h>

#define FW_VERSION "0.0.2"

// Reset-cause flags in RCC->RSTSCKR (not named in ch32fun's X035 header;
// same bit layout as the CH32V003 family).
#define RST_RMVF   (1u << 24)
#define RST_PINF   (1u << 26)
#define RST_PORF   (1u << 27)
#define RST_SFTF   (1u << 28)
#define RST_IWDGF  (1u << 29)
#define RST_WWDGF  (1u << 30)
#define RST_LPWRF  (1u << 31)

// Port B pin numbers (all < 16, so funPinMode handles them).
#define P_PICO_RUN     PB3
#define P_MEZZ_EN      PB4
#define P_EN_B         PB6
#define P_EN_BUCK      PB7
#define P_MEZZ_PRESENT PB8
#define P_MEZZ_FAULT   PB9
#define P_EN_USB       PB10
#define P_EN_A         PB11
#define P_LED          PB12

// ADC channels
#define CH_V_IN   0
#define CH_V_5V   4
#define CH_I_IN   5
#define CH_THERM  9
#define CH_CC1    10
#define CH_CC2    13

// ---------------------------------------------------------------- helpers

static int sda_a_high( void ) { return (GPIOC->INDR >> 17) & 1; }

static void led( int on ) { funDigitalWrite( P_LED, on ? FUN_LOW : FUN_HIGH ); }

static void blink_code( int n )      // n short blinks, then a pause; 3 rounds
{
	for( int round = 0; round < 3; round++ )
	{
		for( int i = 0; i < n; i++ ) { led(1); Delay_Ms( 150 ); led(0); Delay_Ms( 250 ); }
		Delay_Ms( 900 );
	}
}

// Line control. "release" = input floating = the board's own pull decides.
static void line_release( int pin )   { funPinMode( pin, GPIO_CFGLR_IN_FLOAT ); }
static void line_drive_low( int pin )  { funDigitalWrite( pin, FUN_LOW );  funPinMode( pin, GPIO_CFGLR_OUT_10Mhz_OD ); }
static void line_drive_high( int pin ) { funDigitalWrite( pin, FUN_HIGH ); funPinMode( pin, GPIO_CFGLR_OUT_10Mhz_PP ); }

// Logical rail state we asked for: -1 = untouched (hardware default), 0 = off, 1 = on
static int st_a = -1, st_b = -1, st_usb = -1, st_mezz = -1, st_buck = -1;

static void set_en_a( int on )    { st_a = on;    if( on ) line_release( P_EN_A );    else line_drive_low( P_EN_A ); }
static void set_en_b( int on )    { st_b = on;    if( on ) line_release( P_EN_B );    else line_drive_low( P_EN_B ); }
static void set_en_buck( int on ) { st_buck = on; if( on ) line_release( P_EN_BUCK ); else line_drive_low( P_EN_BUCK ); }
static void set_mezz_en( int on ) { st_mezz = on; if( on ) line_drive_high( P_MEZZ_EN ); else line_release( P_MEZZ_EN ); }
static void set_en_usb( int on )
{
	// Hardware rule (spec 2.4): J2 and mezzanine cluster D share one USB D+/D- pair.
	if( on && !funDigitalRead( P_MEZZ_PRESENT ) )
	{
		printf( "REFUSED: mezzanine present (MEZZ_PRESENT low) - J2 VBUS stays off\n" );
		return;
	}
	st_usb = on; if( on ) line_drive_high( P_EN_USB ); else line_release( P_EN_USB );
}

static void release_all( void )
{
	line_release( P_EN_A ); line_release( P_EN_B ); line_release( P_EN_USB );
	line_release( P_MEZZ_EN ); line_release( P_EN_BUCK ); line_release( P_PICO_RUN );
	st_a = st_b = st_usb = st_mezz = st_buck = -1;
}

// ---------------------------------------------------------------- ADC

static void adc_init( void )
{
	RCC->APB2PCENR |= RCC_ADC1EN;
	RCC->APB2PRSTR |= RCC_ADC1RST;
	RCC->APB2PRSTR &= ~RCC_ADC1RST;
	ADC1->CTLR2 |= ADC_ADON;
	ADC1->CTLR3 &= ~ADC_CTLR3_CLK_DIV;
	ADC1->CTLR3 |= ADC_CTLR3_CLK_DIV_3;
	ADC1->RSQR1 = 0; ADC1->RSQR2 = 0;
	ADC1->SAMPTR1 = 0x00FFFFFF;           // longest sample time on every channel:
	ADC1->SAMPTR2 = 0x3FFFFFFF;           // the dividers are 9.1k / 23.5k sources
	ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;

	// analog mode (CFG = 0) on the six wired channels
	funPinMode( PA0, 0 ); funPinMode( PA4, 0 ); funPinMode( PA5, 0 );
	funPinMode( PB1, 0 ); funPinMode( PC0, 0 ); funPinMode( PC3, 0 );
}

static uint16_t adc_read( int ch )     // average of 16 conversions, 0..4095
{
	uint32_t sum = 0;
	ADC1->RSQR3 = ch;
	for( int i = 0; i < 16; i++ )
	{
		ADC1->CTLR2 |= ADC_SWSTART;
		while( !(ADC1->STATR & ADC_EOC) );
		sum += ADC1->RDATAR;
	}
	return sum / 16;
}

static int raw_to_mv( uint16_t raw ) { return (int)( (uint32_t)raw * 3300 / 4095 ); }

// NTC 10k B3950 resistance at -20..125 C in 5 C steps (computed, beta model)
static const uint32_t ntc_r[30] = {
	105385, 77898, 58246, 44026, 33621, 25925, 20175, 15837, 12535, 10000,
	8037, 6506, 5301, 4348, 3588, 2978, 2486, 2086, 1760, 1492,
	1270, 1087, 934, 805, 698, 606, 529, 463, 407, 359 };

// returns tenths of a degree C; THERMAL = 3.3 V * Rntc / (Rntc + 10k)
static int ntc_decic( uint16_t raw )
{
	if( raw >= 4094 ) return -999;                // open
	if( raw == 0 ) return 9999;                   // short
	uint32_t r = (uint32_t)10000 * raw / ( 4095 - raw );
	if( r >= ntc_r[0] ) return -200;
	for( int i = 1; i < 30; i++ )
		if( r >= ntc_r[i] )
			return ( i * 50 - 250 ) - (int)( ( r - ntc_r[i] ) * 50 / ( ntc_r[i-1] - ntc_r[i] ) );
	return 1250;
}

// ---------------------------------------------------------------- watchdog

static void iwdg_start_unfed( void )   // ~5.6 s (HSI/1024 = 46.9 kHz, /64, reload 4095), never fed
{
	IWDG->CTLR = 0x5555; IWDG->PSCR = 4;
	IWDG->CTLR = 0x5555; IWDG->RLDR = 0xFFF;
	IWDG->CTLR = 0xAAAA;
	IWDG->CTLR = 0xCCCC;
}

// ---------------------------------------------------------------- reset test

static void wait_sda_high( const char * why )
{
	if( sda_a_high() ) return;
	printf( "TEST INVALID until PC17/SDA_A is high (%s): bus A not powered? "
	        "Power the board from USB-C J1, not only from the flash clamp.\n", why );
	while( !sda_a_high() ) { led(1); Delay_Ms( 50 ); led(0); Delay_Ms( 50 ); }
	printf( "PC17 high now, continuing\n" );
}

static const char * cause_name( uint32_t f )
{
	if( f & RST_IWDGF ) return "WATCHDOG";
	if( f & RST_SFTF )  return "SOFTWARE";
	if( f & RST_PORF )  return "POWER-ON";
	if( f & RST_PINF )  return "PIN/debugger";
	if( f & RST_WWDGF ) return "WINDOW-WDG";
	if( f & RST_LPWRF ) return "LOW-POWER";
	return "unknown";
}

// ---------------------------------------------------------------- monitor

static volatile int cmd = 0;
void handle_debug_input( int numbytes, uint8_t * data )
{
	if( numbytes > 0 && data[0] > ' ' ) cmd = data[0];
}

static const char * st( int s ) { return s < 0 ? "default" : ( s ? "ON" : "OFF" ); }

static void help( void )
{
	printf( "keys: a/b EN_A/EN_B  u EN_USB  m MEZZ_EN  k EN_BUCK  p PICO_RUN pulse\n"
	        "      0 release all  d ADC dump  r reset test  h help\n" );
}

static void status_line( void )
{
	uint16_t vin = adc_read( CH_V_IN ), v5 = adc_read( CH_V_5V ), iin = adc_read( CH_I_IN );
	uint16_t th = adc_read( CH_THERM ), c1 = adc_read( CH_CC1 ), c2 = adc_read( CH_CC2 );
	int t = ntc_decic( th );
	printf( "V_IN %5d mV  V_5V %5d mV  I_IN %4d mA  CC1 %4d mV  CC2 %4d mV  T %d.%d C | "
	        "SDA_A %d PRES %d FAULT %d | A %s B %s USB %s MEZZ %s BUCK %s\n",
	        raw_to_mv( vin ) * 11, raw_to_mv( v5 ) * 11, raw_to_mv( iin ) * 2,
	        raw_to_mv( c1 ) * 2, raw_to_mv( c2 ) * 2, t / 10, ( t < 0 ? -t : t ) % 10,
	        sda_a_high(), funDigitalRead( P_MEZZ_PRESENT ), funDigitalRead( P_MEZZ_FAULT ),
	        st( st_a ), st( st_b ), st( st_usb ), st( st_mezz ), st( st_buck ) );
}

static void adc_dump( void )
{
	printf( "raw ADC (ch: raw / mV at pin):\n" );
	for( int ch = 0; ch < 16; ch++ )
	{
		uint16_t r = adc_read( ch );
		printf( "  A%-2d %4d / %4d mV\n", ch, r, raw_to_mv( r ) );
	}
}

// current logical state of a line; before its first command = its hardware default
static int def_on( int s, int default_on ) { return s < 0 ? default_on : s; }

// Boot log in RAM, outside anything the startup code touches (.bss is a few bytes at
// 0x20000000, the stack grows down from 0x20005000). RAM survives every reset but not a
// power loss, so "no valid log" = the first boot since power-up, whatever the reset
// flags say. That is what tells a cold start apart from a reset the factory bootloader
// may issue on its way to the app.
#define BOOTLOG_MAGIC 0x4E4B4231u            // "NKB1"
#define BOOTLOG_MAX   16
typedef struct { uint32_t magic, count; uint32_t flags[BOOTLOG_MAX]; uint8_t pc17[BOOTLOG_MAX]; } bootlog_t;
#define BOOTLOG ((volatile bootlog_t *)0x20003000)

int main( void )
{
	SystemInit();
	uint32_t flags = RCC->RSTSCKR;
	RCC->RSTSCKR |= RST_RMVF;                 // clear for the next reset

	funGpioInitAll();
	funDigitalWrite( P_LED, FUN_HIGH );       // LED off before it becomes an output
	funPinMode( P_LED, GPIO_CFGLR_OUT_10Mhz_PP );
	adc_init();

	int cold = ( BOOTLOG->magic != BOOTLOG_MAGIC || BOOTLOG->count >= BOOTLOG_MAX );
	if( cold ) { BOOTLOG->magic = BOOTLOG_MAGIC; BOOTLOG->count = 0; }
	BOOTLOG->flags[BOOTLOG->count] = flags;
	BOOTLOG->pc17[BOOTLOG->count] = sda_a_high();
	BOOTLOG->count++;

	printf( "\nnoknok PicoHub MOD-0011 bring-up v" FW_VERSION "  reset flags 0x%08lx = %s  PC17 %d%s\n",
	        (unsigned long)flags, cause_name( flags ), sda_a_high(), cold ? "  (first boot since power-up)" : "" );
	printf( "boot log since power-up:" );
	for( uint32_t i = 0; i < BOOTLOG->count; i++ )
		printf( "  #%lu 0x%08lx %s PC17=%d", (unsigned long)i + 1, (unsigned long)BOOTLOG->flags[i],
		        cause_name( BOOTLOG->flags[i] ), BOOTLOG->pc17[i] );
	printf( "\n" );

	// On the X035 the factory bootloader runs at EVERY power-up and starts the app with a
	// software reset, so a cold start arrives as POR + SOFTWARE (measured 17 Oct 2026).
	// Check POR first. The RAM log alone is not enough: RAM can survive an unplug when the
	// chip is kept alive through the debug lines.
	if( cold || ( flags & RST_PORF ) )
		flags = 0;                            // cold start: always step 1

	if( flags & RST_IWDGF )
	{
		blink_code( 3 );
		printf( "RESET TEST PASS: power-on -> software reset -> watchdog reset, "
		        "app came back each time with PC17 high.\n" );
	}
	else if( flags & RST_SFTF )
	{
		blink_code( 2 );
		wait_sda_high( "before watchdog reset" );
		printf( "STEP 2/3 back from SOFTWARE reset. Watchdog reset in ~5.6 s (PC17 %d)\n", sda_a_high() );
		iwdg_start_unfed();
		while( 1 ) { led(1); Delay_Ms( 50 ); led(0); Delay_Ms( 450 ); }
	}
	else
	{
		blink_code( 1 );
		wait_sda_high( "before software reset" );
		printf( "STEP 1/3 cold start. Software reset in 3 s (PC17 %d)\n", sda_a_high() );
		Delay_Ms( 3000 );
		NVIC_SystemReset();
		while( 1 );
	}

	// ---- monitor ----
	help();
	int tick = 0;
	while( 1 )
	{
		poll_input();
		int c = cmd; cmd = 0;
		switch( c )
		{
		case 'a': set_en_a( !def_on( st_a, 1 ) );       printf( "EN_A -> %s\n", st( st_a ) ); break;
		case 'b': set_en_b( !def_on( st_b, 1 ) );       printf( "EN_B -> %s\n", st( st_b ) ); break;
		case 'u': set_en_usb( !def_on( st_usb, 0 ) );   printf( "EN_USB -> %s\n", st( st_usb ) ); break;
		case 'm': set_mezz_en( !def_on( st_mezz, 0 ) ); printf( "MEZZ_EN -> %s\n", st( st_mezz ) ); break;
		case 'k': set_en_buck( !def_on( st_buck, 1 ) ); printf( "EN_BUCK -> %s\n", st( st_buck ) ); break;
		case 'p':
			line_drive_low( P_PICO_RUN ); Delay_Ms( 100 ); line_release( P_PICO_RUN );
			printf( "PICO_RUN pulsed\n" ); break;
		case '0': release_all(); printf( "all lines released (hardware defaults)\n" ); break;
		case 'd': adc_dump(); break;
		case 'r': printf( "re-running reset test\n" ); Delay_Ms( 50 ); NVIC_SystemReset(); break;
		case 'h': help(); break;
		default: break;
		}
		if( ++tick >= 100 ) { tick = 0; status_line(); }   // ~1 s
		led( tick < 5 );                                     // heartbeat
		Delay_Ms( 10 );
	}
}
