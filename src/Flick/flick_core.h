#pragma once
#ifndef FLICK_CORE_H
#define FLICK_CORE_H

#include "daisy.h"
#include "daisysp.h"
#include "flick_oscillator.h"
#include "Dattorro.hpp"
#include <math.h>

using clevelandmusicco::FlickOscillator;
using daisy::Parameter;
using daisysp::DelayLine;
using daisysp::fonepole;

/// Increment this when changing the settings struct so the software will know
/// to reset to defaults if this ever changes.
constexpr int SETTINGS_VERSION = 4;

// Audio configuration constants
constexpr size_t MAX_DELAY_SIZE = static_cast<size_t>(48000.0f * 2.0f); // 2 second max delay assuming 48k

// Tremolo constants
constexpr float TREMOLO_SPEED_MIN = 0.2f;   // Minimum tremolo speed in Hz
constexpr float TREMOLO_SPEED_MAX = 16.0f;  // Maximum tremolo speed in Hz
constexpr float TREMOLO_DEPTH_SCALE = 1.0f; // Scale factor for tremolo depth
constexpr float TREMOLO_LED_BRIGHTNESS = 0.4f; // LED brightness when only tremolo is active

// Delay constants
constexpr float DELAY_TIME_MIN_SECONDS = 0.02f;  // Minimum delay time (20ms - enables doubling/slapback)
constexpr float DELAY_WET_MIX_ATTENUATION = 0.333f; // Attenuation for wet delay signal
constexpr float DELAY_DRY_WET_PERCENT_MAX = 100.0f; // Max value for dry/wet percentage

// LED constants
constexpr float TAP_TEMPO_BLINK_DUTY_CYCLE = 0.1f; // 10% duty cycle for tap tempo LED

// Tap tempo constants
constexpr uint32_t TAP_TEMPO_TIMEOUT_MS = 5000;     // Exit tap tempo after 5 seconds
constexpr uint32_t TAP_TEMPO_MIN_INTERVAL_MS = 20;  // Min 20ms = 3000 BPM (enables doubling/slapback)
constexpr uint32_t TAP_TEMPO_MAX_INTERVAL_MS = 4000; // Max 4 seconds = 15 BPM
constexpr float MS_PER_SECOND = 1000.0f;            // Milliseconds per second conversion

// Harmonic tremolo state (filter cutoffs taken from Fender 6G12-A schematic)
constexpr float HARMONIC_TREMOLO_LPF_CUTOFF = 144.0f; // 220K and 5nF LPF
constexpr float HARMONIC_TREMOLO_HPF_CUTOFF = 636.0f; // 1M and 250pF HPF

enum PedalMode {
  PEDAL_MODE_NORMAL,
  PEDAL_MODE_EDIT_REVERB,     // Edit mode activated by long-press of the left foot switch
  PEDAL_MODE_EDIT_MONO_STEREO,// Edit mode activated by long-press of the right foot switch
  PEDAL_MODE_TAP_TEMPO        // Tap tempo mode activated by double-press of left foot switch
};

enum MonoStereoMode {                       // Controlled by Toggle Switch 3
  MS_MODE_MIMO, // Mono In, Mono Out        // TOGGLESWITCH_DOWN
  MS_MODE_MISO, // Mono In, Stereo Out      // TOGGLESWITCH_MIDDLE
  MS_MODE_SISO  // Stereo In, Stereo Out    // TOGGLESWITCH_UP
};

enum DelaySubdivision {
  DELAY_SUBDIV_QUARTER_TRIPLET,  // 0.6666x multiplier (DOWN) - 2/3 of quarter note
  DELAY_SUBDIV_NORMAL,            // 1.0x multiplier (MIDDLE) - quarter note
  DELAY_SUBDIV_DOTTED_EIGHTH,    // 0.75x multiplier (UP) - 3/4 of quarter note
};

enum TremoloMode {
  TREMOLO_HARMONIC,    // Harmonic tremolo (DOWN)
  TREMOLO_SINE,        // Sine wave tremolo (MIDDLE)
  TREMOLO_SQUARE,      // Square wave tremolo (UP)
};

enum ReverbKnobMode {
  REVERB_KNOB_ALL_DRY,
  REVERB_KNOB_DRY_WET_MIX,
  REVERB_KNOB_ALL_WET,
};

enum TremDelMakeUpGain {
  MAKEUP_GAIN_NONE,
  MAKEUP_GAIN_NORMAL,
  MAKEUP_GAIN_HEAVY,
};

// Helper structures for soft takeover functionality
// Used to prevent parameter jumps when entering edit modes or switching control sources

