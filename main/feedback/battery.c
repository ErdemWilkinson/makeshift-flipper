#include "battery.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "driver/gpio.h"

// Battery sense pin: GPIO3 = ADC1 channel 3 on the ESP32-C6. GPIO3 used to be
// the (unverified, floating) joystick centre press; repurposed here as an
// analog input, which is a better fit -- driven by the resistor divider it no
// longer floats. Wire it as:  BAT+ --[R1]--+--[R2]-- GND,  node -> GPIO3.
// With R1 == R2 the node sees half the pack voltage, so a 4.2 V cell reads
// ~2.1 V, comfortably inside the ADC range at 12 dB attenuation.
#define BATTERY_ADC_UNIT      ADC_UNIT_1
#define BATTERY_ADC_CHANNEL   ADC_CHANNEL_3   // GPIO3
#define BATTERY_ADC_ATTEN     ADC_ATTEN_DB_12 // ~0..3.1 V usable
#define BATTERY_DIVIDER_NUM   2               // divider is 2:1 (R1 == R2)
#define BATTERY_DIVIDER_DEN   1

// Below this reconstructed pack voltage we assume nothing is wired to the pin
// (a bare ADC input floats near 0). A real single cell never sits this low
// while the board is running, so it's a safe "not present" threshold.
#define BATTERY_ABSENT_MV     2500

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_cali_ok;
static bool s_ready;

void battery_init(void)
{
    // GPIO3 was previously configured as a pulled-up digital input (the old
    // joystick centre press). A pull-up on an ADC pin pushes a floating,
    // unwired input toward full-scale, which reads as a bogus "full battery".
    // Explicitly float the pin so that, until the divider is soldered, the
    // reading stays low/unstable and battery_read() reports "not present"
    // instead of a fake level that jumps around on every redraw.
    gpio_set_pull_mode((gpio_num_t)3, GPIO_FLOATING);

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed; battery gauge disabled");
        return;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "ADC channel config failed; battery gauge disabled");
        return;
    }

    // Curve-fitting calibration is what the C6 supports; fall back to raw
    // scaling if it isn't available so a reading still comes through.
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK);
    s_ready = true;
}

// A LiPo's voltage-vs-charge curve is very non-linear, so map a handful of
// (mV, %) points and interpolate between them instead of assuming a straight
// line from 3.0 to 4.2 V (which badly overstates a nearly-empty cell).
static int lipo_percent(int mv)
{
    static const struct { int mv; int pct; } curve[] = {
        { 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3900, 70 },
        { 3800, 60 },  { 3700, 45 }, { 3600, 30 }, { 3500, 18 },
        { 3400, 9 },   { 3300, 4 },  { 3000, 0 },
    };
    const int n = (int)(sizeof(curve) / sizeof(curve[0]));
    if (mv >= curve[0].mv) {
        return 100;
    }
    if (mv <= curve[n - 1].mv) {
        return 0;
    }
    for (int i = 0; i < n - 1; i++) {
        if (mv <= curve[i].mv && mv > curve[i + 1].mv) {
            int span_mv = curve[i].mv - curve[i + 1].mv;
            int span_pct = curve[i].pct - curve[i + 1].pct;
            int into = curve[i].mv - mv;
            return curve[i].pct - (span_pct * into) / span_mv;
        }
    }
    return 0;
}

battery_reading_t battery_read(void)
{
    battery_reading_t out = { .valid = false, .millivolts = 0, .percent = 0 };
    if (!s_ready) {
        return out;
    }

    // Average a few samples to knock down ADC noise.
    int64_t acc_mv = 0;
    int taken = 0;
    int min_mv = 100000, max_mv = -1;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            continue;
        }
        int mv = 0;
        if (s_cali_ok) {
            if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
                continue;
            }
        } else {
            // Rough fallback: 12-bit range over ~3100 mV at 12 dB.
            mv = (raw * 3100) / 4095;
        }
        acc_mv += mv;
        if (mv < min_mv) min_mv = mv;
        if (mv > max_mv) max_mv = mv;
        taken++;
    }
    if (taken == 0) {
        return out;
    }

    // A real cell through the divider is steady to within a few mV across these
    // eight back-to-back samples. An unwired (floating) pin swings wildly, so a
    // large spread means "nothing connected" -- report not-present rather than a
    // number that jumps on every redraw. (This was the "percent drops as I
    // scroll" symptom: each redraw re-read a floating pin.)
    if (max_mv - min_mv > 150) {
        return out;
    }

    int pin_mv = (int)(acc_mv / taken);
    int pack_mv = pin_mv * BATTERY_DIVIDER_NUM / BATTERY_DIVIDER_DEN;
    if (pack_mv < BATTERY_ABSENT_MV) {
        return out; // nothing plausible on the pin -> divider not wired yet
    }

    out.valid = true;
    out.millivolts = pack_mv;
    out.percent = lipo_percent(pack_mv);
    return out;
}
