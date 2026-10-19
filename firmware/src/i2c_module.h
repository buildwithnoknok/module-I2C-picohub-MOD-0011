// SPDX-License-Identifier: MIT
// Copyright (c) 2026 noknok
/*
 * noknok PicoHub (MOD-0011) power manager - the noknok I2C module interface (DEV-122, ADR-004).
 * Included once by picohub.c (one translation unit; uses its `hub`, `pr` and policy state).
 *
 * The X035 is an ordinary noknok I2C module on bus A (PC16 = SCL, PC17 = SDA, I2C1 remap):
 *   - Enumeration per Ecosystem software/enumeration.md: after a UID-seeded FNV-1a backoff the
 *     module listens at 0x7F, answers 10 bytes [UID 8, type 0x07, CRC8] and takes the address the
 *     Conductor assigns with [0x1D, addr]. Not assigned within 200 ms -> short re-backoff.
 *   - The assigned address and the brain's wishes are kept in a RAM cell over a warm reset of the
 *     X035 (watchdog, debugger), so the brain never loses the power manager; a power-on clears it.
 *   - Command map (README.md of this repo): 0xB1 GET_VERSION, 0xB0 (ignored until DEV-124),
 *     0x10 GET_STATUS, 0x11 GET_SOURCE_CAPS, 0x12 CLEAR_FAULTS, 0x20 SET_RAIL, 0x21 CYCLE_RAIL,
 *     0x22 RESET_PICO, 0x23 SET_MIN_INPUT_VOLTAGE.
 * Every command is decoded in the I2C ISR at STOP; anything that switches hardware is handed to
 * the main loop, which applies the interlocks (the brain sets wishes, the board decides).
 */

#define MODULE_TYPE      0x07   // picohub (Ecosystem enumeration.md type table)
#define PROTOCOL_VERSION 1      // first byte of the 0xB1 reply, as on every noknok module
#define ENUM_ADDR        0x7F
#define REG_ASSIGN_ADDR  0x1D
#define CMD_ENTER_BOOTLOADER 0xB0
#define CMD_GET_VERSION  0xB1
#define CMD_GET_STATUS   0x10
#define CMD_GET_CAPS     0x11
#define CMD_CLEAR_FAULTS 0x12
#define CMD_SET_RAIL     0x20
#define CMD_CYCLE_RAIL   0x21
#define CMD_RESET_PICO   0x22
#define CMD_SET_MIN_VIN  0x23
#define KEY_BYTE         0x5A   // RESET_PICO and CYCLE_RAIL: a bus glitch must never trigger them
#define STATUS_FORMAT    1

enum { RES_APPLIED = 0, RES_INTERLOCK = 1, RES_FAULT_POWER = 2, RES_INVALID = 3 };
enum { RAILID_BUS_A = 0, RAILID_BUS_B = 1, RAILID_J2 = 2, RAILID_MEZZ = 3 };

#define UID_ADDR ( (volatile uint8_t *)0x1FFFF7E8 )   // ESIG UID0 + UID1, 8 bytes (as on the V003)
#define UID_LEN  8

// Kept over a warm reset (outside .data/.bss, below the stack; the event log ends at 0x20004307)
#define KEEP_MAGIC 0x4E4B4B31u   // "NKK1"
typedef struct { uint32_t magic; uint8_t addr, wish_bus_b, wish_j2, wish_mezz; uint16_t min_mv, check; } keep_t;
#define KEEP ( (volatile keep_t *)0x20004400 )

enum { I2C_BOOT_WAITING, I2C_ENUM_READY, I2C_ASSIGNING, I2C_ASSIGNED };
enum { RD_RESULT, RD_VERSION, RD_STATUS, RD_CAPS };

static volatile uint8_t i2c_state = I2C_BOOT_WAITING;
static volatile uint8_t i2c_addr, i2c_new_addr;
static volatile uint8_t rx_buf[8], rx_len;
static volatile uint8_t tx_buf[32], tx_len, tx_idx;
static volatile uint8_t read_kind = RD_RESULT;
static volatile uint8_t last_result = 0xFF;
static uint32_t backoff_until, enum_ready_t;

// actions handed from the ISR to the main loop
static volatile bool act_clear;
static volatile int8_t act_cycle_rail = -1;
static volatile uint16_t act_cycle_ms;
static volatile bool act_reset_pico;
static volatile uint16_t act_reset_ms;

