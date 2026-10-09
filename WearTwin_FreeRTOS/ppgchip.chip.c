#include "wokwi-api.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define I2C_ADDRESS 0x57
#define FIFO_DEPTH 32
#define MAX_18BIT 0x3FFFFu
#define DEFAULT_SAMPLE_RATE 100u
#define DEFAULT_SAMPLE_PERIOD_US (1000000u / DEFAULT_SAMPLE_RATE)
#define PULSE_ARRIVAL_SECONDS 0.150f

// This is a practical MAX30102-style simulation model, not a transistor-level
// model of the optical package. It implements the common I2C/register/FIFO path
// used by firmware and generates raw 18-bit RED/IR data for host-side analysis.
typedef struct {
    pin_t beat_in;
    pin_t mode0;
    pin_t mode1;
    pin_t mode2;
    timer_t timer;
    i2c_dev_t i2c;

    uint32_t spo2_attr;
    uint32_t perfusion_attr;
    uint32_t motion_attr;

    uint8_t regs[256];
    uint8_t reg_pointer;
    bool expect_register;

    uint32_t red_fifo[FIFO_DEPTH];
    uint32_t ir_fifo[FIFO_DEPTH];
    uint8_t fifo_wr_ptr;
    uint8_t fifo_rd_ptr;
    uint8_t fifo_count;
    uint8_t fifo_overflow;
    uint8_t fifo_byte_index;
    uint32_t read_red;
    uint32_t read_ir;

    uint32_t sample_rate;
    uint32_t sample_period_us;
    uint32_t sample_number;

    uint64_t last_beat_ns;
    uint64_t previous_beat_ns;
    float rr_seconds;
    bool have_beat;

    float baseline_phase;
    uint32_t rng;
} ppg_state_t;

static uint32_t random_u32(ppg_state_t *chip) {
    // Per-chip xorshift PRNG: deterministic, but not the same noise on each sample.
    uint32_t x = chip->rng ? chip->rng : 0xA341316Cu;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    chip->rng = x;
    return x;
}

static float random_signed(ppg_state_t *chip) {
    return ((float)(random_u32(chip) & 0x00FFFFFFu) / 8388607.5f) - 1.0f;
}

static float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static uint32_t clamp18(float x) {
    if (x < 0.0f) return 0;
    if (x > (float)MAX_18BIT) return MAX_18BIT;
    return (uint32_t)(x + 0.5f);
}

// Phase 0..1 pulse template. The quick systolic upstroke and slower decline
// intentionally differ from the electrical ECG waveform.
static float ppg_pulse(float phase) {
    static const float x[] = {0.00f, 0.06f, 0.12f, 0.18f, 0.25f, 0.34f, 0.44f, 0.52f, 0.60f, 0.72f, 0.86f, 1.00f};
    static const float y[] = {0.00f, 0.04f, 0.28f, 0.78f, 1.00f, 0.82f, 0.62f, 0.52f, 0.57f, 0.42f, 0.18f, 0.00f};
    if (phase <= 0.0f || phase >= 1.0f) return 0.0f;
    for (unsigned i = 1; i < sizeof(x) / sizeof(x[0]); ++i) {
        if (phase <= x[i]) {
            float f = (phase - x[i - 1]) / (x[i] - x[i - 1]);
            return y[i - 1] + f * (y[i] - y[i - 1]);
        }
    }
    return 0.0f;
}

static float get_nominal_bpm(ppg_state_t *chip) {
    // The ECG chip sends actual beat events over BEAT_IN. These mode pins are
    // only a fallback if the event wire is disconnected.
    if (pin_read(chip->mode0) == HIGH) return 55.0f;
    if (pin_read(chip->mode1) == HIGH) return 75.0f;
    if (pin_read(chip->mode2) == HIGH) return 110.0f;
    return 75.0f;
}

static void on_beat_change(void *user_data, pin_t pin, uint32_t value) {
    (void)pin;
    ppg_state_t *chip = (ppg_state_t *)user_data;
    if (value != HIGH) return;

    uint64_t now = get_sim_nanos();
    if (chip->last_beat_ns != 0 && now > chip->last_beat_ns) {
        float rr = (float)(now - chip->last_beat_ns) / 1000000000.0f;
        // Reject implausible edge intervals rather than letting a transient
        // create a wildly stretched or compressed simulated pulse.
        if (rr >= 0.30f && rr <= 2.0f) chip->rr_seconds = rr;
    }
    chip->previous_beat_ns = chip->last_beat_ns;
    chip->last_beat_ns = now;
    chip->have_beat = true;
}

