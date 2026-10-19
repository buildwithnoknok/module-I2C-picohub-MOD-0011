// SPDX-License-Identifier: MIT
// Copyright (c) 2026 noknok
/*
 * noknok PicoHub (MOD-0011) power manager firmware - CH32X035G8U6, ch32fun.
 *
 * What it does (DEV-121, ADR-004, design page "MOD-0011 power manager - I2C interface and
 * input voltage policy"):
 *   - USB-PD sink on J1. Cold start: hold the 5 V buck off, take the 5 V contract, release
 *     the buck, then step to 9 V when the charger offers it. Any charger works: a 5 V-only
 *     adapter runs the board at 5 V ("limited power" notice, not an error).
 *   - Interlocks, always enforced:
 *       J2 VBUS (USB host port) only when a sink is detected on its CC lines AND no mezzanine
 *       has taken the USB uplink (MEZZ_PRESENT); on a 5 V-only source only if it advertises
 *       >= 1.5 A. A charger plugged into J2 reads like an empty socket, so J2 stays off.
 *       MEZZ_EN (mezzanine 5 V + enable line) on when MEZZ_PRESENT is asserted; off on MEZZ_FAULT.
 *       Bus A is never switched off by this firmware (the X035's own link to the brain).
 *   - Protection (thresholds are PLACEHOLDERS until the load tests):
 *       input over-current from I_IN, over-temperature from NTC TH1 (no voltage step-down: it
 *       would raise the current through the hub), MEZZ_FAULT, 5 V rail under-voltage.
 *   - Status LED, independent watchdog, reset cause (the X035 factory bootloader starts the
 *     app with a software reset at every power-up, so a cold start = POWER-ON + SOFTWARE:
 *     the POWER-ON flag is checked first).
 *   - The noknok I2C module interface on bus A (i2c_module.h, DEV-122): enumeration as type 0x07,
 *     telemetry, steering commands. Input voltage by demand (9 -> 15 -> 20 V and back) and the
 *     brain's minimum input voltage.
 * Not here yet: the bootloader (DEV-124).
 *
 * Debug link (`minichlink -T`, see ../readme.md): a status line every 2 s, and keys
 *   s status now   v cycle the voltage target 9 -> 15 -> 20 -> 9 V (bench test only)
 *   c clear faults   t +5 C on the temperature reading (simulated heat)   T offset off
 *   w stop feeding the watchdog (reset test)   h help
 *
 * Pin map = the board as fabricated (docs/spec.md section 3):
 *   PA0 A0 V_IN x11     PA4 A4 V_5V x11     PA5 A5 I_IN 0.5 V/A (+14 mA offset)
 *   PB1 A9 NTC TH1      PC0 A10 / PC3 A13 J2 CC1/CC2 sense, x2
 *   PB3 PICO_RUN (open-drain only)   PB4 MEZZ_EN (pull-down, drive high = on)
 *   PB6 EN_B (pull-up, drive low = off)   PB7 EN_BUCK (self-starts, drive low = off)
 *   PB8 MEZZ_PRESENT (low = present)   PB9 MEZZ_FAULT (low = fault)
 *   PB10 EN_USB (pull-down, drive high = on)   PB11 EN_A (pull-up)   PB12 LED_K (active low)
 *   PC14/PC15 USB-PD CC1/CC2 (external 5.1 k Rd)   PC16/PC17 I2C bus A (not used yet)
 */
#include "ch32fun.h"
#include <stdbool.h>
#include <stdio.h>

#define USBPD_IMPLEMENTATION
#include "usbpd.h"

#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0

// Print only while a debugger is attached: without one, the first printf would stall ~200 ms,
// long enough to miss a USB-PD reply deadline.
#define LOG( ... ) do { if ( DidDebuggerAttach() ) printf( __VA_ARGS__ ); } while ( 0 )

// ------------------------------------------------------------------ thresholds (placeholders)
// To be set from the load tests at 20 V / 3 A; until then conservative placeholders.
#define LIMIT_IN_MA_MAX     3000  // board design limit, 20 V / 3 A
#define OC_TRIP_MS           200  // over the limit this long -> shed the next load
#define OC_RECOVER_PCT        80  // ... back under this share of the limit ...
#define OC_RECOVER_MS       5000  // ... for this long -> loads return
#define T_HOT_ON             750  // 0.1 C: notice "hot"
#define T_HOT_OFF            700
#define T_SHED_ON            850  // shed J2 + MEZZ_EN
#define T_SHED_OFF           750
#define T_CRIT_ON            950  // buck off
#define T_CRIT_OFF           700
#define D8_SHEDS               3  // this many temperature sheds ...
#define D8_WINDOW_MS      600000  // ... within 10 minutes -> MEZZ_EN stays off until cleared
#define V5_UNDER_MV         4400  // 5 V rail under-voltage (not on a 5 V-only source)
#define MEZZ_FAULT_HOLD_MS  1000  // MEZZ_EN returns this long after MEZZ_FAULT is released
#define CC_RD_MIN_MV         550  // J2 CC line (after the x2 divider) when a sink's Rd pulls it:
#define CC_RD_MAX_MV        1500  //   ~0.9 V; empty socket ~4 V, cable Ra ~0.2 V
#define SINK_ATTACH_MS       100  // USB-C tCCDebounce
#define SINK_DETACH_MS        20
#define MEZZ_PRESENT_MS       50
#define BUCK_HOLD_MAX_MS     500  // cold start: buck held off at most this long
#define STEP_UP_DELAY_MS     200  // cold start: 5 V contract + buck on, rails settle, then 9 V
#define NO_PD_AFTER_MS      1000  // attached, no Source_Capabilities -> 5 V-only source
#define PD_REPLY_TIMEOUT_MS 1000  // no Accept / PS_RDY -> soft reset and try again
#define I_IN_OFFSET_MA        14  // measured offset of the INA180 path (DEV-117 step 4)
#define DEMAND_UP_PCT         80  // input current above this share of the contract ...
#define DEMAND_UP_MS        2000  // ... for this long -> next voltage level up
#define DEMAND_DOWN_PCT       60  // input power below this share of the next lower level ...
#define DEMAND_DOWN_MS     30000  // ... for this long -> one level down (never below 9 V / the minimum)