static uint8_t crc8( const volatile uint8_t * d, uint8_t len )   // polynomial 0x07, init 0
{
	uint8_t crc = 0;
	for( uint8_t i = 0; i < len; i++ )
	{
		crc ^= d[i];
		for( uint8_t j = 0; j < 8; j++ ) crc = ( crc & 0x80 ) ? ( crc << 1 ) ^ 0x07 : ( crc << 1 );
	}
	return crc;
}

// FNV-1a over the 8 UID bytes. The loop starts at the offset basis (no XOR with a seed first),
// or chips from one batch collapse to the same backoff (lesson from the V003 modules).
static uint32_t fnv_hash( uint32_t h )
{
	for( uint8_t i = 0; i < UID_LEN; i++ ) { h ^= UID_ADDR[i]; h *= 16777619UL; }
	return h;
}

// ------------------------------------------------------------------ replies (built in the ISR)
static void put16( uint8_t i, uint16_t v ) { tx_buf[i] = v & 0xFF; tx_buf[i + 1] = v >> 8; }

static void build_uid( void )
{
	for( uint8_t i = 0; i < UID_LEN; i++ ) tx_buf[i] = UID_ADDR[i];
	tx_buf[8] = MODULE_TYPE;
	tx_buf[9] = crc8( tx_buf, 9 );
	tx_len = 10;
}

static void build_version( void )   // 4 bytes, the noknok app standard
{
	tx_buf[0] = PROTOCOL_VERSION;
	tx_buf[1] = FW_VERSION_MAJOR; tx_buf[2] = FW_VERSION_MINOR; tx_buf[3] = FW_VERSION_PATCH;
	tx_len = 4;
}

static void build_status( void )    // 20 bytes, little-endian, last byte CRC8
{
	tx_buf[0] = STATUS_FORMAT;
	tx_buf[1] = hub.pd_state;
	put16( 2, hub.contract_mv );
	put16( 4, hub.contract_ma );
	put16( 6, hub.vin_mv );
	put16( 8, hub.iin_ma );
	put16( 10, hub.v5_mv );
	put16( 12, (uint16_t)hub.temp_dc );
	tx_buf[14] = hub.rails;
	tx_buf[15] = hub.inputs;
	tx_buf[16] = hub.faults;
	tx_buf[17] = hub.notices;
	tx_buf[18] = hub.reset_cause;
	tx_buf[19] = crc8( tx_buf, 19 );
	tx_len = 20;
}

static void build_caps( void )      // [n] + n x {mV u16, mA u16} (fixed PDOs only) + CRC8
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t count = USBPD_GetCapabilities( &caps );
	uint8_t n = 0;
	for( size_t i = 0; i < count && n < 7; i++ )
	{
		const USBPD_SourcePDO_t * p = &caps->Source[i];
		if( p->Header.PDOType != eUSBPD_PDO_FIXED ) continue;
		put16( 1 + n * 4, p->FixedSupply.VoltageIn50mV * 50 );
		put16( 3 + n * 4, p->FixedSupply.MaxCurrentIn10mA * 10 );
		n++;
	}
	tx_buf[0] = n;
	tx_buf[1 + n * 4] = crc8( tx_buf, 1 + n * 4 );
	tx_len = 2 + n * 4;
}

static bool pdo_offers( uint16_t mv )   // is there a fixed PDO at or above mv?
{
	USBPD_SPR_CapabilitiesMessage_t * caps;
	size_t count = USBPD_GetCapabilities( &caps );
	for( size_t i = 0; i < count; i++ )
		if( caps->Source[i].Header.PDOType == eUSBPD_PDO_FIXED && caps->Source[i].FixedSupply.VoltageIn50mV * 50 >= mv )
			return true;
	return false;
}