static float pulse_phase(ppg_state_t *chip, uint64_t now_ns) {
    float rr = chip->rr_seconds;
    if (rr < 0.30f || rr > 2.0f) rr = 60.0f / get_nominal_bpm(chip);

    uint64_t origin_ns = chip->have_beat ? chip->last_beat_ns : 0;
    float age;
    if (chip->have_beat) {
        age = (float)(now_ns - origin_ns) / 1000000000.0f;
    } else {
        // Fallback lets the sensor produce data even if BEAT_IN is not wired.
        age = (float)now_ns / 1000000000.0f;
    }
    age -= PULSE_ARRIVAL_SECONDS;
    if (age < 0.0f) return -1.0f;

    // A peripheral pulse occupies part of the RR interval; it is not an ECG copy.
    float duration = rr * 0.72f;
    return age >= duration ? -1.0f : age / duration;
}

static void fifo_push(ppg_state_t *chip, uint32_t red, uint32_t ir) {
    if (chip->fifo_count >= FIFO_DEPTH) {
        chip->fifo_overflow = (uint8_t)((chip->fifo_overflow + 1u) & 0x0Fu);
        chip->regs[0x05] = chip->fifo_overflow;
        chip->regs[0x00] |= 0x40u; // FIFO almost/full-style status indicator
        return;
    }
    chip->red_fifo[chip->fifo_wr_ptr] = red & MAX_18BIT;
    chip->ir_fifo[chip->fifo_wr_ptr] = ir & MAX_18BIT;
    chip->fifo_wr_ptr = (uint8_t)((chip->fifo_wr_ptr + 1u) & 0x1Fu);
    chip->fifo_count++;
    chip->regs[0x04] = chip->fifo_wr_ptr;
}

static void generate_sample(ppg_state_t *chip) {
    // MAX30102 mode 0x02 = HR only, 0x03 = SpO2 (RED + IR).
    uint8_t mode = chip->regs[0x09] & 0x07u;
    if ((chip->regs[0x09] & 0x40u) || mode == 0u || mode == 0x04u) return;

    // Read simulator controls and clamp them before using them in arithmetic.
    float spo2 = clampf((float)attr_read(chip->spo2_attr), 70.0f, 100.0f);
    float perfusion = clampf(attr_read_float(chip->perfusion_attr), 0.10f, 2.0f);
    float motion = clampf(attr_read_float(chip->motion_attr), 0.0f, 1.0f);

    // Generate a stable pulse directly from sample count. This avoids jumps
    // when the simulated ECG beat wire has an initial/irregular edge.
    float bpm = 60.0f / ((chip->rr_seconds >= 0.30f && chip->rr_seconds <= 2.0f)
                         ? chip->rr_seconds : 0.8f);
    float period_samples = (float)chip->sample_rate * 60.0f / bpm;
    if (period_samples < 1.0f) period_samples = 1.0f;
    float phase = fmodf((float)chip->sample_number, period_samples) / period_samples;
    float pulse = ppg_pulse(phase);

    // The empirical mapping creates synthetic data only; this is not a
    // clinical calibration and the chip does not directly output SpO2.
    float ratio_of_ratios = clampf((110.0f - spo2) / 25.0f, 0.35f, 1.60f);

    // Keep DC well inside the 18-bit ADC range and AC a small fraction of DC.
    // At perfusion=1, IR pulse amplitude is about 1.8% of DC; RED is scaled
    // by the target-dependent ratio. This produces a stable ratio for firmware.
    const float red_dc = 80000.0f;
    const float ir_dc = 100000.0f;

    // Perfusion controls pulse amplitude, while target SpO2 controls the
    // normalized RED/IR AC ratio. Compensate for the different DC levels:
    // (RED_AC / RED_DC) / (IR_AC / IR_DC) ~= ratio_of_ratios.
    const float ir_ac_fraction = 0.018f * perfusion;
    const float red_ac_fraction =
        ir_ac_fraction * ratio_of_ratios * (red_dc / ir_dc);

    // Small baseline drift and bounded noise; motion increases noise without
    // allowing it to dominate the pulse or drive samples near ADC full scale.
    float t = (float)chip->sample_number / (float)chip->sample_rate;
    float baseline = 100.0f * sinf(2.0f * PI * 0.18f * t + chip->baseline_phase);
    float motion_noise = motion * (350.0f * random_signed(chip)
                                  + 220.0f * sinf(2.0f * PI * 1.7f * t));
    float common_noise = 45.0f * random_signed(chip) + baseline + motion_noise;
    float red_noise = 35.0f * random_signed(chip);
    float ir_noise = 35.0f * random_signed(chip);

    uint32_t red = clamp18(red_dc + red_dc * red_ac_fraction * pulse
                           + common_noise + red_noise);
    uint32_t ir = clamp18(ir_dc + ir_dc * ir_ac_fraction * pulse
                          + common_noise + ir_noise);

    if (mode == 0x02u) {
        // HR mode: IR only in the first three bytes.
        fifo_push(chip, 0, ir);
    } else {
        // SpO2 mode: six bytes per sample, RED first then IR.
        fifo_push(chip, red, ir);
    }
    chip->sample_number++;
}