// ------------------------------------------------------------------ status (exposed by DEV-122)
enum { PD_NONE = 0, PD_NEGOTIATING = 1, PD_CONTRACT = 2, PD_FAILED = 3 };   // GET_STATUS byte 1
enum { RAIL_BUS_A = 1 << 0, RAIL_BUS_B = 1 << 1, RAIL_J2 = 1 << 2, RAIL_MEZZ = 1 << 3, RAIL_BUCK = 1 << 4 };
enum { IN_J2_SINK = 1 << 0, IN_MEZZ_PRESENT = 1 << 1, IN_MEZZ_FAULT = 1 << 2, IN_MIN_SET = 1 << 3, IN_MIN_SUSPENDED = 1 << 4 };
enum { F_OVERCURRENT = 1 << 0, F_TEMP_SHED = 1 << 1, F_TEMP_CRIT = 1 << 2, F_MEZZ = 1 << 3,
       F_V5_UNDER = 1 << 4, F_PD_FAILED = 1 << 5, F_NTC = 1 << 6 };          // latched
enum { N_5V_ONLY = 1 << 0, N_WEAK = 1 << 1, N_HOT = 1 << 2, N_J2_INTERLOCK = 1 << 3,
       N_J2_POWER = 1 << 4, N_MIN_NOT_MET = 1 << 5 };                        // live
enum { WISH_OFF = 0, WISH_ON = 1, WISH_AUTO = 2 };
enum { RC_POWER_ON = 0, RC_WATCHDOG = 1, RC_OTHER = 2, RC_SOFTWARE = 3 };

static struct
{
	// measurements
	uint16_t vin_mv, v5_mv, iin_ma, cc1_mv, cc2_mv;
	int16_t temp_dc;       // 0.1 C; -999 = sensor open, 9999 = sensor short
	// PD
	uint8_t pd_state;
	uint16_t contract_mv, contract_ma;
	uint16_t target_mv;    // what the policy asks the charger for (9000 by default)
	uint16_t min_mv;       // the brain's minimum input voltage (0, 15000, 20000)
	// rails, inputs, faults
	uint8_t rails, inputs, faults, notices;
	uint8_t wish_bus_b, wish_j2, wish_mezz;   // set by the brain (SET_RAIL)
	uint8_t reset_cause;
} hub;

// ------------------------------------------------------------------ pins
#define P_PICO_RUN     PB3
#define P_MEZZ_EN      PB4
#define P_EN_B         PB6
#define P_EN_BUCK      PB7
#define P_MEZZ_PRESENT PB8
#define P_MEZZ_FAULT   PB9
#define P_EN_USB       PB10
#define P_EN_A         PB11
#define P_LED          PB12

#define CH_V_IN  0
#define CH_V_5V  4
#define CH_I_IN  5
#define CH_THERM 9
#define CH_CC1   10
#define CH_CC2   13

#define RST_RMVF  ( 1u << 24 )   // RCC->RSTSCKR flags (not named in ch32fun's X035 header)
#define RST_PORF  ( 1u << 27 )
#define RST_SFTF  ( 1u << 28 )
#define RST_IWDGF ( 1u << 29 )

// "release" = input, the board's own pull decides; that is the hardware default of every line.
static void line_release( int pin )    { funPinMode( pin, GPIO_CFGLR_IN_FLOAT ); }
static void line_drive_low( int pin )  { funDigitalWrite( pin, FUN_LOW );  funPinMode( pin, GPIO_CFGLR_OUT_10Mhz_OD ); }
static void line_drive_high( int pin ) { funDigitalWrite( pin, FUN_HIGH ); funPinMode( pin, GPIO_CFGLR_OUT_10Mhz_PP ); }

// Each output keeps its last state so a line is only reconfigured when it changes.
static void out_buck( bool on )
{
	if( on == !!( hub.rails & RAIL_BUCK ) ) return;
	if( on ) { line_release( P_EN_BUCK ); hub.rails |= RAIL_BUCK; }
	else     { line_drive_low( P_EN_BUCK ); hub.rails &= ~RAIL_BUCK; }
}
static void out_bus_b( bool on )
{
	if( on == !!( hub.rails & RAIL_BUS_B ) ) return;
	if( on ) { line_release( P_EN_B ); hub.rails |= RAIL_BUS_B; }
	else     { line_drive_low( P_EN_B ); hub.rails &= ~RAIL_BUS_B; }
}
static void out_j2( bool on )
{
	if( on == !!( hub.rails & RAIL_J2 ) ) return;
	if( on ) { line_drive_high( P_EN_USB ); hub.rails |= RAIL_J2; }
	else     { line_release( P_EN_USB ); hub.rails &= ~RAIL_J2; }
}
static void out_mezz( bool on )
{
	if( on == !!( hub.rails & RAIL_MEZZ ) ) return;
	if( on ) { line_drive_high( P_MEZZ_EN ); hub.rails |= RAIL_MEZZ; }
	else     { line_release( P_MEZZ_EN ); hub.rails &= ~RAIL_MEZZ; }
}
static void led( bool on ) { funDigitalWrite( P_LED, on ? FUN_LOW : FUN_HIGH ); }

// ------------------------------------------------------------------ time
// SysTick runs at HCLK (48 MHz) as a 64-bit counter; read high/low/high so a carry between the
// two 32-bit halves can never produce a jump.
static uint32_t millis( void )
{
	volatile uint32_t * c = (volatile uint32_t *)&SysTick->CNT;
	uint32_t hi, lo;
	do { hi = c[1]; lo = c[0]; } while( hi != c[1] );
	return (uint32_t)( ( ( (uint64_t)hi << 32 ) | lo ) / ( FUNCONF_SYSTEM_CORE_CLOCK / 1000 ) );
}

// ------------------------------------------------------------------ ADC
static void adc_init( void )
{
	RCC->APB2PCENR |= RCC_ADC1EN;
	RCC->APB2PRSTR |= RCC_ADC1RST;
	RCC->APB2PRSTR &= ~RCC_ADC1RST;
	ADC1->CTLR2 |= ADC_ADON;
	ADC1->CTLR3 &= ~ADC_CTLR3_CLK_DIV;
	ADC1->CTLR3 |= ADC_CTLR3_CLK_DIV_3;
	ADC1->RSQR1 = 0; ADC1->RSQR2 = 0;
	ADC1->SAMPTR1 = 0x00FFFFFF;   // longest sample time: the dividers are 9.1 k / 23.5 k sources
	ADC1->SAMPTR2 = 0x3FFFFFFF;
	ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;
	funPinMode( PA0, 0 ); funPinMode( PA4, 0 ); funPinMode( PA5, 0 );   // analog mode
	funPinMode( PB1, 0 ); funPinMode( PC0, 0 ); funPinMode( PC3, 0 );
}