// Tracks knob position and implements soft takeover with movement threshold
struct KnobTakeover {
  float entry_value;              // Knob position when control was suspended
  bool taken_over;                // Whether knob has moved enough to take control

  // Default constructor
  KnobTakeover() : entry_value(0.0f), taken_over(false) {}

  // Reset takeover state and capture current knob position
  void capture(float current_value) {
    entry_value = current_value;
    taken_over = false;
  }

  // Check if knob has moved enough to take over control
  // Returns true if knob is actively controlling (either already taken over or just took over)
  bool checkTakeover(float current_value, float threshold = 0.05f) {
    if (!taken_over) {
      if (fabs(current_value - entry_value) > threshold) {
        taken_over = true;
        return true;  // Just taken over - knob now controls
      }
      return false;   // Not yet taken over - knob doesn't control
    }
    return true;      // Already taken over - knob controls
  }
};

// Tracks switch position and detects changes
struct SwitchChangeDetector {
  int entry_position;                    // Switch position when tracking started
  bool changed;                         // Whether switch has been moved from entry position

  // Default constructor
  SwitchChangeDetector() : entry_position(0), changed(false) {}

  // Reset change state and capture current switch position
  void capture(int current_position) {
    entry_position = current_position;
    changed = false;
  }

  // Check if switch position has changed from entry position
  // Returns true if switch has been moved (either just changed or previously changed)
  bool checkChange(int current_position) {
    if (!changed && current_position != entry_position) {
      changed = true;
      return true;    // Just changed
    }
    return changed;   // Return current changed state
  }
};

// Persistent Settings
struct Settings {
  int version; // Version of the settings struct
  float decay;
  float diffusion;
  float input_cutoff_freq;
  float tank_cutoff_freq;
  float tank_mod_speed;
  float tank_mod_depth;
  float tank_mod_shape;
  float pre_delay;
  int mono_stereo_mode;
  int makeup_gain_mode;       // Makeup gain setting
  bool bypass_reverb;        // Reverb bypass state (true = bypassed)
  bool bypass_delay;         // Delay bypass state (true = bypassed)
  bool bypass_tremolo;       // Tremolo bypass state (true = bypassed)

  //Overloading the != operator
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

struct Delay {
  DelayLine<float, MAX_DELAY_SIZE> *del;
  float current_delay;
  float delay_target;
  float feedback;

  float Process(float in) {
    // set delay times
    fonepole(current_delay, delay_target, 0.0002f);
    del->SetDelay(current_delay);

    float read = del->Read();
    del->Write((feedback * read) + in);

    return read;
  }
};

struct LowPassFilter {
    float alpha;
    float prev_y = 0.0f;

    void Init(float fc, float fs) {
        alpha = expf(-2.0f * M_PI * fc / fs);
    }

    float Process(float x) {
        float y = (1.0f - alpha) * x + alpha * prev_y;
        prev_y = y;
        return y;
    }
};

struct HighPassFilter {
    float alpha;
    float prev_x = 0.0f;
    float prev_y = 0.0f;

    void Init(float fc, float fs) {
        alpha = expf(-2.0f * M_PI * fc / fs);
    }

    float Process(float x) {
        float y = (1.0f + alpha) * 0.5f * (x - prev_x) + alpha * prev_y;
        prev_x = x;
        prev_y = y;
        return y;
    }
};

enum Switch3Function {
    SWITCH3_DELAY_SUBDIVISION,
    SWITCH3_MAKEUP_GAIN
};

class FlickCore {
public:
    FlickCore();
    ~FlickCore();

    // Configuration
    void SetSwitch3Function(Switch3Function function) { switch3_function_ = function; }

    // Initialize with delay lines and sample rate
    void Init(float sample_rate, 
              DelayLine<float, MAX_DELAY_SIZE>* delay_line_l, 
              DelayLine<float, MAX_DELAY_SIZE>* delay_line_r);

    // Initialize the parameter objects with hardware controls
    void InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2, 
                      daisy::AnalogControl* k3, daisy::AnalogControl* k4, 
                      daisy::AnalogControl* k5, daisy::AnalogControl* k6);

    // Main processing delegates
    // sw1..3 are the raw switch positions (0, 1, 2 typically)
    void ProcessControls(int sw1, int sw2, int sw3);
    
    // Process a block of audio
    // We use float* pointers for channels.
    void ProcessAudio(const float* in_l, const float* in_r, float* out_l, float* out_r, size_t size);

    // Event handlers
    // footswitch_index: 0 for Left, 1 for Right
    void HandleNormalPress(int footswitch_index);
    void HandleDoublePress(int footswitch_index);
    void HandleLongPress(int footswitch_index);
    
    void CheckTapTempoTimeout();

