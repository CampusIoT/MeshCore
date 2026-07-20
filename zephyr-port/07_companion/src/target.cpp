#include "target.h"
#include "zephyr_radiolib_hal.h"
/* Chip quirks (header-CRC standby, RX max-length re-assert), the multi-SF side-detector
 * machinery, and the mesh wrapper (standby-before-re-arm, TX-SF policy, per-link ADR hooks)
 * are shared with the bare-metal variant via CustomLR2021 / CustomLR2021Wrapper -- single
 * source for the LR2021 code. Multi-SF knobs and defaults are documented in CustomLR2021.h. */
#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>   /* k_msleep (used by the multi-SF A/B test) */

static ZephyrHal hal;

static Module s_mod(&hal, LR_PIN_NSS, LR_PIN_DIO1, LR_PIN_RESET, LR_PIN_BUSY);
static CustomLR2021 s_lora(&s_mod);

ZBoard board;   /* defined before the radio wrapper that references it */

static CustomLR2021Wrapper s_radio(s_lora, board);

RadioLibWrapper &radio_driver = s_radio;
VolatileRTCClock rtc_clock;
SensorManager    sensors;

mesh::LocalIdentity radio_new_identity()
{
	RadioNoiseListener rng(s_lora);
	return mesh::LocalIdentity(&rng);   /* new identity from LoRa RSSI noise */
}

void ZBoard::reboot() { sys_reboot(SYS_REBOOT_COLD); }

#define MC_HF_CUTOFF_MHZ  1500.0f

static float  s_freq    = LORA_FREQ;
static int8_t s_req_dbm = LORA_TX_POWER;   /* last app-requested power, pre-clamp */

static int8_t clamp_tx_for_band(float freq, int8_t dbm)
{
	if (freq > MC_HF_CUTOFF_MHZ)                       /* 2.4 GHz HF PA: -19..+12 */
		return dbm > 12 ? 12 : (dbm < -19 ? -19 : dbm);
	return dbm > 22 ? 22 : (dbm < -9 ? -9 : dbm);     /* sub-GHz LF PA: -9..+22 */
}


static bool radio_bringup(float freq, float bw, uint8_t sf, uint8_t cr)
{
	for (int attempt = 0; attempt < 8; attempt++) {

		int16_t st = s_lora.begin(freq, bw, sf, cr,
					  RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE,
					  clamp_tx_for_band(freq, s_req_dbm), 16, /*tcxoVoltage=*/0.0f);
		if (st == RADIOLIB_ERR_NONE) {
			/* match CustomLR2021's proven config: explicit header, CRC, RX boosted gain */
			s_lora.explicitHeader();
			s_lora.setCRC(2);
			s_lora.setRxBoostedGainMode(LR2021_RX_BOOST_LEVEL);
			s_lora.applyMultiSF();                   /* arm side detectors (no-op unless -DLR2021_MULTISF) */
			int16_t rx = s_lora.startReceive();      /* the operation that fails on a bad boot */
			if (rx == RADIOLIB_ERR_NONE) {
				s_lora.standby();                /* idle; the wrapper arms RX in its loop */
				s_freq = freq;
				if (attempt) printk("radio: RX ok after %d retr%s\n",
						    attempt, attempt == 1 ? "y" : "ies");
				return true;
			}
			printk("radio: begin ok but RX arm failed (%d), resetting\n", rx);
		} else {
			printk("radio: begin failed (%d), resetting\n", st);
		}
		s_lora.reset();        /* full chip reset, then retry the whole bring-up */
		k_msleep(50);
	}
	return false;
}

bool radio_init()
{
	s_lora.setIrqDio(LR2021_IRQ_DIO);
	s_freq = LORA_FREQ;
	s_req_dbm = LORA_TX_POWER;
	if (!radio_bringup(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR)) return false;
	s_radio.begin();
	return true;
}

uint32_t radio_get_rng_seed() { return s_lora.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr)
{

	bool band_changed = (freq > MC_HF_CUTOFF_MHZ) != (s_freq > MC_HF_CUTOFF_MHZ);
	s_lora.standby();
	int16_t st = s_lora.setFrequency(freq);     /* recalibrates the front-end for the band */
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setBandwidth(bw);
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setSpreadingFactor(sf);
	if (st == RADIOLIB_ERR_NONE) st = s_lora.setCodingRate(cr);
	if (st == RADIOLIB_ERR_NONE) s_lora.applyMultiSF();  /* re-arm side detectors for the new floor SF/BW */
	s_freq = freq;
	if (band_changed) s_lora.setOutputPower(clamp_tx_for_band(freq, s_req_dbm));
	s_radio.begin();   /* reset wrapper -> dispatcher re-arms RX (startReceive) on the new config */
	if (st != RADIOLIB_ERR_NONE) {
		printk("radio_set_params: apply error %d for %u kHz bw=%u kHz sf%u cr%u\n",
		       st, (unsigned)(freq * 1000.0f), (unsigned)(bw * 1000.0f), sf, cr);
	} else {
		printk("radio_set_params: now on %u kHz bw=%u kHz sf%u cr%u\n",
		       (unsigned)(freq * 1000.0f), (unsigned)(bw * 1000.0f), sf, cr);
	}
}