static uint16_t adc_read( int ch )   // average of 16 conversions, 0..4095 (~0.3 ms)
{
	uint32_t sum = 0;
	ADC1->RSQR3 = ch;
	for( int i = 0; i < 16; i++ )
	{
		ADC1->CTLR2 |= ADC_SWSTART;
		while( !( ADC1->STATR & ADC_EOC ) );
		sum += ADC1->RDATAR;
	}
	return sum / 16;
}

static uint32_t raw_mv( uint16_t raw ) { return (uint32_t)raw * 3300 / 4095; }

// NTC 10k B3950 resistance at -20..125 C in 5 C steps (beta model), as in the bring-up firmware
static const uint32_t ntc_r[30] = {
	105385, 77898, 58246, 44026, 33621, 25925, 20175, 15837, 12535, 10000,
	8037, 6506, 5301, 4348, 3588, 2978, 2486, 2086, 1760, 1492,
	1270, 1087, 934, 805, 698, 606, 529, 463, 407, 359 };

static int16_t ntc_decic( uint16_t raw )   // THERMAL = 3.3 V * Rntc / (Rntc + 10 k)
{
	if( raw >= 4090 ) return -999;   // open
	if( raw <= 5 ) return 9999;      // short
	uint32_t r = (uint32_t)10000 * raw / ( 4095 - raw );
	if( r >= ntc_r[0] ) return -200;
	for( int i = 1; i < 30; i++ )
		if( r >= ntc_r[i] )
			return ( i * 50 - 250 ) - (int)( ( r - ntc_r[i] ) * 50 / ( ntc_r[i - 1] - ntc_r[i] ) );
	return 1250;
}

static int16_t temp_offset_dc;   // debug key 't': simulated heat for the protection test

// One channel per call, round robin: every value is refreshed every ~6 ms, and the main loop
// never blocks long enough to miss a USB-PD deadline.
static void adc_step( void )
{
	static uint8_t n;
	switch( n )
	{
	case 0: hub.vin_mv = raw_mv( adc_read( CH_V_IN ) ) * 11; break;
	case 1: hub.v5_mv  = raw_mv( adc_read( CH_V_5V ) ) * 11; break;
	case 2: { int32_t ma = (int32_t)raw_mv( adc_read( CH_I_IN ) ) * 2 - I_IN_OFFSET_MA;
	          hub.iin_ma = ma < 0 ? 0 : ma; } break;
	case 3: { int16_t t = ntc_decic( adc_read( CH_THERM ) );
	          hub.temp_dc = ( t == -999 || t == 9999 ) ? t : t + temp_offset_dc; } break;
	case 4: hub.cc1_mv = raw_mv( adc_read( CH_CC1 ) ) * 2; break;
	case 5: hub.cc2_mv = raw_mv( adc_read( CH_CC2 ) ) * 2; break;
	}
	n = ( n + 1 ) % 6;
}

// ------------------------------------------------------------------ USB-PD policy
static struct
{
	uint32_t caps_seen;    // last Source_Capabilities count handled
	int8_t idx;            // PDO index of the running contract, -1 = none
	int8_t req_idx;        // PDO index of the request in flight, -1 = none
	uint16_t rp_ma;        // Type-C current advertised by the source's Rp (no PD)
	USBPD_State_e last;    // library state on the previous poll
	uint32_t state_t;      // when the library state last changed
	uint32_t attach_t;     // when the source was detected
	uint8_t retries;
	bool soft_reset_due;   // warm start: ask the source for its capabilities again
} pd;

static const USBPD_SourcePDO_t * pdo_at( int i )
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t n = USBPD_GetCapabilities( &caps );
	return ( i >= 0 && (size_t)i < n ) ? &caps->Source[i] : 0;
}

static bool pdo_fixed( const USBPD_SourcePDO_t * p ) { return p && p->Header.PDOType == eUSBPD_PDO_FIXED; }

// The fixed PDO with the highest voltage <= target. PDO 0 is always the 5 V one.
static int pdo_choose( uint16_t target_mv )
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t n = USBPD_GetCapabilities( &caps );
	int best = 0;
	uint32_t best_mv = 0;
	for( size_t i = 0; i < n; i++ )
	{
		const USBPD_SourcePDO_t * p = &caps->Source[i];
		if( !pdo_fixed( p ) ) continue;
		uint32_t mv = p->FixedSupply.VoltageIn50mV * 50;
		if( mv <= target_mv && mv > best_mv ) { best = i; best_mv = mv; }
	}
	return best;
}

// Type-C current from the source's Rp, read with the PD PHY's comparators on the active CC
// line (sink thresholds: 0.66 V = 1.5 A, 1.23 V = 3 A). Only used when there is no PD.
static uint16_t rp_level_ma( void )
{
	volatile uint16_t * port = ( s_instance.lastCCLine == eUSBPD_CC2 ) ? &USBPD->PORT_CC2 : &USBPD->PORT_CC1;
	uint16_t save = *port;
	*port = ( save & ~CC_CMP_MASK ) | CC_CMP_123; Delay_Us( 20 );
	bool hi = *port & PA_CC_AI;
	*port = ( save & ~CC_CMP_MASK ) | CC_CMP_66;  Delay_Us( 20 );
	bool mid = *port & PA_CC_AI;
	*port = save;
	return hi ? 3000 : mid ? 1500 : 500;
}

static uint32_t pr_boot_ms;   // when main() started (set once)

