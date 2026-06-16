#include "button.h"
#include "router.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_zigbee.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "BUTTON";

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define BOOT_BTN_GPIO       9

#define TAP_WINDOW_MS       500U    /* max ms between consecutive taps     */
#define HOLD_5S_MS          5000U   /* hold threshold for factory reset     */
#define BLINK_SLOW_MS       200U
#define BLINK_FAST_MS       100U
#define TX_CONFIRM_FLASHES  3U
#define TX_FLASH_ON_MS      120U
#define TX_FLASH_OFF_MS     120U

/* -------------------------------------------------------------------------
 * LED helpers
 * ---------------------------------------------------------------------- */
extern void set_led_locked(uint8_t r, uint8_t g, uint8_t b);

#define _OFF              0,   0,   0
#define _MAGENTA        255,   0, 255
#define _BRIGHT_RED     255,   0,   0
#define _SOFT_BLUE        0,   0,  64
#define _GREEN            0,  16,   0
#define _RED             64,   0,   0

/* -------------------------------------------------------------------------
 * State
 * ---------------------------------------------------------------------- */

static volatile uint8_t  s_tap_count  = 0;
static volatile bool     s_high_power = false;   /* boot default: 8 dBm */
static volatile bool     s_holding    = false;   /* button currently held */

static TimerHandle_t     s_tap_timer   = NULL;
static TimerHandle_t     s_hold_timer  = NULL;
static TimerHandle_t     s_blink_timer = NULL;

static volatile bool     s_blinking   = false;
static volatile bool     s_blink_state = false;

/* -------------------------------------------------------------------------
 * Forward declarations (NO IRAM_ATTR here -- only on the definition)
 * ---------------------------------------------------------------------- */
static void tap_window_cb(TimerHandle_t t);
static void hold_cb(TimerHandle_t t);
static void blink_cb(TimerHandle_t t);
static void do_tx_toggle(void);
static void do_factory_reset(void);
static void start_blink(bool fast);
static void stop_blink(void);
static void btn_isr(void *arg);

/* -------------------------------------------------------------------------
 * ISR -- both edges
 * ---------------------------------------------------------------------- */
static void IRAM_ATTR btn_isr(void *arg)
{
    BaseType_t woken = pdFALSE;

    if (gpio_get_level(BOOT_BTN_GPIO) == 0) {
        /* PRESS */
        s_tap_count++;
        s_holding = true;
        xTimerResetFromISR(s_tap_timer,  &woken);
        xTimerResetFromISR(s_hold_timer, &woken);
    } else {
        /* RELEASE */
        s_holding = false;
        xTimerStopFromISR(s_hold_timer, &woken);
        /* If the hold timer hadn't fired yet, cancel any blink in progress */
        s_blinking = false;
    }

    portYIELD_FROM_ISR(woken);
}

/* -------------------------------------------------------------------------
 * Tap-window timer
 * ---------------------------------------------------------------------- */
static void tap_window_cb(TimerHandle_t t)
{
    (void)t;
    uint8_t taps = s_tap_count;
    s_tap_count  = 0;
    if (taps >= 3) {
        do_tx_toggle();
    }
}

/* -------------------------------------------------------------------------
 * Hold timer -- fires after HOLD_5S_MS
 * ---------------------------------------------------------------------- */
static void hold_cb(TimerHandle_t t)
{
    (void)t;
    ESP_LOGW(TAG, "Hold 5 s -- factory reset triggered");
    start_blink(true);
    /* Brief fast blink so the user sees feedback before erase */
    vTaskDelay(pdMS_TO_TICKS(600));
    stop_blink();
    do_factory_reset();
}

/* -------------------------------------------------------------------------
 * Blink timer
 * ---------------------------------------------------------------------- */
static void blink_cb(TimerHandle_t t)
{
    if (!s_blinking) {
        xTimerStop(t, 0);
        set_led_locked(_OFF);
        s_blink_state = false;
        return;
    }
    s_blink_state = !s_blink_state;
    s_blink_state ? set_led_locked(_MAGENTA) : set_led_locked(_OFF);
}