// ------------------------------------------------------------------ command decode (ISR, at STOP)
static void decode_command( void )
{
	uint8_t c = rx_buf[0];
	read_kind = RD_RESULT;
	switch( c )
	{
	case CMD_GET_VERSION: read_kind = RD_VERSION; return;
	case CMD_GET_STATUS:  read_kind = RD_STATUS; return;
	case CMD_GET_CAPS:    read_kind = RD_CAPS; return;
	case CMD_ENTER_BOOTLOADER: last_result = RES_INVALID; return;   // no bootloader yet (DEV-124)
	case CMD_CLEAR_FAULTS: act_clear = true; last_result = RES_APPLIED; return;

	case CMD_SET_RAIL:
	{
		uint8_t rail = rx_buf[1], want = rx_buf[2];
		if( rx_len != 3 || want > WISH_AUTO || rail < RAILID_BUS_B || rail > RAILID_MEZZ ) { last_result = RES_INVALID; return; }
		if( rail == RAILID_BUS_B ) { hub.wish_bus_b = want; last_result = RES_APPLIED; return; }
		if( rail == RAILID_J2 )
		{
			hub.wish_j2 = want;
			// auto = "on when a device comes": no device is not a refusal. "on" without a device,
			// or a mezzanine on the USB uplink, is held off by the interlock.
			if( want == WISH_OFF ) last_result = RES_APPLIED;
			else if( pr.present || ( want == WISH_ON && !pr.sink ) ) last_result = RES_INTERLOCK;
			else if( !pr.sink ) last_result = RES_APPLIED;
			else if( hub.notices & N_J2_POWER || pr.oc_level || pr.temp_shed ) last_result = RES_FAULT_POWER;
			else last_result = RES_APPLIED;
			return;
		}
		hub.wish_mezz = want;   // RAILID_MEZZ
		if( want == WISH_ON && ( ( hub.inputs & IN_MEZZ_FAULT ) || pr.oc_level >= 2 || pr.temp_shed || pr.d8_suspended ) )
			last_result = RES_FAULT_POWER;
		else last_result = RES_APPLIED;
		return;
	}

	case CMD_CYCLE_RAIL:
	{
		uint8_t rail = rx_buf[1];
		uint16_t ms = rx_buf[2] | ( rx_buf[3] << 8 );
		if( rx_len != 5 || rx_buf[4] != KEY_BYTE || rail > RAILID_MEZZ ) { last_result = RES_INVALID; return; }
		uint16_t max = rail == RAILID_BUS_A ? 2000 : 5000;
		act_cycle_ms = ms < 50 ? 50 : ms > max ? max : ms;
		act_cycle_rail = rail;
		last_result = RES_APPLIED;
		return;
	}

	case CMD_RESET_PICO:
	{
		uint16_t ms = rx_buf[1] | ( rx_buf[2] << 8 );
		if( rx_len != 4 || rx_buf[3] != KEY_BYTE ) { last_result = RES_INVALID; return; }
		act_reset_ms = ms < 10 ? 10 : ms > 1000 ? 1000 : ms;
		act_reset_pico = true;
		last_result = RES_APPLIED;
		return;
	}

	case CMD_SET_MIN_VIN:
	{
		uint8_t v = rx_buf[1];
		if( rx_len != 2 || ( v != 0 && v != 15 && v != 20 ) ) { last_result = RES_INVALID; return; }
		hub.min_mv = v * 1000;
		last_result = ( v && hub.pd_state != PD_NEGOTIATING && !pdo_offers( hub.min_mv ) ) ? RES_FAULT_POWER : RES_APPLIED;
		return;
	}

	default:
		last_result = 0xFF;
		return;
	}
}

// ------------------------------------------------------------------ I2C1 slave on PC16/PC17
static void i2c_slave_init( uint8_t addr )
{
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;
	RCC->APB1PCENR |= RCC_APB1Periph_I2C1;
	// I2C1 remap 0b010: SCL = PC16, SDA = PC17 (the QFN-28 has no PA10/PA11)
	AFIO->PCFR1 = ( AFIO->PCFR1 & ~AFIO_PCFR1_I2C1_REMAP ) | AFIO_PCFR1_I2C1_REMAP_1;
	// PC16/PC17 live in CFGXR (pins 16..23); ch32fun's funPinMode only covers 0..15. AF open-drain.
	GPIOC->CFGXR = ( GPIOC->CFGXR & ~0xFFu ) | 0xFFu;

	I2C1->CTLR1 |= I2C_CTLR1_SWRST;
	I2C1->CTLR1 &= ~I2C_CTLR1_SWRST;
	I2C1->CTLR2 = 48;                    // peripheral clock 48 MHz
	I2C1->CKCFGR = 240;                  // 100 kHz (slave: only matters for timing filters)
	I2C1->OADDR1 = (uint16_t)addr << 1;
	I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN | I2C_CTLR2_ITERREN;
	I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;
	NVIC_EnableIRQ( I2C1_EV_IRQn );
	NVIC_EnableIRQ( I2C1_ER_IRQn );
	i2c_addr = addr;
}

