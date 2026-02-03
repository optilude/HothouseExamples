#pragma once
#ifndef FLICK_CORE_H
#define FLICK_CORE_H

#include "daisy.h"
#include "daisysp.h"
#include "flick_reverb_effect.h"
#include "flick_tremolo_effect.h"
#include "flick_delay_effect.h"

using daisy::Parameter;
using daisysp::DelayLine;

/// Increment this when changing the settings struct so the software will know
/// to reset to defaults if this ever changes.
constexpr int SETTINGS_VERSION = 4;

// Re-export delay size for external use
constexpr size_t MAX_DELAY_SIZE = flick::kMaxDelaySize;

// LED constants
constexpr float TAP_TEMPO_BLINK_DUTY_CYCLE = 0.1f;

// Tap tempo constants
constexpr uint32_t TAP_TEMPO_TIMEOUT_MS = 5000;
constexpr uint32_t TAP_TEMPO_MIN_INTERVAL_MS = 20;
constexpr uint32_t TAP_TEMPO_MAX_INTERVAL_MS = 4000;
constexpr float MS_PER_SECOND = 1000.0f;

/**
 * @brief Pedal operating modes
 */
enum PedalMode {
    PEDAL_MODE_NORMAL,           // Standard operation
    PEDAL_MODE_EDIT_REVERB,      // Reverb parameter editing
    PEDAL_MODE_EDIT_MONO_STEREO, // Mono/stereo and makeup gain editing
    PEDAL_MODE_TAP_TEMPO         // Tap tempo entry
};

// Re-export effect types for external use
using MonoStereoMode = flick::ReverbEffect::MonoStereoMode;
constexpr auto MS_MODE_MIMO = flick::ReverbEffect::MS_MODE_MIMO;
constexpr auto MS_MODE_MISO = flick::ReverbEffect::MS_MODE_MISO;
constexpr auto MS_MODE_SISO = flick::ReverbEffect::MS_MODE_SISO;

using TremDelMakeUpGain = flick::TremoloEffect::MakeupGain;
constexpr auto MAKEUP_GAIN_NONE = flick::TremoloEffect::MAKEUP_NONE;
constexpr auto MAKEUP_GAIN_NORMAL = flick::TremoloEffect::MAKEUP_NORMAL;
constexpr auto MAKEUP_GAIN_HEAVY = flick::TremoloEffect::MAKEUP_HEAVY;

/**
 * @brief Persistent settings structure
 */
struct Settings {
    int version;
    float decay;
    float diffusion;
    float input_cutoff_freq;
    float tank_cutoff_freq;
    float tank_mod_speed;
    float tank_mod_depth;
    float tank_mod_shape;
    float pre_delay;
    int mono_stereo_mode;
    int makeup_gain_mode;
    bool bypass_reverb;
    bool bypass_delay;
    bool bypass_tremolo;

    bool operator!=(const Settings& a) const {
        return !(
            a.version == version &&
            a.decay == decay &&
            a.diffusion == diffusion &&
            a.input_cutoff_freq == input_cutoff_freq &&
            a.tank_cutoff_freq == tank_cutoff_freq &&
            a.tank_mod_speed == tank_mod_speed &&
            a.tank_mod_depth == tank_mod_depth &&
            a.tank_mod_shape == tank_mod_shape &&
            a.pre_delay == pre_delay &&
            a.mono_stereo_mode == mono_stereo_mode &&
            a.makeup_gain_mode == makeup_gain_mode &&
            a.bypass_reverb == bypass_reverb &&
            a.bypass_delay == bypass_delay &&
            a.bypass_tremolo == bypass_tremolo
        );
    }
};

/**
 * @brief Core pedal logic orchestrating the three effects
 * 
 * FlickCore coordinates:
 * - Effect processing order (Delay -> Tremolo -> Reverb)
 * - Mode transitions (Normal, Edit Reverb, Edit Mono/Stereo, Tap Tempo)
 * - Footswitch handling
 * - LED state management
 * - Settings persistence
 * 
 * The actual effect processing is delegated to the individual effect classes.
 */
class FlickCore {
public:
    FlickCore();
    ~FlickCore();