// PD event log in RAM that survives a warm reset (debugger attach, watchdog) but not a power
// loss. Attaching `minichlink -T` resets the X035, so this is how the events of a cold start
// are read afterwards: the banner prints the previous boot's log. Outside .bss/.data (those
// are a few hundred bytes at 0x20000000) and below the stack (top 0x20004FF0).
#define EVLOG_MAGIC 0x4E4B5044u   // "NKPD"
#define EVLOG_N     96   // 8 B each: 0x20004000..0x20004307, far below the stack
enum { EV_BOOT = 1, EV_STATE, EV_CAPS, EV_REQ, EV_CONTRACT, EV_REJECT, EV_TIMEOUT, EV_SOFTRST, EV_HARDRST, EV_NOPD, EV_FAILED, EV_RAILS, EV_INPUTS, EV_I2C_ADDR, EV_PICO_RESET, EV_CYCLE, EV_LEVEL };
typedef struct { uint32_t magic, n; struct { uint32_t ms; uint16_t code, val; } e[EVLOG_N]; } evlog_t;
#define EVLOG ( (volatile evlog_t *)0x20004000 )
static void ev( uint16_t code, uint16_t val )
{
	uint32_t i = EVLOG->n % EVLOG_N;
	EVLOG->e[i].ms = millis(); EVLOG->e[i].code = code; EVLOG->e[i].val = val;
	EVLOG->n++;
}
static void ev_print( void )
{
	static const char * names[] = { "?", "boot", "state", "caps", "request", "contract", "reject",
	                                "timeout", "soft-reset", "hard-reset", "no-PD", "PD-failed", "rails", "inputs", "i2c-addr", "pico-reset", "cycle", "target" };
	uint32_t n = EVLOG->n, first = n > EVLOG_N ? n - EVLOG_N : 0;
	LOG( "PD event log (%lu events):", (unsigned long)n );
	for( uint32_t k = first; k < n; k++ )
	{
		uint32_t i = k % EVLOG_N;
		uint16_t c = EVLOG->e[i].code;
		LOG( " %lu:%s=%u", (unsigned long)EVLOG->e[i].ms, names[c < 18 ? c : 0], EVLOG->e[i].val );
	}
	LOG( "\n" );
}

// The IRQ answers every received message with GoodCRC at once. Never start our own message
// while that is still going out: it would cut the GoodCRC off, the source would never see its
// Source_Capabilities acknowledged and would retry, then give up (seen on the bench, v0.1.0).
static uint32_t pd_tx_idle_since;
static bool pd_tx_idle( uint32_t now )
{
	if( USBPD->CONTROL & PD_TX_EN ) { pd_tx_idle_since = now; return false; }
	return now - pd_tx_idle_since >= 2;   // >= 1 ms of quiet; the reply deadline is 24 ms
}

static void pd_request( int i, uint32_t now )
{
	s_instance.state = eSTATE_WAIT_ACCEPT;   // before sending: the Accept may arrive at once
	USBPD_SelectPDO( i, 0 );
	pd.req_idx = i;
	pd.state_t = now;
	const USBPD_SourcePDO_t * p = pdo_at( i );
	ev( EV_REQ, p ? p->FixedSupply.VoltageIn50mV * 50 : 0 );
	LOG( "PD: request PDO %d (%lu mV)\n", i, p ? (unsigned long)p->FixedSupply.VoltageIn50mV * 50 : 0ul );
}

static void pd_restart( void )
{
	USBPD_Reset();
	pd.idx = pd.req_idx = -1;
	pd.caps_seen = 0;
	pd.retries = 0;
	pd.last = eSTATE_IDLE;
	hub.pd_state = PD_NEGOTIATING;
}

static void pd_poll( uint32_t now )
{
	if( s_instance.hardReset )
	{
		// The source drops VBUS after a hard reset, so the board usually loses power here;
		// if it does not (another supply), start over cleanly.
		s_instance.hardReset = false;
		ev( EV_HARDRST, 0 );
		LOG( "PD: hard reset from the source\n" );
		pd_restart();
	}

	USBPD_State_e st = USBPD_GetState();
	if( st != pd.last )
	{
		if( st == eSTATE_CABLE_DETECT && pd.last == eSTATE_IDLE ) pd.attach_t = now;
		LOG( "PD: %s -> %s at %lu ms\n", USBPD_StateToStr( pd.last ), USBPD_StateToStr( st ), (unsigned long)now );
		ev( EV_STATE, st );
		pd.last = st;
		pd.state_t = now;
	}

	// No Source_Capabilities in time -> a 5 V-only source; keep listening anyway. A source that
	// advertises only default USB current (or a USB-A to C cable) sits below the library's
	// 0.66 V attach threshold and never leaves IDLE: that is 500 mA by definition.
	if( hub.pd_state == PD_NEGOTIATING && ( st == eSTATE_IDLE || st == eSTATE_CABLE_DETECT ) &&
	    now - ( st == eSTATE_IDLE ? pr_boot_ms : pd.attach_t ) > NO_PD_AFTER_MS )
	{
		hub.pd_state = PD_NONE;
		pd.rp_ma = st == eSTATE_IDLE ? 500 : rp_level_ma();
		hub.contract_mv = 5000;
		hub.contract_ma = pd.rp_ma;
		LOG( "PD: no USB-PD source, 5 V only, Type-C current %u mA\n", pd.rp_ma );
		ev( EV_NOPD, pd.rp_ma );
	}

	switch( st )
	{
	case eSTATE_IDLE:
		USBPD_SinkNegotiate();   // only detects the source on CC and arms the receiver
		break;

	case eSTATE_CABLE_DETECT:
		if( pd.soft_reset_due && now - pd.attach_t > 50 && pd_tx_idle( now ) )
		{
			pd.soft_reset_due = false;
			LOG( "PD: warm start, soft reset to read the capabilities again\n" );
			ev( EV_SOFTRST, 0 );
			USBPD_SendSoftReset();
		}
		break;

	case eSTATE_SOURCE_CAP:
		if( !pd_tx_idle( now ) ) break;   // our GoodCRC for the caps is still going out
		if( s_instance.rejected )
		{
			s_instance.rejected = false;
			pd.req_idx = -1;
			LOG( "PD: request rejected\n" );
			ev( EV_REJECT, 0 );
			if( pd.idx >= 0 )
			{
				// keep the running contract; aim at it so the policy does not ask again at once
				const USBPD_SourcePDO_t * p = pdo_at( pd.idx );
				if( p ) hub.target_mv = p->FixedSupply.VoltageIn50mV * 50;
				s_instance.state = eSTATE_PS_RDY;
			}
			else pd_request( 0, now );         // 5 V is always offered
		}
		else if( s_instance.capsRx != pd.caps_seen )
		{
			pd.caps_seen = s_instance.capsRx;
			ev( EV_CAPS, s_instance.pdoCount );
			pd_request( pdo_choose( hub.target_mv ), now );
		}
		break;

	case eSTATE_WAIT_ACCEPT:
	case eSTATE_WAIT_PS_RDY:
		if( now - pd.state_t > PD_REPLY_TIMEOUT_MS && pd_tx_idle( now ) )
		{
			pd.req_idx = -1;
			if( ++pd.retries > 3 )
			{
				hub.pd_state = PD_FAILED;
				hub.faults |= F_PD_FAILED;
				LOG( "PD: no answer after 3 retries, giving up (5 V)\n" );
				ev( EV_FAILED, st );
				pd_restart();
				hub.pd_state = PD_FAILED;
			}
			else
			{
				LOG( "PD: no answer, soft reset (retry %u)\n", pd.retries );
				ev( EV_TIMEOUT, st );
				USBPD_SendSoftReset();
			}
			pd.state_t = now;
		}
		break;

	case eSTATE_PS_RDY:
		if( pd.req_idx >= 0 )
		{
			const USBPD_SourcePDO_t * p = pdo_at( pd.req_idx );
			pd.idx = pd.req_idx;
			pd.req_idx = -1;
			pd.retries = 0;
			hub.pd_state = PD_CONTRACT;
			hub.contract_mv = p ? p->FixedSupply.VoltageIn50mV * 50 : 5000;
			uint16_t ma = p ? p->FixedSupply.MaxCurrentIn10mA * 10 : 0;
			hub.contract_ma = ma > LIMIT_IN_MA_MAX ? LIMIT_IN_MA_MAX : ma;
			LOG( "PD: contract %u mV / %u mA\n", hub.contract_mv, hub.contract_ma );
			ev( EV_CONTRACT, hub.contract_mv );
		}
		else if( pd.idx >= 0 && pdo_choose( hub.target_mv ) != pd.idx && pd_tx_idle( now ) )
		{
			pd_request( pdo_choose( hub.target_mv ), now );   // the target changed
		}
		break;

	default:
		break;
	}
}

