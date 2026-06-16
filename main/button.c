#include "button.h"
#include "router.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_zigbee.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "BUTTON";

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */

#define BOOT_BTN_GPIO           9

/* Triple-tap: max ms between consecutive press edges */
#define TAP_WINDOW_MS           500U
/* Hold thresholds */
#define HOLD_REBOOT_MS          3000U
#define HOLD_FACTORY_RESET_MS   5000U
/* Blink periods */
#define BLINK_SLOW_MS           200U
#define BLINK_FAST_MS           100U
/* TX-toggle confirmation flashes */
#define TX_CONFIRM_FLASHES      3U
#define TX_FLASH_ON_MS          120U
#define TX_FLASH_OFF_MS         120U

/* -------------------------------------------------------------------------
 * LED helpers
 *
 * set_led_locked() is defined in router.c. Forward-declaring it here is
 * correct: both TUs are linked into the same binary.
 * ---------------------------------------------------------------------- */
extern void set_led_locked(uint8_t r, uint8_t g, uint8_t b);

/* Local colour aliases -- not exported, used only in this module */
#define _OFF              0,   0,   0
#define _MAGENTA        255,   0, 255   /* destructive action in progress */
#define _BRIGHT_RED     255,   0,   0   /* TX boost-power confirmation     */
#define _SOFT_BLUE        0,   0,  64   /* TX normal-power confirmation    */
#define _GREEN            0,  16,   0   /* back to normal                  */
#define _RED             64,   0,   0   /* error / off-network             */

/* -------------------------------------------------------------------------
 * State
 * ---------------------------------------------------------------------- */

static volatile uint8_t  s_tap_count        = 0;
/* false = normal power (LOW = 8 dBm) which is the boot default */
static volatile bool     s_high_power       = false;

static TimerHandle_t     s_tap_timer        = NULL;
static TimerHandle_t     s_hold_timer       = NULL;
static TimerHandle_t     s_blink_timer      = NULL;

static volatile bool     s_blinking         = false;
static volatile bool     s_blink_state      = false;
static volatile bool     s_reboot_pending   = false;
static volatile uint8_t  s_hold_stage       = 0;     /* 0=idle 1=reboot-arm 2=fr-arm */

/* -------------------------------------------------------------------------
 * Forward declarations
 * NOTE: IRAM_ATTR must NOT appear on forward declarations -- only on the
 * definition. Placing it on both causes a conflicting section attribute
 * warning (-Wattributes) that becomes an error with -Werror.
 * ---------------------------------------------------------------------- */
static void tap_window_cb(TimerHandle_t t);
static void hold_cb(TimerHandle_t t);
static void blink_cb(TimerHandle_t t);
static void do_tx_toggle(void);
static void do_reboot(void);
static void do_factory_reset(void);
static void start_blink(bool fast, bool reboot_on_stop);
static void stop_blink(void);
static void btn_isr(void *arg);   /* IRAM_ATTR only on definition below */

/* -------------------------------------------------------------------------
 * ISR -- both edges
 * ---------------------------------------------------------------------- */
static void IRAM_ATTR btn_isr(void *arg)
{
    BaseType_t woken = pdFALSE;

    if (gpio_get_level(BOOT_BTN_GPIO) == 0) {
        /* PRESS: count tap, restart both timers */
        s_tap_count++;
        xTimerResetFromISR(s_tap_timer,  &woken);
        s_hold_stage = 0;
        xTimerResetFromISR(s_hold_timer, &woken);
    } else {
        /* RELEASE: stop hold timer */
        xTimerStopFromISR(s_hold_timer, &woken);
        if (s_hold_stage == 0) {
            s_blinking = false;
        }
    }

    portYIELD_FROM_ISR(woken);
}

/* -------------------------------------------------------------------------
 * Tap-window timer -- fires TAP_WINDOW_MS after last tap
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
 * Hold timer -- two-stage: reboot at 3 s, factory reset at 5 s
 * ---------------------------------------------------------------------- */
static void hold_cb(TimerHandle_t t)
{
    s_hold_stage++;

    if (s_hold_stage == 1) {
        ESP_LOGI(TAG, "Hold 3 s -- reboot armed (release to reboot, keep holding for factory reset)");
        start_blink(false, true);
        xTimerChangePeriod(t, pdMS_TO_TICKS(HOLD_FACTORY_RESET_MS - HOLD_REBOOT_MS), 0);
        xTimerReset(t, 0);
    } else if (s_hold_stage == 2) {
        ESP_LOGW(TAG, "Hold 5 s -- factory reset triggered");
        stop_blink();
        start_blink(true, false);
        vTaskDelay(pdMS_TO_TICKS(600));
        do_factory_reset();
    }
}

/* -------------------------------------------------------------------------
 * Blink timer -- auto-reload; toggles MAGENTA
 * ---------------------------------------------------------------------- */
static void blink_cb(TimerHandle_t t)
{
    if (!s_blinking) {
        xTimerStop(t, 0);
        set_led_locked(_OFF);
        s_blink_state = false;
        if (s_reboot_pending) {
            s_reboot_pending = false;
            vTaskDelay(pdMS_TO_TICKS(100));
            do_reboot();
        }
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
    if (s_high_power) { r = 255; g = 0;  b = 0;  }   /* BRIGHT_RED  = boost  */
    else              { r = 0;   g = 0;  b = 64; }   /* SOFT_BLUE   = normal */

    for (int i = 0; i < TX_CONFIRM_FLASHES; i++) {
        set_led_locked(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(TX_FLASH_ON_MS));
        set_led_locked(_OFF);
        vTaskDelay(pdMS_TO_TICKS(TX_FLASH_OFF_MS));
    }
    set_led_locked(_GREEN);
}

static void do_reboot(void)
{
    ESP_LOGI(TAG, "User reboot (hold 3 s)");
    set_led_locked(_RED);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void do_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing Zigbee NVS partition");
    set_led_locked(_RED);

    nvs_handle_t h;
    if (nvs_open(ESP_ZIGBEE_STORAGE_PARTITION_NAME, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    } else {
        nvs_flash_erase_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME);
    }

    ESP_LOGW(TAG, "NVS erased -- rebooting");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

/* -------------------------------------------------------------------------
 * Blink helpers
 * ---------------------------------------------------------------------- */
static void start_blink(bool fast, bool reboot_on_stop)
{
    s_reboot_pending = reboot_on_stop;
    s_blinking       = true;
    s_blink_state    = false;
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

    /* gpio_install_isr_service may already be installed by another component.
     * ESP_ERR_INVALID_STATE means it is already up -- that is fine. */
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
    if (!s_tap_timer)  return ESP_ERR_NO_MEM;

    s_hold_timer = xTimerCreate("btn_hold",
                                pdMS_TO_TICKS(HOLD_REBOOT_MS),
                                pdFALSE, NULL, hold_cb);
    if (!s_hold_timer) return ESP_ERR_NO_MEM;

    s_blink_timer = xTimerCreate("btn_blink",
                                 pdMS_TO_TICKS(BLINK_SLOW_MS),
                                 pdTRUE, NULL, blink_cb);
    if (!s_blink_timer) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "BOOT button ready (GPIO%d)", BOOT_BTN_GPIO);
    ESP_LOGI(TAG, "  3x tap   -> TX toggle  %d dBm (normal) / %d dBm (boost)",
             ROUTER_TX_POWER_LOW_DBM, ROUTER_TX_POWER_HIGH_DBM);
    ESP_LOGI(TAG, "  Hold 3 s -> reboot");
    ESP_LOGI(TAG, "  Hold 5 s -> factory reset");
    return ESP_OK;
}