static void on_timer(void *user_data) {
    generate_sample((ppg_state_t *)user_data);
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
    (void)user_data;
    (void)address;
    ppg_state_t *chip = (ppg_state_t *)user_data;
    chip->expect_register = !read;
    return true;
}

static uint8_t fifo_read_byte(ppg_state_t *chip) {
    if (chip->fifo_count == 0) return 0;

    if (chip->fifo_byte_index == 0) {
        chip->read_red = chip->red_fifo[chip->fifo_rd_ptr];
        chip->read_ir = chip->ir_fifo[chip->fifo_rd_ptr];
    }

    uint8_t result = 0;
    if ((chip->regs[0x09] & 0x07u) == 0x02u) {
        // HR mode: IR only, three bytes per sample.
        uint8_t idx = chip->fifo_byte_index % 3u;
        result = (uint8_t)((chip->read_ir >> (16u - 8u * idx)) & 0xFFu);
        chip->fifo_byte_index++;
        if (chip->fifo_byte_index >= 3u) {
            chip->fifo_byte_index = 0;
            chip->fifo_rd_ptr = (uint8_t)((chip->fifo_rd_ptr + 1u) & 0x1Fu);
            chip->fifo_count--;
            chip->regs[0x06] = chip->fifo_rd_ptr;
        }
    } else {
        uint8_t idx = chip->fifo_byte_index;
        if (idx < 3u) result = (uint8_t)((chip->read_red >> (16u - 8u * idx)) & 0xFFu);
        else result = (uint8_t)((chip->read_ir >> (16u - 8u * (idx - 3u))) & 0xFFu);
        chip->fifo_byte_index++;
        if (chip->fifo_byte_index >= 6u) {
            chip->fifo_byte_index = 0;
            chip->fifo_rd_ptr = (uint8_t)((chip->fifo_rd_ptr + 1u) & 0x1Fu);
            chip->fifo_count--;
            chip->regs[0x06] = chip->fifo_rd_ptr;
        }
    }
    return result;
}

static uint8_t on_i2c_read(void *user_data) {
    ppg_state_t *chip = (ppg_state_t *)user_data;
    uint8_t reg = chip->reg_pointer;
    uint8_t value = 0;

    if (reg == 0x07u) {
        value = fifo_read_byte(chip);
    } else if (reg == 0x00u) {
        value = chip->regs[0x00];
        chip->regs[0x00] = 0;
    } else if (reg == 0x04u) {
        value = chip->fifo_wr_ptr & 0x1Fu;
    } else if (reg == 0x05u) {
        value = chip->fifo_overflow & 0x0Fu;
    } else if (reg == 0x06u) {
        value = chip->fifo_rd_ptr & 0x1Fu;
    } else if (reg == 0xFFu) {
        value = 0x15u; // MAX30102 PART_ID
    } else if (reg == 0xFEu) {
        value = 0x03u; // simulated revision
    } else {
        value = chip->regs[reg];
    }

    if (reg != 0x07u) chip->reg_pointer++;
    return value;
}