// Voltage target from a measured input voltage (warm start: keep what the source gives).
static uint16_t mv_to_level( uint16_t mv )
{
	return mv > 17000 ? 20000 : mv > 12000 ? 15000 : mv > 7000 ? 9000 : 5000;
}

// ------------------------------------------------------------------ protection + interlocks
static struct
{
	uint32_t sink_t;            // J2 sink-detect debounce
	bool sink;
	uint32_t pres_t;            // MEZZ_PRESENT debounce
	bool present;
	uint32_t mezz_fault_t;      // last time MEZZ_FAULT was seen asserted
	uint32_t oc_t;              // over-current: when the current state started
	uint8_t oc_level;           // 0 = nothing shed, 1 = J2 shed, 2 = J2 + MEZZ_EN shed
	bool temp_hot, temp_shed, temp_crit;
	uint32_t shed_times[D8_SHEDS];
	uint8_t shed_n;
	bool d8_suspended;
	uint32_t v5_low_t;
	uint32_t buck_on_t;
	bool buck_hold;             // cold start: buck held off during PD negotiation
	uint32_t boot_t;
	bool step_up_pending;       // cold start: 5 V first, then 9 V
	uint32_t contract_t;
} pr;

static void clear_faults( void )
{
	hub.faults = 0;
	pr.d8_suspended = false;
	pr.shed_n = 0;
	hub.inputs &= ~IN_MIN_SUSPENDED;
}

static void temp_shed_event( uint32_t now )
{
	// D8: three temperature sheds within 10 minutes -> MEZZ_EN stays off until cleared
	for( int i = D8_SHEDS - 1; i > 0; i-- ) pr.shed_times[i] = pr.shed_times[i - 1];
	pr.shed_times[0] = now;
	if( pr.shed_n < D8_SHEDS ) pr.shed_n++;
	if( pr.shed_n >= D8_SHEDS && now - pr.shed_times[D8_SHEDS - 1] < D8_WINDOW_MS )
	{
		pr.d8_suspended = true;
		hub.inputs |= IN_MIN_SUSPENDED;
		LOG( "PROT: %d temperature sheds within 10 min, MEZZ_EN suspended until cleared\n", D8_SHEDS );
	}
}

#include "i2c_module.h"

static bool cycling( int rail, uint32_t now ) { return (int32_t)( now - cycle_until[rail] ) < 0; }