void radio_set_tx_power(int8_t dbm)
{
	s_req_dbm = dbm;
	s_lora.setOutputPower(clamp_tx_for_band(s_freq, dbm));
}

/* TX-SF policy hook for multi-SF meshes: subsequent packets go out at `sf` (should be > the RX
 * floor SF so peers' side detectors catch them) while this node keeps receiving on the floor.
 * Pass 0 to transmit at the floor SF again. RX-side multi-SF must be enabled on the PEERS for
 * this to help. Sits ON TOP of the built-in per-packet policy (adverts at floor+offset); pass 0
 * to hand control back to that policy. Exposed for a future adaptive-SF layer to drive. */
void radio_set_tx_sf(uint8_t sf)
{
	s_radio.setTxSpreadingFactor(sf);
}

/* Periodic LoRa RX health counters (LR2021 internal stats, aggregated over the primary + side
 * detectors). Prints only when a counter changes. Read it as: rx climbing with crcErr/hdrErr also
 * climbing => packets are detected but not decoding (marginal link, or an SF/LDRO/sync mismatch on
 * a side detector); falseSync climbing => noise is tripping a detector; rx climbing with crc/hdr
 * flat => healthy reception. Counters are cumulative since boot. GODMODE exposes getLoRaRxStats. */
void radio_log_rx_stats(void)
{
	static uint16_t p_total, p_crc, p_hdr, p_false;
	static bool primed;
	uint16_t total = 0, crcErr = 0, hdrErr = 0, falseSync = 0;
	if (s_lora.getLoRaRxStats(&total, &crcErr, &hdrErr, &falseSync) != RADIOLIB_ERR_NONE) return;
	if (primed && total == p_total && crcErr == p_crc && hdrErr == p_hdr && falseSync == p_false)
		return;   /* nothing changed since last dump */
	p_total = total; p_crc = crcErr; p_hdr = hdrErr; p_false = falseSync; primed = true;
	printk("rx-stats: rx=%u crcErr=%u hdrErr=%u falseSync=%u floorSF%u\n",
	       total, crcErr, hdrErr, falseSync, s_lora.primarySF());
}

#ifdef MULTISF_ABTEST
/* Standalone multi-SF side-detector A/B test (bypasses the mesh)
 * Reproduces RadioLib's LR2021_Receive_MultiSF example on THIS board, isolating a single variable:
 * the setLoRaSideDetCad() call. Reuses the already-initialised s_lora and NEVER returns, so the
 * mesh never starts. Frequency/BW come from the boot preset, so identically-built nodes match.
*/

#ifndef MULTISF_ABTEST_PRIMARY_SF
  #define MULTISF_ABTEST_PRIMARY_SF 9
#endif

/* Build identity stamped on every RX line so a mid-stream RTT attach is still unambiguous. */
#ifdef MULTISF_ABTEST_FIX
  #define ABT_FIXTAG "FIX"
#else
  #define ABT_FIXTAG "NOFIX"
#endif

static void abtest_arm_sides(bool verbose)
{
	LR2021LoRaSideDetector_t sd[3];
	for (int i = 0; i < 3; i++) {
		sd[i].sf       = MULTISF_ABTEST_PRIMARY_SF + 1 + i;   /* 10, 11, 12 */
		sd[i].ldro     = ((float)(1UL << sd[i].sf) / s_lora.currentBandwidthKhz()) >= 16.0f;
		sd[i].invertIQ = false;
		sd[i].syncWord = RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE;
	}
	int16_t st = s_lora.setSideDetector(sd, 3);
#ifdef MULTISF_ABTEST_FIX
	/* Explicit per-SF thresholds, datasheet Table 6-19 3-symbol row. */
	static const uint8_t detPeakBySf[8] = { 51, 51, 52, 54, 56, 60, 60, 65 };
	uint8_t pnr[3] = { 0, 0, 0 }, peak[3];
	for (int i = 0; i < 3; i++) peak[i] = detPeakBySf[sd[i].sf - 5];
	int16_t st2 = s_lora.setLoRaSideDetCad(pnr, peak, 3);
	if (verbose)
		printk("ABTEST arm: sides SF%u/%u/%u setSideDetector=%d setLoRaSideDetCad=%d (peak %u/%u/%u) [FIX ON]\n",
		       sd[0].sf, sd[1].sf, sd[2].sf, st, st2, peak[0], peak[1], peak[2]);
#else
	if (verbose)
		printk("ABTEST arm: sides SF%u/%u/%u setSideDetector=%d (det_peak NOT set — stock example) [FIX OFF]\n",
		       sd[0].sf, sd[1].sf, sd[2].sf, st);
#endif
}

