/*
 * hv_control.c  –  HV generator control for the ICM7555-based HV supply.
 *
 * Current mode: GPIO12 HIGH = HV on, hardware handles voltage regulation.
 *
 * GPIO12 → ICM7555 oscillator enable
 *   HIGH = oscillator running = HV output active
 *   LOW  = oscillator stopped = HV output off
 *
 * ── Software regulation (disabled) ───────────────────────────────────────
 * The ADC feedback path and hysteresis regulator below are kept for
 * reference but commented out.  Re-enable if software regulation is needed:
 *
 *   GPIO2 / ADC1_CH1 → feedback divider mid-point
 *     R33=R34=R35=1MΩ, R36=18kΩ  →  Vhigh = Vadc × 167.7
 *
 *   Regulation logic:
 *     Vhigh < (target − hysteresis)  →  enable oscillator
 *     Vhigh > (target + hysteresis)  →  disable oscillator
 *     inside dead band               →  no change
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* #include "freertos/semphr.h" */       /* needed for software regulation */

#include "driver/gpio.h"
#include "esp_log.h"

/* ── ADC includes (software regulation only) ──────────────────────────── */
/* #include "esp_adc/adc_oneshot.h"    */
/* #include "esp_adc/adc_cali.h"       */
/* #include "esp_adc/adc_cali_scheme.h"*/

#include "geiger_config.h"
#include "hv_control.h"

static const char *TAG = "HV";

/* ── Internal state ───────────────────────────────────────────────────── */

/* static adc_oneshot_unit_handle_t s_adc_handle   = NULL; */
/* static adc_cali_handle_t         s_cali_handle  = NULL; */
/* static bool                      s_cali_enabled = false; */
/* static SemaphoreHandle_t         s_mutex        = NULL; */
/* static float                     s_voltage      = 0.0f; */

/* ── GPIO control ──────────────────────────────────────────────────────── */

void hv_control_enable(void)
{
    gpio_set_level(GPIO_HV_ENABLE, 1);
    ESP_LOGI(TAG, "HV ON");
}

void hv_control_disable(void)
{
    gpio_set_level(GPIO_HV_ENABLE, 0);
    ESP_LOGI(TAG, "HV OFF");
}

/* ── ADC + calibration init (software regulation only) ───────────────── */
/*
static esp_err_t adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,    // 0–3.1 V range; Vadc_max ≈ 2.39 V at 400 V HV
    };
    ret = adc_oneshot_config_channel(s_adc_handle,
                                     (adc_channel_t)HV_ADC_CHANNEL_NUM,
                                     &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali_handle);
    if (ret == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable – using raw conversion");
        s_cali_enabled = false;
    }

    return ESP_OK;
}
*/

/* ── ADC read + Vhigh conversion (software regulation only) ──────────── */
/*
#define HV_ADC_SAMPLES 4

static float adc_read_vhigh(void)
{
    int32_t raw_sum = 0;
    for (int i = 0; i < HV_ADC_SAMPLES; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc_handle, (adc_channel_t)HV_ADC_CHANNEL_NUM, &raw);
        raw_sum += raw;
    }
    int raw_avg = (int)(raw_sum / HV_ADC_SAMPLES);

    int mv = 0;
    if (s_cali_enabled) {
        adc_cali_raw_to_voltage(s_cali_handle, raw_avg, &mv);
    } else {
        mv = (raw_avg * 3100) / 4095;   // 12-bit, ~3100 mV full-scale at ATTEN_DB_12
    }

    float vadc  = mv / 1000.0f;
    float vhigh = vadc * HV_ADC_DIVIDER_RATIO;

    ESP_LOGD(TAG, "raw=%d  vadc=%.3fV  vhigh=%.1fV", raw_avg, vadc, vhigh);
    return vhigh;
}
*/

/* ── Public API ───────────────────────────────────────────────────────── */

esp_err_t hv_control_init(void)
{
    /*
     * GPIO12 is already configured as OUTPUT LOW by the early GPIO init
     * block in app_main() – oscillator stays off until hv_control_task()
     * enables it after the startup delay.
     */

    /* Software regulation: initialise ADC and mutex.
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    esp_err_t ret = adc_init();
    if (ret != ESP_OK) return ret;
    */

    ESP_LOGI(TAG, "initialized (hardware regulation mode)");
    return ESP_OK;
}

float hv_control_get_voltage(void)
{
    /* Software regulation: return measured voltage.
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    float v = s_voltage;
    xSemaphoreGive(s_mutex);
    return v;
    */
    return 0.0f;   /* not measured in hardware-regulation mode */
}

/* ── Task ─────────────────────────────────────────────────────────────── */

void hv_control_task(void *arg)
{
    (void)arg;

    /* Startup delay – let the rest of the system settle before enabling HV */
    vTaskDelay(pdMS_TO_TICKS(500));

    hv_control_enable();

    /* ── Software regulation loop (disabled) ──────────────────────────── */
    /*
    const TickType_t period = pdMS_TO_TICKS(HV_REGULATION_PERIOD_MS);
    TickType_t last_wake    = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, period);

        float vhigh = adc_read_vhigh();

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_voltage = vhigh;
        xSemaphoreGive(s_mutex);

        if (vhigh < (HV_TARGET_VOLTAGE_V - HV_HYSTERESIS_V)) {
            if (!s_hv_on) {
                hv_control_enable();
                ESP_LOGI(TAG, "HV low (%.1fV) – oscillator ON", vhigh);
            }
        } else if (vhigh > (HV_TARGET_VOLTAGE_V + HV_HYSTERESIS_V)) {
            if (s_hv_on) {
                hv_control_disable();
                ESP_LOGI(TAG, "HV high (%.1fV) – oscillator OFF", vhigh);
            }
        }
    }
    */

    /* Hardware regulation active – nothing more to do */
    vTaskDelete(NULL);
}