static void protect_and_switch( uint32_t now )
{
	// ---- inputs
	bool rd = ( hub.cc1_mv >= CC_RD_MIN_MV && hub.cc1_mv <= CC_RD_MAX_MV ) ||
	          ( hub.cc2_mv >= CC_RD_MIN_MV && hub.cc2_mv <= CC_RD_MAX_MV );
	if( !( hub.rails & RAIL_BUCK ) ) rd = false;   // no +5V, no Rp: nothing to see
	if( rd == pr.sink ) pr.sink_t = now;
	else if( now - pr.sink_t >= ( rd ? SINK_ATTACH_MS : SINK_DETACH_MS ) ) { pr.sink = rd; pr.sink_t = now; }

	bool pres = !funDigitalRead( P_MEZZ_PRESENT );
	if( pres == pr.present ) pr.pres_t = now;
	else if( now - pr.pres_t >= MEZZ_PRESENT_MS ) { pr.present = pres; pr.pres_t = now; }

	bool mfault = !funDigitalRead( P_MEZZ_FAULT );
	if( mfault ) { pr.mezz_fault_t = now; hub.faults |= F_MEZZ; }
	bool mezz_fault_hold = mfault || ( pr.mezz_fault_t && now - pr.mezz_fault_t < MEZZ_FAULT_HOLD_MS );

	hub.inputs = ( hub.inputs & ( IN_MIN_SET | IN_MIN_SUSPENDED ) ) |
	             ( pr.sink ? IN_J2_SINK : 0 ) | ( pr.present ? IN_MEZZ_PRESENT : 0 ) | ( mfault ? IN_MEZZ_FAULT : 0 );

	// ---- source: limited power?
	bool five_only = hub.pd_state == PD_NONE || hub.pd_state == PD_FAILED ||
	                 ( hub.pd_state == PD_CONTRACT && hub.contract_mv <= 5000 );
	uint16_t limit_ma = hub.pd_state == PD_CONTRACT ? hub.contract_ma : pd.rp_ma;
	if( hub.pd_state == PD_NEGOTIATING ) limit_ma = LIMIT_IN_MA_MAX;   // nothing known yet
	if( limit_ma > LIMIT_IN_MA_MAX ) limit_ma = LIMIT_IN_MA_MAX;
	uint8_t n = 0;
	if( five_only && hub.pd_state != PD_NEGOTIATING ) n |= N_5V_ONLY;
	if( hub.pd_state != PD_NEGOTIATING && (uint32_t)hub.contract_mv * limit_ma < 15000000u ) n |= N_WEAK;

	// ---- input over-current: shed J2 first, then MEZZ_EN; the buck and the buses stay on so
	// the brain keeps running and can report it
	if( hub.iin_ma > limit_ma )
	{
		if( now - pr.oc_t >= OC_TRIP_MS && pr.oc_level < 2 )
		{
			pr.oc_level++;
			pr.oc_t = now;
			hub.faults |= F_OVERCURRENT;
			LOG( "PROT: input %u mA > %u mA, shed level %u\n", hub.iin_ma, limit_ma, pr.oc_level );
		}
	}
	else if( pr.oc_level && hub.iin_ma < (uint32_t)limit_ma * OC_RECOVER_PCT / 100 )
	{
		if( now - pr.oc_t >= OC_RECOVER_MS ) { pr.oc_level = 0; pr.oc_t = now; LOG( "PROT: input current back to normal\n" ); }
	}
	else pr.oc_t = now;

	// ---- temperature (never a voltage step-down: under a mezzanine load that raises the
	// current through the hub). NTC open or shorted: report, do not act.
	int16_t t = hub.temp_dc;
	if( t == -999 || t == 9999 ) hub.faults |= F_NTC;
	else
	{
		if( !pr.temp_hot && t > T_HOT_ON ) pr.temp_hot = true;
		if( pr.temp_hot && t < T_HOT_OFF ) pr.temp_hot = false;
		if( !pr.temp_shed && t > T_SHED_ON )
		{
			pr.temp_shed = true; hub.faults |= F_TEMP_SHED;
			LOG( "PROT: %d.%d C, shedding J2 and MEZZ_EN\n", t / 10, t % 10 );
			temp_shed_event( now );
		}
		if( pr.temp_shed && t < T_SHED_OFF ) pr.temp_shed = false;
		if( !pr.temp_crit && t > T_CRIT_ON ) { pr.temp_crit = true; hub.faults |= F_TEMP_CRIT; LOG( "PROT: critical temperature, buck off\n" ); }
		if( pr.temp_crit && t < T_CRIT_OFF ) { pr.temp_crit = false; LOG( "PROT: cooled down, buck on\n" ); }
	}
	if( pr.temp_hot ) n |= N_HOT;

	// ---- buck: off while held at a cold start or at critical temperature
	if( pr.buck_hold && ( hub.pd_state != PD_NEGOTIATING || now - pr.boot_t > BUCK_HOLD_MAX_MS ) )
	{
		pr.buck_hold = false;
		LOG( "buck released after %lu ms\n", (unsigned long)( now - pr.boot_t ) );
	}
	bool buck = !pr.buck_hold && !pr.temp_crit;
	if( buck && !( hub.rails & RAIL_BUCK ) ) pr.buck_on_t = now;
	out_buck( buck );

	// ---- 5 V rail under-voltage (a 5 V-only source puts the buck in dropout on purpose)
	if( buck && !five_only && now - pr.buck_on_t > 1000 && hub.v5_mv < V5_UNDER_MV )
	{
		if( now - pr.v5_low_t > 500 ) hub.faults |= F_V5_UNDER;
	}
	else pr.v5_low_t = now;

	// ---- J2 (USB host port VBUS)
	bool j2_power = !five_only || pd.rp_ma >= 1500 || ( hub.pd_state == PD_CONTRACT && hub.contract_ma >= 1500 );
	bool j2 = hub.wish_j2 != WISH_OFF && pr.sink && buck;
	if( j2 && pr.present ) { j2 = false; n |= N_J2_INTERLOCK; }
	if( j2 && !j2_power )  { j2 = false; n |= N_J2_POWER; }
	if( pr.oc_level >= 1 || pr.temp_shed || cycling( RAILID_J2, now ) ) j2 = false;
	out_j2( j2 );

	// ---- MEZZ_EN (mezzanine 5 V + enable line)
	bool mezz = ( hub.wish_mezz == WISH_ON || ( hub.wish_mezz == WISH_AUTO && pr.present ) ) && buck;
	if( mezz_fault_hold || pr.oc_level >= 2 || pr.temp_shed || pr.d8_suspended || cycling( RAILID_MEZZ, now ) ) mezz = false;
	out_mezz( mezz );

	// ---- bus B: only on the brain's wish. Bus A: never touched here (only a timed CYCLE_RAIL).
	out_bus_b( hub.wish_bus_b != WISH_OFF && !cycling( RAILID_BUS_B, now ) );

	hub.notices = n;

	// log every change of the outputs and the debounced inputs (read later with read_evlog.py)
	static uint8_t last_rails = 0xFF, last_inputs = 0xFF;
	if( hub.rails != last_rails ) { last_rails = hub.rails; ev( EV_RAILS, hub.rails ); }
	if( hub.inputs != last_inputs ) { last_inputs = hub.inputs; ev( EV_INPUTS, hub.inputs ); }
}

// ------------------------------------------------------------------ input voltage policy (D7, D8)
// The board picks its input voltage by demand; the brain may only set a minimum. Over-temperature
// never lowers the voltage (under a mezzanine load that raises the current through the hub); a
// D8 suspension ignores the minimum until the faults are cleared.
static uint16_t demand_mv = 9000;
static uint32_t up_t, down_t;

static uint16_t level_ma( uint16_t mv )   // the source's current at a fixed voltage, capped at 3 A
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t n = USBPD_GetCapabilities( &caps );
	for( size_t i = 0; i < n; i++ )
		if( pdo_fixed( &caps->Source[i] ) && caps->Source[i].FixedSupply.VoltageIn50mV * 50 == mv )
		{
			uint16_t ma = caps->Source[i].FixedSupply.MaxCurrentIn10mA * 10;
			return ma > LIMIT_IN_MA_MAX ? LIMIT_IN_MA_MAX : ma;
		}
	return 0;
}