static void reset_fifo(ppg_state_t *chip) {
    chip->fifo_wr_ptr = 0;
    chip->fifo_rd_ptr = 0;
    chip->fifo_count = 0;
    chip->fifo_overflow = 0;
    chip->fifo_byte_index = 0;
    chip->regs[0x04] = 0;
    chip->regs[0x05] = 0;
    chip->regs[0x06] = 0;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
    ppg_state_t *chip = (ppg_state_t *)user_data;
    if (chip->expect_register) {
        chip->reg_pointer = data;
        chip->expect_register = false;
        return true;
    }

    uint8_t reg = chip->reg_pointer;
    if (reg == 0x04u) {
        chip->fifo_wr_ptr = data & 0x1Fu;
        chip->regs[0x04] = chip->fifo_wr_ptr;
    } else if (reg == 0x05u) {
        chip->fifo_overflow = data & 0x0Fu;
        chip->regs[0x05] = chip->fifo_overflow;
    } else if (reg == 0x06u) {
        chip->fifo_rd_ptr = data & 0x1Fu;
        chip->fifo_count = 0;
        chip->fifo_byte_index = 0;
        chip->regs[0x06] = chip->fifo_rd_ptr;
    } else {
        chip->regs[reg] = data;
        if (reg == 0x09u && (data & 0x80u)) {
            reset_fifo(chip);
            chip->regs[0x09] = (uint8_t)(data & ~0x80u);
        }
        if (reg == 0x0Au) {
            static const uint16_t rates[] = {50, 100, 200, 400, 800, 1000, 1600, 3200};
            uint8_t rate_code = (data >> 2) & 0x07u;
            chip->sample_rate = rates[rate_code];
            chip->sample_period_us = (1000000u + chip->sample_rate / 2u) / chip->sample_rate;
            if (chip->sample_period_us == 0) chip->sample_period_us = 1;
            timer_start(chip->timer, chip->sample_period_us, true);
        }
    }
    chip->reg_pointer++;
    return true;
}

static void on_i2c_disconnect(void *user_data) {
    (void)user_data;
}

void chip_init(void) {
    ppg_state_t *chip = (ppg_state_t *)calloc(1, sizeof(ppg_state_t));
    if (!chip) return;

    chip->beat_in = pin_init("BEAT_IN", INPUT_PULLDOWN);
    chip->mode0 = pin_init("MODE0", INPUT_PULLDOWN);
    chip->mode1 = pin_init("MODE1", INPUT_PULLDOWN);
    chip->mode2 = pin_init("MODE2", INPUT_PULLDOWN);
    chip->rng = 0xC001D00Du;
    chip->rr_seconds = 60.0f / 75.0f;
    chip->sample_rate = DEFAULT_SAMPLE_RATE;
    chip->sample_period_us = DEFAULT_SAMPLE_PERIOD_US;
    chip->baseline_phase = 0.7f;

    chip->spo2_attr = attr_init("spo2Target", 98);
    chip->perfusion_attr = attr_init_float("perfusion", 1.0f);
    chip->motion_attr = attr_init_float("motionNoise", 0.10f);

    // Relevant MAX30102 defaults for a usable SpO2-mode simulation.
    chip->regs[0x08] = 0x0Fu; // FIFO sample averaging / rollover defaults
    chip->regs[0x09] = 0x03u; // SpO2 mode, RED + IR
    chip->regs[0x0A] = 0x27u; // 100 Hz, 18-bit ADC (typical driver setting)
    chip->regs[0x0C] = 0x24u; // LED1 pulse amplitude register (model-independent)
    chip->regs[0x0D] = 0x24u; // LED2 pulse amplitude register
    chip->regs[0xFF] = 0x15u;
    chip->regs[0xFE] = 0x03u;

    const pin_watch_config_t beat_watch = {
        .user_data = chip,
        .edge = RISING,
        .pin_change = on_beat_change,
    };
    pin_watch(chip->beat_in, &beat_watch);

    const i2c_config_t i2c_config = {
        .address = I2C_ADDRESS,
        .scl = pin_init("SCL", INPUT_PULLUP),
        .sda = pin_init("SDA", INPUT_PULLUP),
        .connect = on_i2c_connect,
        .read = on_i2c_read,
        .write = on_i2c_write,
        .disconnect = on_i2c_disconnect,
        .user_data = chip,
    };
    chip->i2c = i2c_init(&i2c_config);

    const timer_config_t timer_config = {
        .user_data = chip,
        .callback = on_timer,
    };
    chip->timer = timer_init(&timer_config);
    timer_start(chip->timer, chip->sample_period_us, true);

    printf("WearTwin PPG simulator ready at I2C 0x57; initial SpO2 target=98%%\n");
}