/* -------------------------------------------------------------------------
 * Actions
 * ---------------------------------------------------------------------- */
static void do_tx_toggle(void)
{
    s_high_power = !s_high_power;
    int8_t pwr = s_high_power ? ROUTER_TX_POWER_HIGH_DBM : ROUTER_TX_POWER_LOW_DBM;

    esp_zigbee_lock_acquire(portMAX_DELAY);
    esp_err_t err = esp_ieee802154_set_txpower(pwr);
    esp_zigbee_lock_release();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_txpower(%d) failed: %s", pwr, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "TX power -> %d dBm (%s)",
             pwr, s_high_power ? "BOOST (20 dBm)" : "NORMAL (8 dBm)");

    uint8_t r, g, b;
    if (s_high_power) { r = 255; g = 0;  b = 0;  }
    else              { r = 0;   g = 0;  b = 64; }

    for (int i = 0; i < TX_CONFIRM_FLASHES; i++) {
        set_led_locked(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(TX_FLASH_ON_MS));
        set_led_locked(_OFF);
        vTaskDelay(pdMS_TO_TICKS(TX_FLASH_OFF_MS));
    }
    set_led_locked(_GREEN);
}

static void do_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing NVS partition '%s'",
             ESP_ZIGBEE_STORAGE_PARTITION_NAME);
    set_led_locked(_RED);

    /* nvs_flash_erase_partition() does a full partition erase via the flash
     * driver, bypassing the NVS layer entirely.  This works correctly even
     * when the partition is uninitialised, corrupted, or the NVS handle
     * cannot be opened -- which is exactly the failure mode we saw with the
     * previous nvs_open / nvs_erase_all approach. */
    esp_err_t err = nvs_flash_erase_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_erase_partition failed: %s", esp_err_to_name(err));
        /* Proceed to reboot anyway: a failed erase is better than staying
         * in a broken state. The user can retry. */
    } else {
        ESP_LOGW(TAG, "Partition erased OK -- rebooting");
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

/* -------------------------------------------------------------------------
 * Blink helpers
 * ---------------------------------------------------------------------- */
static void start_blink(bool fast)
{
    s_blinking    = true;
    s_blink_state = false;
    TickType_t period = fast ? pdMS_TO_TICKS(BLINK_FAST_MS)
                             : pdMS_TO_TICKS(BLINK_SLOW_MS);
    xTimerChangePeriod(s_blink_timer, period, 0);
    xTimerReset(s_blink_timer, 0);
}

static void stop_blink(void)
{
    s_blinking = false;
    xTimerStop(s_blink_timer, 0);
    set_led_locked(_OFF);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
esp_err_t button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOOT_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "gpio_config failed");

    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(isr_err, TAG, "gpio_install_isr_service failed");
    }

    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(BOOT_BTN_GPIO, btn_isr, NULL),
        TAG, "gpio_isr_handler_add failed");

    s_tap_timer = xTimerCreate("btn_tap",
                               pdMS_TO_TICKS(TAP_WINDOW_MS),
                               pdFALSE, NULL, tap_window_cb);
    if (!s_tap_timer) return ESP_ERR_NO_MEM;

    s_hold_timer = xTimerCreate("btn_hold",
                                pdMS_TO_TICKS(HOLD_5S_MS),
                                pdFALSE, NULL, hold_cb);
    if (!s_hold_timer) return ESP_ERR_NO_MEM;

    s_blink_timer = xTimerCreate("btn_blink",
                                 pdMS_TO_TICKS(BLINK_SLOW_MS),
                                 pdTRUE, NULL, blink_cb);
    if (!s_blink_timer) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "BOOT button ready (GPIO%d)", BOOT_BTN_GPIO);
    ESP_LOGI(TAG, "  3x tap   -> TX toggle  %d dBm <-> %d dBm",
             ROUTER_TX_POWER_LOW_DBM, ROUTER_TX_POWER_HIGH_DBM);
    ESP_LOGI(TAG, "  Hold 5 s -> factory reset (NVS erase + reboot)");
    return ESP_OK;
}
