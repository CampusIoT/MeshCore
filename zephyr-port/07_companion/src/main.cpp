/*
 * Step 4e: the full MeshCore companion on Zephyr. Wires companion_radio's MyMesh +
 * DataStore onto the proven Zephyr seams: radio (target.cpp / RadioLib+ZephyrHal),
 * filesystem (InternalFS / flash_area), and serial = the bonded bt_nus SerialBLEInterface.
 * The phone app pairs (level 4, fixed PIN), then talks the companion protocol over NUS.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <Arduino.h>
#include <target.h>
#include <helpers/nrf54/InternalFileSystem.h>
#include <helpers/SimpleMeshTables.h>
#include "serial_ble_interface.h"
#include "MyMesh.h"   /* examples/companion_radio (on the include path) */
#ifdef DISPLAY_CLASS
#include "zephyr_ssd1306_display.h"
#include "UITask.h"   /* examples/companion_radio/ui-tiny;  the common companion UI */
#endif

SerialShim Serial;

StdRNG fast_rng;
SimpleMeshTables tables;
DataStore store(InternalFS, rtc_clock);
#ifdef DISPLAY_CLASS
static DISPLAY_CLASS display;                 /* ZephyrSSD1306Display (native SSD1306 + CFB) */
static UITask ui_task(&board, &ble);          /* the common ui-tiny UITask, drawing to `display` */
#endif
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
#ifdef DISPLAY_CLASS
              , &ui_task
#endif
);

/* USER button (double-click -> send self-advert)
 * The XIAO nRF54L15 exposes a USER button (devicetree alias sw0 = usr_btn, active-low +
 * pull-up). CONFIG_INPUT/gpio-keys is not enabled, so the pin is free to poll directly.
 * Detect a double-click (two debounced presses within USER_BTN_DBLCLICK_MS) each main-loop
 * tick and fire the_mesh.advert(); the same zero-hop self-advert (+ polyglot copies) the app
 * sends on CMD_SEND_SELF_ADVERT. */
#define USER_BTN_DEBOUNCE_MS  25
#define USER_BTN_DBLCLICK_MS  400
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
static const struct gpio_dt_spec user_btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static bool user_button_double_click(void)
{
	static bool ready, pressed;
	static int64_t edge_t, first_click_t;
	static int clicks;

	if (!ready) {
		if (!gpio_is_ready_dt(&user_btn) ||
		    gpio_pin_configure_dt(&user_btn, GPIO_INPUT) != 0) return false;
		ready = true;
	}

	int64_t now = k_uptime_get();
	bool raw = gpio_pin_get_dt(&user_btn) > 0;          /* DT ACTIVE_LOW -> 1 means pressed */

	if (raw != pressed && (now - edge_t) >= USER_BTN_DEBOUNCE_MS) {
		edge_t  = now;
		pressed = raw;
		if (pressed) {                                  /* debounced press edge */
			if (clicks == 1 && (now - first_click_t) <= USER_BTN_DBLCLICK_MS) {
				clicks = 0;
				return true;                        /* second press in the window == double-click */
			}
			clicks = 1;
			first_click_t = now;
		}
	}
	if (clicks == 1 && (now - first_click_t) > USER_BTN_DBLCLICK_MS) clicks = 0;  /* lone click expired */
	return false;
}
#else
static bool user_button_double_click(void) { return false; }
#endif

int main(void)
{
	printk("\n=== MeshCore companion on Zephyr (board=%s) ===\n", CONFIG_BOARD);

	board.begin();
	if (!radio_init()) { printk("radio_init FAILED\n"); return 0; }
#ifdef MULTISF_ABTEST
	radio_multisf_abtest();   /* standalone side-detector A/B test; never returns (mesh not started) */
#endif
	fast_rng.begin(radio_get_rng_seed());
	if (!InternalFS.begin()) { printk("InternalFS FAILED\n"); return 0; }
	store.begin();
	the_mesh.begin(false);   /* loads prefs from /new_prefs */

#ifdef DISPLAY_CLASS
	if (display.begin()) {   /* false if no OLED is attached -> run headless, mesh unaffected */
		ui_task.begin(&display, &sensors, the_mesh.getNodePrefs());
		printk("OLED: SSD1306 status screen ready\n");
	} else {
		printk("OLED: no display on I2C -> headless\n");
	}
#endif

	/* DEBUG: did the node name survive the last power cycle? On a cold boot this should show
	 * the previously-set name and exists=1; if it shows the default name / exists=0 the prefs
	 * file was lost (reformat or never persisted). Paired with the "FS: mounted OK/FAILED" line. */
	printk("PREFS: cold boot -> /new_prefs exists=%d, node_name='%s'\n",
	       InternalFS.exists("/new_prefs"), the_mesh.getNodePrefs()->node_name);

	char name[48];
	snprintf(name, sizeof(name), "%s%s", BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name);
	ble.begin(name, the_mesh.getBLEPin());
	the_mesh.startInterface(ble);
	printk("companion up: '%s' pin %lu — connect from the MeshCore app\n",
	       name, (unsigned long)the_mesh.getBLEPin());

	while (1) {
		the_mesh.loop();
		rtc_clock.tick();
		sensors.loop();

		if (user_button_double_click()) {
			printk("USER btn: double-click -> self-advert\n");
			the_mesh.advert();
#ifdef DISPLAY_CLASS
			ui_task.wake();                         /* light the screen + reset auto-off */
			ui_task.showAlert("Advert sent", 2000);
#endif
		}

		/* A rename from the app (CMD_SET_ADVERT_NAME) only updates prefs; push it to BLE so
		 * the advertised/GAP name follows without a reboot. */
		char cur[48];
		snprintf(cur, sizeof(cur), "%s%s", BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name);
		if (strcmp(cur, name) != 0) {
			strcpy(name, cur);
			ble.setDeviceName(name);
			/* DEBUG: confirm the rename reached the FS (savePrefs ran in the CMD handler).
			 * exists=1 means /new_prefs was (re)written; if it's still 0 the save never landed. */
			printk("PREFS: rename -> '%s', /new_prefs exists=%d\n",
			       the_mesh.getNodePrefs()->node_name, InternalFS.exists("/new_prefs"));
		}

#ifdef LR2021_MULTISF
		/* multi-SF diagnostic: poll the LR2021 RX health counters every ~2 s (prints on change) */
		static int64_t last_rxstats;
		if (k_uptime_get() - last_rxstats >= 2000) {
			last_rxstats = k_uptime_get();
			radio_log_rx_stats();
		}
#endif

#ifdef DISPLAY_CLASS
		ui_task.loop();   /* render the status screen (MyMesh feeds it via the _ui hooks) */
#endif

		k_msleep(1);   /* snappy serial/radio polling for the app's frame bursts */
	}
	return 0;
}