void radio_multisf_abtest(void)
{
#ifdef MULTISF_ABTEST_TX
	printk("ABTEST: TX role — sweeping SF%u..%u\n",
	       MULTISF_ABTEST_PRIMARY_SF, MULTISF_ABTEST_PRIMARY_SF + 3);
	const uint8_t sfs[4] = { MULTISF_ABTEST_PRIMARY_SF,     MULTISF_ABTEST_PRIMARY_SF + 1,
	                         MULTISF_ABTEST_PRIMARY_SF + 2, MULTISF_ABTEST_PRIMARY_SF + 3 };
	uint32_t cnt = 0;
	while (1) {
		for (int i = 0; i < 4; i++) {
			s_lora.standby();
			s_lora.setSpreadingFactor(sfs[i]);
			char msg[24];
			int n = snprintf(msg, sizeof(msg), "ABT SF%u #%lu", sfs[i], (unsigned long)cnt);
			int16_t st = s_lora.transmit((uint8_t *)msg, n);
			printk("ABTEST tx: SF%u #%lu st=%d\n", sfs[i], (unsigned long)cnt, st);
			cnt++;
			k_msleep(1500);
		}
	}
#else
	printk("ABTEST: RX role — primary SF%u + side detectors SF%u/%u/%u\n",
	       MULTISF_ABTEST_PRIMARY_SF, MULTISF_ABTEST_PRIMARY_SF + 1,
	       MULTISF_ABTEST_PRIMARY_SF + 2, MULTISF_ABTEST_PRIMARY_SF + 3);
	s_lora.standby();
	s_lora.setSpreadingFactor(MULTISF_ABTEST_PRIMARY_SF);
	abtest_arm_sides(true);
	s_lora.clearIrqFlags(0xFFFFFFFFUL);
	s_lora.startReceive();
	printk("ABTEST build: [%s] compiled %s %s\n", ABT_FIXTAG, __DATE__, __TIME__);
#ifdef MULTISF_ABTEST_FIX
	printk("ABTEST: listening [FIX ON] — expect det=Main AND det=Side1/2/3...\n");
#else
	printk("ABTEST: listening [FIX OFF] — expect ONLY det=Main SF%u...\n", MULTISF_ABTEST_PRIMARY_SF);
#endif

	uint32_t rxcnt = 0;
	uint8_t buf[64];
	while (1) {
		uint32_t irq = s_lora.getIrqFlags();
		if (irq & (RADIOLIB_LR2021_IRQ_RX_DONE | RADIOLIB_LR2021_IRQ_CRC_ERROR)) {
			bool crcFail = irq & RADIOLIB_LR2021_IRQ_CRC_ERROR;
			int len = (int)s_lora.getPacketLength(true);
			uint8_t det = s_lora.readDetectorIndex();
			int sf = (det == 1) ? MULTISF_ABTEST_PRIMARY_SF
			       : (det == 2) ? MULTISF_ABTEST_PRIMARY_SF + 1
			       : (det == 4) ? MULTISF_ABTEST_PRIMARY_SF + 2
			       : (det == 8) ? MULTISF_ABTEST_PRIMARY_SF + 3 : -1;
			const char *dn = (det == 1) ? "Main" : (det == 2) ? "Side1"
			               : (det == 4) ? "Side2" : (det == 8) ? "Side3" : "?";
			if (len > 0) s_lora.readData(buf, len > (int)sizeof(buf) ? sizeof(buf) : len);
			printk("ABTEST[%s] rx #%lu: det=%s SF%d len=%d rssi=%.1f snr=%.1f crc=%s\n",
			       ABT_FIXTAG, (unsigned long)rxcnt++, dn, sf, len,
			       (double)s_lora.getRSSI(), (double)s_lora.getSNR(), crcFail ? "FAIL" : "ok");
			/* periodic identity line: a mid-stream RTT attach still self-identifies the binary */
			if ((rxcnt % 25) == 0)
				printk("ABTEST id: [%s] compiled %s %s (boot rx count %lu)\n",
				       ABT_FIXTAG, __DATE__, __TIME__, (unsigned long)rxcnt);
			s_lora.clearIrqFlags(0xFFFFFFFFUL);
			s_lora.standby();
			abtest_arm_sides(false);     /* re-arm (silent): startReceive alone drops the side detectors */
			s_lora.startReceive();
		}
		k_msleep(2);
	}
#endif
}
#endif  /* MULTISF_ABTEST */