static uint16_t next_level( uint16_t cur, int dir )   // next fixed PDO voltage up/down, 9..20 V
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t n = USBPD_GetCapabilities( &caps );
	uint16_t best = 0;
	for( size_t i = 0; i < n; i++ )
	{
		if( !pdo_fixed( &caps->Source[i] ) ) continue;
		uint16_t mv = caps->Source[i].FixedSupply.VoltageIn50mV * 50;
		if( mv < 9000 || mv > 20000 ) continue;
		if( dir > 0 && mv > cur && ( !best || mv < best ) ) best = mv;
		if( dir < 0 && mv < cur && mv > best ) best = mv;
	}
	return best;
}

static void voltage_policy( uint32_t now )
{
	if( hub.pd_state == PD_CONTRACT && pd.req_idx < 0 && !pr.step_up_pending )
	{
		uint16_t cur = hub.contract_mv;
		uint16_t up = next_level( cur, +1 ), dn = next_level( cur, -1 );
		bool high = up && hub.iin_ma > (uint32_t)hub.contract_ma * DEMAND_UP_PCT / 100;
		uint32_t p_mw = (uint32_t)hub.vin_mv * hub.iin_ma / 1000;
		bool low = dn && dn < demand_mv && p_mw < (uint32_t)dn * level_ma( dn ) / 1000 * DEMAND_DOWN_PCT / 100;
		if( !high ) up_t = now;
		if( !low ) down_t = now;
		if( high && now - up_t >= DEMAND_UP_MS ) { if( up > demand_mv ) demand_mv = up; up_t = now; LOG( "demand: up to %u mV\n", up ); }
		if( low && now - down_t >= DEMAND_DOWN_MS ) { demand_mv = dn; down_t = now; LOG( "demand: down to %u mV\n", dn ); }
	}
	uint16_t min = pr.d8_suspended ? 0 : hub.min_mv;
	uint16_t t = pr.step_up_pending ? 5000 : ( demand_mv > min ? demand_mv : min );
	if( t != hub.target_mv ) { hub.target_mv = t; ev( EV_LEVEL, t ); }
	if( hub.min_mv ) hub.inputs |= IN_MIN_SET; else hub.inputs &= ~IN_MIN_SET;
	if( min && hub.pd_state != PD_NEGOTIATING && hub.contract_mv < min ) hub.notices |= N_MIN_NOT_MET;
}

// ------------------------------------------------------------------ LED
// fast blink = protection active / fault; solid = negotiating; double blip every 2 s =
// limited power (5 V only); short blip every 4 s = healthy contract.
static void led_update( uint32_t now )
{
	bool prot = pr.oc_level || pr.temp_shed || pr.temp_crit || pr.d8_suspended ||
	            ( hub.inputs & IN_MEZZ_FAULT ) || hub.pd_state == PD_FAILED;
	if( prot ) led( ( now / 125 ) & 1 );
	else if( hub.pd_state == PD_NEGOTIATING ) led( true );
	else if( hub.notices & N_5V_ONLY ) { uint32_t p = now % 2000; led( p < 60 || ( p > 200 && p < 260 ) ); }
	else led( now % 4000 < 40 );
}

// ------------------------------------------------------------------ watchdog
static void iwdg_start( void )   // ~1 s: HSI/1024 = 46.9 kHz, /32, reload 1465
{
	IWDG->CTLR = 0x5555; IWDG->PSCR = 3;
	IWDG->CTLR = 0x5555; IWDG->RLDR = 1465;
	IWDG->CTLR = 0xAAAA;
	IWDG->CTLR = 0xCCCC;
}
static void iwdg_feed( void ) { IWDG->CTLR = 0xAAAA; }

// ------------------------------------------------------------------ debug link
static volatile int cmd;
void handle_debug_input( int numbytes, uint8_t * data )
{
	if( numbytes > 0 && data[0] > ' ' ) cmd = data[0];
}

static void print_status( void )
{
	static const char * pdn[] = { "no PD", "negotiating", "contract", "PD failed" };
	int t = hub.temp_dc;
	LOG( "%s %u mV/%u mA (target %u min %u) | V_IN %u V_5V %u I_IN %u mA | T %d.%d C | CC %u/%u | "
	     "rails %02x in %02x faults %02x notices %02x | i2c %s 0x%02x\n",
	     pdn[hub.pd_state & 3], hub.contract_mv, hub.contract_ma, hub.target_mv, hub.min_mv,
	     hub.vin_mv, hub.v5_mv, hub.iin_ma, t / 10, ( t < 0 ? -t : t ) % 10,
	     hub.cc1_mv, hub.cc2_mv, hub.rails, hub.inputs, hub.faults, hub.notices,
	     i2c_state == I2C_ASSIGNED ? "addr" : "enum", i2c_state == I2C_ASSIGNED ? i2c_addr : ENUM_ADDR );
}

// Key 'p': which CC comparator thresholds each J1 CC line is above (sink side, external Rd).
// Default USB Rp ~0.4 V, 1.5 A ~0.9 V, 3 A ~1.7 V; nothing = no source on that line.
static void print_cc( void )
{
	static const uint16_t thr[] = { CC_CMP_22, CC_CMP_45, CC_CMP_55, CC_CMP_66, CC_CMP_95, CC_CMP_123 };
	static const char * name[] = { "0.22", "0.45", "0.55", "0.66", "0.95", "1.23" };
	uint32_t cfg = USBPD->CONFIG;
	for( int line = 0; line < 2; line++ )
	{
		volatile uint16_t * port = line ? &USBPD->PORT_CC2 : &USBPD->PORT_CC1;
		if( line ) USBPD->CONFIG |= CC_SEL; else USBPD->CONFIG &= ~CC_SEL;
		uint16_t save = *port;
		LOG( "CC%d (PORT %04x) above:", line + 1, save );
		for( int i = 0; i < 6; i++ )
		{
			*port = ( save & ~CC_CMP_MASK ) | thr[i]; Delay_Us( 10 );
			if( *port & PA_CC_AI ) LOG( " %sV", name[i] );
		}
		*port = save;
		LOG( "\n" );
	}
	USBPD->CONFIG = cfg;
	LOG( "PD library state %d, caps received %lu, IRQ rx %lu sop0 %lu tx %lu, CONFIG %04lx CONTROL %02x STATUS %02x\n",
	     (int)USBPD_GetState(), (unsigned long)s_instance.capsRx, (unsigned long)s_instance.irqRx,
	     (unsigned long)s_instance.irqSop0, (unsigned long)s_instance.irqTx, (unsigned long)USBPD->CONFIG,
	     USBPD->CONTROL, USBPD->STATUS );
}