    /**
     * @brief Initialize with delay lines and sample rate
     * @param sample_rate Audio sample rate (typically 48000)
     * @param delay_line_l Left delay line (must be in SDRAM)
     * @param delay_line_r Right delay line (must be in SDRAM)
     */
    void Init(float sample_rate,
              DelayLine<float, MAX_DELAY_SIZE>* delay_line_l,
              DelayLine<float, MAX_DELAY_SIZE>* delay_line_r);

    /**
     * @brief Initialize parameter objects with hardware controls
     */
    void InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2,
                      daisy::AnalogControl* k3, daisy::AnalogControl* k4,
                      daisy::AnalogControl* k5, daisy::AnalogControl* k6);

    /**
     * @brief Process controls once per audio block
     * @param sw1 Toggle switch 1 position (0=DOWN, 1=MIDDLE, 2=UP)
     * @param sw2 Toggle switch 2 position
     * @param sw3 Toggle switch 3 position
     */
    void ProcessControls(int sw1, int sw2, int sw3);

    /**
     * @brief Process a block of audio
     */
    void ProcessAudio(const float* in_l, const float* in_r,
                      float* out_l, float* out_r, size_t size);

    /**
     * @brief Handle footswitch events
     * @param footswitch_index 0 for left, 1 for right
     */
    void HandleNormalPress(int footswitch_index);
    void HandleDoublePress(int footswitch_index);
    void HandleLongPress(int footswitch_index);

    /**
     * @brief Check for tap tempo timeout (call from main loop)
     */
    void CheckTapTempoTimeout();

    // LED state
    float GetLedLeftBrightness() const { return led_left_brightness_; }
    float GetLedRightBrightness() const { return led_right_brightness_; }

    // Settings management
    Settings GetSettings() const;
    void SetSettings(const Settings& settings);
    bool ShouldSaveSettings() const { return trigger_settings_save_; }
    void ClearSaveFlag() { trigger_settings_save_ = false; }

    // Factory reset
    bool IsFactoryResetMode() const { return is_factory_reset_mode_; }
    void SetFactoryResetMode(bool mode) { is_factory_reset_mode_ = mode; }
    void UpdateFactoryReset();

    void RestoreDefaults();

    // Access to current mode (for external checks like bootloader)
    PedalMode GetPedalMode() const { return pedal_mode_; }

    // Access to effects for advanced control
    flick::ReverbEffect& GetReverbEffect() { return reverb_; }
    flick::TremoloEffect& GetTremoloEffect() { return tremolo_; }
    flick::DelayEffect& GetDelayEffect() { return delay_; }

private:
    // Update LED states based on current mode
    void updateLeds();
    
    // Mode transitions
    void enterTapTempoMode();
    void exitTapTempoMode();
    void handleTapTempoTap();
    
    // Settings helpers
    void saveBypassStates();
    void saveMonoStereoSettings();
    void restoreMonoStereoSettings();

    // Sample rate
    float sample_rate_;
    
    // Effects
    flick::ReverbEffect reverb_;
    flick::TremoloEffect tremolo_;
    flick::DelayEffect delay_;

    // Parameters for factory reset and generic processing
    Parameter p_knob_1_;

    // Operating mode
    PedalMode pedal_mode_;

    // LED state
    float led_left_brightness_;
    float led_right_brightness_;

    // Tap tempo state
    bool tap_tempo_active_;
    uint32_t tap_tempo_last_tap_time_;
    uint32_t tap_tempo_interval_ms_;
    float tap_tempo_delay_samples_;
    bool tap_tempo_controls_delay_;
    float tap_tempo_tremolo_freq_hz_;
    bool tap_tempo_controls_tremolo_;
    float tap_tempo_samples_min_;
    float tap_tempo_samples_max_;

    // Settings persistence
    bool trigger_settings_save_;
    Settings saved_state_;
    TremDelMakeUpGain current_makeup_gain_;

    // Factory reset state
    bool is_factory_reset_mode_;
    int factory_reset_stage_;
    uint32_t reset_blink_interval_;
    uint32_t last_led_toggle_time_;
    bool led_toggle_state_;
};

#endif // FLICK_CORE_H