    // State Accessors
    float GetLedLeftBrightness() const { return led_left_brightness_; }
    float GetLedRightBrightness() const { return led_right_brightness_; }
    
    // Settings Management
    Settings GetSettings() const;
    void SetSettings(const Settings& settings);
    bool ShouldSaveSettings() const { return trigger_settings_save_; }
    void ClearSaveFlag() { trigger_settings_save_ = false; }
    
    // Factory Reset Helpers
    bool IsFactoryResetMode() const { return is_factory_reset_mode_; }
    void SetFactoryResetMode(bool mode) { is_factory_reset_mode_ = mode; }
    void UpdateFactoryReset(); // Internal logic driven by control processing
    
    // Restore logic
    void RestoreDefaults();

private:
    float sample_rate_;
    
    bool trigger_settings_save_;

    FlickOscillator osc;
    float dc_offset;
    
    // Delay lines 
    DelayLine<float, MAX_DELAY_SIZE> *delMemL;
    DelayLine<float, MAX_DELAY_SIZE> *delMemR;

    Dattorro verb;
    
    // State
    PedalMode pedal_mode;
    MonoStereoMode mono_stereo_mode;
    TremDelMakeUpGain current_makeup_gain;
    TremoloMode tremolo_mode_; // Cached active tremolo mode

    // Parameters
    Parameter p_verb_amt;
    Parameter p_trem_speed, p_trem_depth;
    Parameter p_delay_time, p_delay_feedback, p_delay_amt;
    Parameter p_knob_1, p_knob_2, p_knob_3, p_knob_4, p_knob_5, p_knob_6;

    Delay delayL;
    Delay delayR;
    int delay_drywet;

    // Bypass vars
    bool bypass_verb;
    bool bypass_trem;
    bool bypass_delay;
    
    // LED state
    float led_left_brightness_;
    float led_right_brightness_;

    // Tap tempo state
    bool tap_tempo_active;
    uint32_t tap_tempo_last_tap_time;
    uint32_t tap_tempo_interval_ms;
    float tap_tempo_delay_samples;
    bool tap_tempo_controls_delay;
    float tap_tempo_tremolo_freq_hz;
    bool tap_tempo_controls_tremolo;
    float TAP_TEMPO_SAMPLES_MIN;
    float TAP_TEMPO_SAMPLES_MAX;

    // Soft takeover helpers
    KnobTakeover tap_tempo_delay_knob_takeover;
    KnobTakeover tap_tempo_tremolo_knob_takeover;
    
    KnobTakeover reverb_edit_wet_amount_knob;
    KnobTakeover reverb_edit_pre_delay_knob;
    KnobTakeover reverb_edit_decay_knob;
    KnobTakeover reverb_edit_diffusion_knob;
    KnobTakeover reverb_edit_input_cut_knob;
    KnobTakeover reverb_edit_tank_cut_knob;
    SwitchChangeDetector reverb_edit_mod_speed_switch;
    SwitchChangeDetector reverb_edit_mod_depth_switch;
    SwitchChangeDetector reverb_edit_mod_shape_switch;

    float master_delay_time_samples;

    // Harmonic tremolo filters
    LowPassFilter low_pass_l;
    LowPassFilter low_pass_r;
    HighPassFilter high_pass_l;
    HighPassFilter high_pass_r;

    // Reverb internal vars
    bool plate_diffusion_enabled;
    float plate_pre_delay;
    float plate_dry;
    float plate_wet;
    float plate_decay;
    float plate_time_scale;
    float plate_tank_diffusion;
    float plate_input_damp_low;
    float plate_input_damp_high;
    float plate_tank_damp_low;
    float plate_tank_damp_high;
    float plate_tank_mod_speed;
    float plate_tank_mod_depth;
    float plate_tank_mod_shape;

    float reverb_dry_scale_factor;
    float reverb_reverse_scale_factor;

    // Factory Reset State
    bool is_factory_reset_mode_;
    int factory_reset_stage_;
    uint32_t reset_blink_interval_;
    uint32_t last_led_toggle_time_;
    bool led_toggle_state_;

    // Private helpers
    void updateReverbScales(MonoStereoMode mode);
    void restoreReverbSettings();
    void restoreMonoStereoSettings();
    void saveBypassStates();
    void saveMonoStereoSettings();
    void enterTapTempoMode();
    void exitTapTempoMode();
    void handleTapTempoTap();
    void applyDelaySubdivisionAndSetTargets(float masterDelaySamples, int switch3_pos);

    // Constants we likely want to keep near the usage but are private
    float hardLimit100_(const float &x);

    Settings saved_state_;
    
    Switch3Function switch3_function_;
};

#endif // FLICK_CORE_H