static void help( void )
{
	LOG( "keys: s status  p J1 CC levels  v minimum voltage 0/15/20  e enumerate again  c clear faults  t +5 C  T temp offset off\n"
	     "      w stop feeding the watchdog  h help\n"
	     "rails: 01 bus A 02 bus B 04 J2 08 MEZZ_EN 10 buck | in: 01 J2 sink 02 mezz present 04 mezz fault\n"
	     "faults: 01 over-current 02 temp shed 04 temp crit 08 mezz 10 5V low 20 PD failed 40 NTC\n"
	     "notices: 01 5V only 02 weak source 04 hot 08 J2 interlock 10 J2 power\n" );
}

// ------------------------------------------------------------------ main
int main( void )
{
	SystemInit();
	uint32_t flags = RCC->RSTSCKR;
	RCC->RSTSCKR |= RST_RMVF;
	bool cold = flags & RST_PORF;   // check POWER-ON first: the factory bootloader always adds SOFTWARE

	funGpioInitAll();
	if( cold ) line_drive_low( P_EN_BUCK );   // hold the 5 V rail through PD negotiation (C35 gives ~38 ms)
	funDigitalWrite( P_LED, FUN_HIGH );
	funPinMode( P_LED, GPIO_CFGLR_OUT_10Mhz_PP );
	adc_init();

	hub.reset_cause = cold ? RC_POWER_ON : ( flags & RST_IWDGF ) ? RC_WATCHDOG : ( flags & RST_SFTF ) ? RC_SOFTWARE : RC_OTHER;
	hub.rails = RAIL_BUS_A | RAIL_BUS_B | ( cold ? 0 : RAIL_BUCK );   // matches the lines as they are now
	hub.wish_bus_b = WISH_ON;
	hub.wish_j2 = WISH_AUTO;
	hub.wish_mezz = WISH_AUTO;
	hub.pd_state = PD_NEGOTIATING;
	hub.contract_mv = 5000;

	for( int i = 0; i < 6; i++ ) adc_step();   // first readings
	pr.boot_t = pr_boot_ms = millis();
	pr.buck_hold = cold;
	pr.step_up_pending = cold;
	pr.buck_on_t = pr.boot_t;
	pd.idx = pd.req_idx = -1;
	pd.rp_ma = 500;
	if( cold ) hub.target_mv = 5000;   // 5 V first, 9 V once the rails are up
	else
	{
		hub.target_mv = mv_to_level( hub.vin_mv );   // keep what the source gives now
		pd.soft_reset_due = true;
	}

	if( EVLOG->magic != EVLOG_MAGIC ) { EVLOG->magic = EVLOG_MAGIC; EVLOG->n = 0; }
	ev_print();   // the previous boot(s), e.g. a cold start before the debugger attached
	ev( EV_BOOT, flags >> 24 );

	USBPD_Init( eUSBPD_VCC_3V3 );
	USBPD_Reset();
	demand_mv = cold ? 9000 : ( mv_to_level( hub.vin_mv ) > 9000 ? mv_to_level( hub.vin_mv ) : 9000 );
	i2c_module_start( cold, millis() );
	iwdg_start();

	LOG( "\nnoknok PicoHub MOD-0011 power manager v%d.%d.%d  reset 0x%08lx (%s)\n",
	     FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH, (unsigned long)flags,
	     cold ? "power-on" : hub.reset_cause == RC_WATCHDOG ? "watchdog" : hub.reset_cause == RC_SOFTWARE ? "software" : "other" );
	help();

	uint32_t last_ms = millis(), last_policy = 0, last_print = 0;
	bool hang = false;
	while( 1 )
	{
		uint32_t now = millis();
		pd_poll( now );
		i2c_module_step( now );

		// the cold-start step 5 V -> 9 V, once the contract stands and the buck is running
		if( pr.step_up_pending && hub.pd_state != PD_NEGOTIATING && ( hub.rails & RAIL_BUCK ) )
		{
			if( !pr.contract_t ) pr.contract_t = now;
			if( now - pr.contract_t > STEP_UP_DELAY_MS ) pr.step_up_pending = false;   // voltage_policy takes over
		}

		// MEZZ_FAULT acts at once, not only on the 5 ms policy tick
		if( ( hub.rails & RAIL_MEZZ ) && !funDigitalRead( P_MEZZ_FAULT ) ) out_mezz( false );

		if( now != last_ms )
		{
			last_ms = now;
			adc_step();
			if( now - last_policy >= 5 ) { last_policy = now; protect_and_switch( now ); voltage_policy( now ); led_update( now ); }
		}

		if( DidDebuggerAttach() )
		{
			poll_input();
			int c = cmd; cmd = 0;
			switch( c )
			{
			case 's': print_status(); break;
			case 'p': print_cc(); break;
			case 'v': hub.min_mv = hub.min_mv >= 20000 ? 0 : hub.min_mv >= 15000 ? 20000 : 15000;   // as SET_MIN_INPUT_VOLTAGE
			          LOG( "minimum input voltage %u mV\n", hub.min_mv ); break;
			case 'c': clear_faults(); LOG( "faults cleared\n" ); break;
			case 'e': i2c_off(); KEEP->magic = 0; i2c_state = I2C_BOOT_WAITING;   // bench: enumerate again
			          backoff_until = now + ( fnv_hash( 2166136261UL ) % 2500 ) + 300;
			          LOG( "I2C: address forgotten, enumerating again\n" ); break;
			case 't': temp_offset_dc += 50; LOG( "temperature offset +%d.%d C\n", temp_offset_dc / 10, temp_offset_dc % 10 ); break;
			case 'T': temp_offset_dc = 0; LOG( "temperature offset off\n" ); break;
			case 'w': hang = true; LOG( "watchdog no longer fed: reset in ~1 s\n" ); break;
			case 'h': help(); break;
			default: break;
			}
			if( now - last_print >= 2000 ) { last_print = now; print_status(); }
		}

		if( !hang ) iwdg_feed();
	}
}