static void i2c_off( void ) { I2C1->CTLR1 &= ~I2C_CTLR1_PE; }

void I2C1_EV_IRQHandler( void ) __attribute__( ( interrupt ) );
void I2C1_EV_IRQHandler( void )
{
	uint32_t star1 = I2C1->STAR1;
	uint32_t star2 = I2C1->STAR2;   // reading STAR2 clears ADDR

	if( star1 & I2C_STAR1_ADDR )
	{
		rx_len = 0;
		I2C1->CTLR1 |= I2C_CTLR1_ACK;
		if( star2 & I2C_STAR2_TRA )   // the master reads
		{
			tx_idx = 0;
			if( i2c_state == I2C_ENUM_READY ) build_uid();
			else switch( read_kind )
			{
			case RD_VERSION: build_version(); break;
			case RD_STATUS:  build_status(); break;
			case RD_CAPS:    build_caps(); break;
			default:         tx_buf[0] = last_result; tx_len = 1; break;
			}
			read_kind = RD_RESULT;
			I2C1->DATAR = tx_buf[tx_idx++];
		}
		return;
	}
	if( star1 & I2C_STAR1_RXNE )
	{
		uint8_t b = (uint8_t)I2C1->DATAR;
		if( rx_len < sizeof( rx_buf ) ) rx_buf[rx_len++] = b;
		return;
	}
	if( star1 & I2C_STAR1_TXE )
	{
		I2C1->DATAR = ( tx_idx < tx_len ) ? tx_buf[tx_idx++] : 0xFF;
		return;
	}
	if( star1 & I2C_STAR1_STOPF )
	{
		I2C1->CTLR1 |= I2C_CTLR1_PE;   // clears STOPF
		if( i2c_state == I2C_ENUM_READY )
		{
			if( rx_len == 2 && rx_buf[0] == REG_ASSIGN_ADDR && rx_buf[1] >= 0x08 && rx_buf[1] <= 0x77 )
			{
				i2c_new_addr = rx_buf[1];
				i2c_state = I2C_ASSIGNING;   // the address switch happens in the main loop
			}
		}
		else if( i2c_state == I2C_ASSIGNED && rx_len > 0 ) decode_command();
		I2C1->CTLR1 |= I2C_CTLR1_ACK;
		return;
	}
}

void I2C1_ER_IRQHandler( void ) __attribute__( ( interrupt ) );
void I2C1_ER_IRQHandler( void )
{
	I2C1->STAR1 &= ~( I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_AF | I2C_STAR1_OVR );
	I2C1->CTLR1 |= I2C_CTLR1_ACK;
}

// ------------------------------------------------------------------ main-loop side
static uint32_t cycle_until[4];          // a rail is held off until this time (CYCLE_RAIL)
static uint32_t cycle_start, reset_start;
static bool cycle_running, reset_running, bus_a_restoring;
static uint8_t cycle_rail;
static uint16_t cycle_ms;

static void keep_save( void )
{
	KEEP->addr = i2c_state == I2C_ASSIGNED ? i2c_addr : 0;
	KEEP->wish_bus_b = hub.wish_bus_b; KEEP->wish_j2 = hub.wish_j2; KEEP->wish_mezz = hub.wish_mezz;
	KEEP->min_mv = hub.min_mv;
	KEEP->check = KEEP->addr ^ ( KEEP->wish_bus_b << 2 ) ^ ( KEEP->wish_j2 << 4 ) ^ ( KEEP->wish_mezz << 6 ) ^ KEEP->min_mv ^ 0xA55A;
	KEEP->magic = KEEP_MAGIC;
}

static bool keep_valid( void )
{
	return KEEP->magic == KEEP_MAGIC &&
	       KEEP->check == ( KEEP->addr ^ ( KEEP->wish_bus_b << 2 ) ^ ( KEEP->wish_j2 << 4 ) ^ ( KEEP->wish_mezz << 6 ) ^ KEEP->min_mv ^ 0xA55A ) &&
	       KEEP->wish_bus_b <= WISH_AUTO && KEEP->wish_j2 <= WISH_AUTO && KEEP->wish_mezz <= WISH_AUTO &&
	       ( KEEP->min_mv == 0 || KEEP->min_mv == 15000 || KEEP->min_mv == 20000 );
}

// Called once from main(). Cold start: enumerate. Warm start with a valid cell: go straight back
// to the assigned address and the brain's wishes.
static void i2c_module_start( bool cold, uint32_t now )
{
	if( !cold && keep_valid() )
	{
		hub.wish_bus_b = KEEP->wish_bus_b; hub.wish_j2 = KEEP->wish_j2; hub.wish_mezz = KEEP->wish_mezz;
		hub.min_mv = KEEP->min_mv;
		if( KEEP->addr >= 0x08 && KEEP->addr <= 0x77 )
		{
			i2c_slave_init( KEEP->addr );
			i2c_state = I2C_ASSIGNED;
			LOG( "I2C: warm start, back at address 0x%02x\n", KEEP->addr );
			return;
		}
	}
	KEEP->magic = 0;
	backoff_until = now + ( fnv_hash( 2166136261UL ) % 2500 ) + 300;   // 300..2799 ms
	i2c_state = I2C_BOOT_WAITING;
}

static void i2c_module_step( uint32_t now )
{
	// ---- enumeration
	if( i2c_state == I2C_BOOT_WAITING && (int32_t)( now - backoff_until ) >= 0 )
	{
		i2c_slave_init( ENUM_ADDR );
		i2c_state = I2C_ENUM_READY;
		enum_ready_t = now;
	}
	if( i2c_state == I2C_ENUM_READY && now - enum_ready_t > 200 )
	{
		i2c_off();   // not assigned: probably a collision, short re-backoff
		backoff_until = now + ( fnv_hash( now ) % 500 ) + 50;
		i2c_state = I2C_BOOT_WAITING;
	}
	if( i2c_state == I2C_ASSIGNING )
	{
		i2c_off();
		I2C1->OADDR1 = (uint16_t)i2c_new_addr << 1;
		I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;
		i2c_addr = i2c_new_addr;
		i2c_state = I2C_ASSIGNED;
		ev( EV_I2C_ADDR, i2c_addr );
		LOG( "I2C: assigned address 0x%02x\n", i2c_addr );
	}

	// ---- actions from commands
	if( act_clear ) { act_clear = false; clear_faults(); }

	if( act_reset_pico && !reset_running ) { act_reset_pico = false; reset_running = true; reset_start = now + 50; }
	if( reset_running && (int32_t)( now - reset_start ) >= 0 )
	{
		if( now - reset_start < act_reset_ms ) line_drive_low( P_PICO_RUN );
		else { line_release( P_PICO_RUN ); reset_running = false; ev( EV_PICO_RESET, act_reset_ms ); }
	}

	if( act_cycle_rail >= 0 && !cycle_running )
	{
		cycle_rail = act_cycle_rail; cycle_ms = act_cycle_ms; act_cycle_rail = -1;
		cycle_running = true; cycle_start = now + 20;   // let the I2C transaction finish first
	}
	if( cycle_running && (int32_t)( now - cycle_start ) >= 0 )
	{
		if( cycle_rail == RAILID_BUS_A )
		{
			// Bus A carries our own link: the switch-off is always timed and restores itself.
			if( now - cycle_start < cycle_ms )
			{
				if( hub.rails & RAIL_BUS_A ) { i2c_off(); line_drive_low( P_EN_A ); hub.rails &= ~RAIL_BUS_A; }
			}
			else if( !( hub.rails & RAIL_BUS_A ) && !bus_a_restoring )
			{
				line_release( P_EN_A ); hub.rails |= RAIL_BUS_A; bus_a_restoring = true;
			}
			else if( bus_a_restoring && now - cycle_start >= (uint32_t)cycle_ms + 10 )
			{
				i2c_slave_init( i2c_addr );   // the peripheral saw the bus collapse: start it clean
				bus_a_restoring = false; cycle_running = false;
				ev( EV_CYCLE, ( RAILID_BUS_A << 13 ) | cycle_ms );
			}
		}
		else
		{
			cycle_until[cycle_rail] = cycle_start + cycle_ms;
			cycle_running = false;
			ev( EV_CYCLE, ( cycle_rail << 13 ) | cycle_ms );
		}
	}

	// ---- the brain's wishes and minimum survive a warm reset
	keep_save();
}
