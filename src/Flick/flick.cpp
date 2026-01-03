// Flick for Hothouse DIY DSP Platform
// Copyright (C) 2024 Boyd Timothy <btimothy@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daisy.h"
#include "daisysp.h"
#include "flick_oscillator.h"
#include "hothouse.h"
#include "Dattorro.hpp"
#include <math.h>

using clevelandmusicco::FlickOscillator;
using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::Led;
using daisy::Parameter;
using daisy::PersistentStorage;
using daisy::SaiHandle;
using daisy::System;
using daisysp::DelayLine;
using daisysp::fonepole;

/// Increment this when changing the settings struct so the software will know
/// to reset to defaults if this ever changes.
#define SETTINGS_VERSION 3

Hothouse hw;

#define MAX_DELAY static_cast<size_t>(48000 * 2.0f) // 4 second max delay

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
  DELAY_SUBDIV_QUARTER_TRIPLET,  // 1.333x multiplier (DOWN)
  DELAY_SUBDIV_NORMAL,            // 1.0x multiplier (MIDDLE)
  DELAY_SUBDIV_DOTTED_EIGHTH,    // 1.5x multiplier (UP)
};

enum TremoloMode {
  TREMOLO_HARMONIC,    // Harmonic tremolo (DOWN)
  TREMOLO_SINE,        // Sine wave tremolo (MIDDLE)
  TREMOLO_SQUARE,      // Square wave tremolo (UP)
};

// Tap tempo constants
const uint32_t TAP_TEMPO_TIMEOUT_MS = 5000;     // Exit tap tempo after 5 seconds
const uint32_t TAP_TEMPO_MIN_INTERVAL_MS = 50;  // Min 50ms = 1200 BPM (practical limit)
const uint32_t TAP_TEMPO_MAX_INTERVAL_MS = 4000; // Max 4 seconds = 15 BPM
const float TAP_TEMPO_SAMPLES_MIN = 2400.0f;    // 50ms at 48kHz
const float TAP_TEMPO_SAMPLES_MAX = 192000.0f;  // 4s at 48kHz

// DFU mode - both switches
const uint32_t DFU_BOTH_SWITCHES_HOLD_TIME_MS = 5000;  // 5 seconds

// Persistent Settings
struct Settings {
  int version; // Version of the settings struct
  float decay;
  float diffusion;
  float inputCutoffFreq;
  float tankCutoffFreq;
  float tankModSpeed;
  float tankModDepth;
  float tankModShape;
  float preDelay;
  int monoStereoMode;
  int makeupGainMode;       // Makeup gain setting

	//Overloading the != operator
	//This is necessary as this operator is used in the PersistentStorage source code
	bool operator!=(const Settings& a) const {
    return !(
      a.version == version &&
      a.decay == decay &&
      a.diffusion == diffusion &&
      a.inputCutoffFreq == inputCutoffFreq &&
      a.tankCutoffFreq == tankCutoffFreq &&
      a.tankModSpeed == tankModSpeed &&
      a.tankModDepth == tankModDepth &&
      a.tankModShape == tankModShape &&
      a.preDelay == preDelay &&
      a.monoStereoMode == monoStereoMode &&
      a.makeupGainMode == makeupGainMode
    );
  }
};

//Persistent Storage Declaration. Using type Settings and passed the devices qspi handle
PersistentStorage<Settings> saved_settings(hw.seed.qspi);

FlickOscillator osc;
float dc_os = 0;

DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS del_mem_l;
DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS del_mem_r;

Dattorro verb(48000, 16, 4.0);
PedalMode pedal_mode = PEDAL_MODE_NORMAL;
MonoStereoMode mono_stereo_mode = MS_MODE_MIMO;

Parameter p_verb_amt;
Parameter p_trem_speed, p_trem_depth;
Parameter p_delay_time, p_delay_feedback, p_delay_amt;

Parameter p_knob_1, p_knob_2, p_knob_3, p_knob_4, p_knob_5, p_knob_6;

struct Delay {
  DelayLine<float, MAX_DELAY> *del;
  float currentDelay;
  float delayTarget;
  float feedback;

  float Process(float in) {
    // set delay times
    fonepole(currentDelay, delayTarget, 0.0002f);
    del->SetDelay(currentDelay);

    float read = del->Read();
    del->Write((feedback * read) + in);

    return read;
  }
};

enum ReverbKnobMode {
  REVERB_KNOB_ALL_DRY,
  REVERB_KNOB_DRY_WET_MIX,
  REVERB_KNOB_ALL_WET,
};

enum TremDelMakeUpGain {
  TV_MAKEUP_GAIN_NONE,
  TV_MAKEUP_GAIN_NORMAL,
  TV_MAKEUP_GAIN_HEAVY,
};

constexpr ReverbKnobMode K_REVERB_KNOB_MAP[] = {
  REVERB_KNOB_ALL_WET,                        // UP
  REVERB_KNOB_DRY_WET_MIX,                    // MIDDLE
  REVERB_KNOB_ALL_DRY,                        // DOWN
};

constexpr TremDelMakeUpGain K_MAKEUP_GAIN_MAP[] = {
  TV_MAKEUP_GAIN_HEAVY,                       // UP
  TV_MAKEUP_GAIN_NORMAL,                      // MIDDLE
  TV_MAKEUP_GAIN_NONE,                        // DOWN
};

constexpr TremoloMode K_TREMOLO_MODE_MAP[] = {
    TREMOLO_SQUARE,     // UP
    TREMOLO_SINE,       // MIDDLE
    TREMOLO_HARMONIC,   // DOWN
};

constexpr DelaySubdivision K_DELAY_SUBDIVISION_MAP[] = {
  DELAY_SUBDIV_DOTTED_EIGHTH,     // UP (1.5x)
  DELAY_SUBDIV_NORMAL,            // MIDDLE (1.0x)
  DELAY_SUBDIV_QUARTER_TRIPLET,  // DOWN (1.333x)
};

Delay delay_l;
Delay delay_r;
int delay_drywet;

float reverb_tone;
float reverb_feedback;
float reverb_sploodge;

// Bypass vars
Led led_left, led_right;
bool bypass_verb = true;
bool bypass_trem = true;
bool bypass_delay = true;

// Tap tempo state
bool tap_tempo_active = false;
uint32_t tap_tempo_last_tap_time = 0;
uint32_t tap_tempo_interval_ms = 0;
float tap_tempo_delay_samples = 0.0f;
bool tap_tempo_controls_delay = false;  // True when tap tempo overrides knob

// Knob takeover for KNOB_4 (delay time)
float delay_time_last_value = 0.0f;
const float KNOB_TAKEOVER_THRESHOLD = 0.05f;  // 5% movement required

// Master delay time (before subdivision multiplier)
float master_delay_time_samples = 0.0f;

// DFU mode detection
uint32_t both_switches_press_start_time = 0;
bool both_switches_pressed = false;

// Current makeup gain setting (persisted)
TremDelMakeUpGain current_makeup_gain = TV_MAKEUP_GAIN_NORMAL;

// Harmonic tremolo state
using daisysp::Svf;
Svf harmonic_filter_l;  // State variable filter for crossover
Svf harmonic_filter_r;
const float HARMONIC_TREMOLO_CROSSOVER_FREQ = 800.0f;  // Hz

// Reverb vars
bool plate_diffusion_enabled = true;
float plate_pre_delay = 0.;

float plate_delay = 0.0;

float plate_dry = 1.0;
float plate_wet = 0.5;

float plate_decay = 0.8;
float plate_time_scale = 1.007500;

float plate_tank_diffusion = 0.85;

  /**
   * Good Defaults
   * Lo Pitch: .287 (2.87) = 100Hz: 440 * (2^(2.87-5))
   * InputFilterHighCutoffPitch: 0.77 (7.77) is approx 3000Hz
   * TankFilterHighCutFrequency: 0.8 (8.0) is 3520Hz
   * 0.9507 is approx 10kHz
   * 
   * mod speed: 0.5
   * mod depth: 0.5
   * mod shape: 0.75
   */

// The damping values appear to be want to be between 0 and 10
float plate_input_damp_low = 2.87; // approx 100Hz
float plate_input_damp_high = 7.25;

float plate_tank_damp_low = 2.87; // approx 100Hz
float plate_tank_damp_high = 7.25;

float plate_tank_mod_speed = 0.1;
float plate_tank_mod_depth = 0.1;
float plate_tank_mod_shape = 0.25;

const float MINUS_18DB_GAIN = 0.12589254;
const float MINUS_20DB_GAIN = 0.1;

float left_input = 0.;
float right_input = 0.;
float left_output = 0.;
float right_output = 0.;
float reverb_dry_scale_factor = 1.0;
float reverb_reverse_scale_factor = 1.0;

float input_amplification = 1.0; // This isn't really used yet

bool trigger_settings_save = false;

/// @brief Used at startup to control a factory reset.
///
/// This gets set to true in `main()` if footswitch 2 is depressed at boot.
/// The LED lights will start flashing alternatively. To exit this mode without
/// making any changes, press either footswitch.
///
/// To reset, rotate knob_1 to 100%, to 0%, to 100%, and back to 0%. This will
/// restore all defaults and then go into normal pedal mode.
bool is_factory_reset_mode = false;

/// @brief Tracks the stage of knob_1 rotation in factory reset mode.
///
/// 0: User must rotate knob_1 to 100% to advance to the next stage.
/// 1: User must rotate knob_1 to 0% to advance to the next stage.
/// 2: User must rotate knob_1 to 100% to advance to the next stage.
/// 3: User must rotate knob_1 to 0% to complete the factory reset.
int factory_reset_stage = 0;

inline void updateReverbScales(MonoStereoMode mode) {
  switch (mode) {
    case MS_MODE_MIMO:
      reverb_dry_scale_factor = 5.0f; // Make the signal stronger for MIMO mode
      reverb_reverse_scale_factor = 0.2f;
      break;
    case MS_MODE_MISO:
    case MS_MODE_SISO:
      reverb_dry_scale_factor = 2.5f; // MISO and SISO modes
      reverb_reverse_scale_factor = 0.4f;
      break;
  }
}

void loadSettings() {

	// Reference to local copy of settings stored in flash
	Settings &localSettings = saved_settings.GetSettings();

  int savedVersion = localSettings.version;

  if (savedVersion != SETTINGS_VERSION) {
    // Something has changed. Load defaults!
    saved_settings.RestoreDefaults();
    loadSettings();
    return;
  }

  plate_decay = localSettings.decay;
  plate_tank_diffusion = localSettings.diffusion;
  plate_input_damp_high = localSettings.inputCutoffFreq;
  plate_tank_damp_high = localSettings.tankCutoffFreq;
  plate_tank_mod_speed = localSettings.tankModSpeed;
  plate_tank_mod_depth = localSettings.tankModDepth;
  plate_tank_mod_shape = localSettings.tankModShape;
  plate_pre_delay = localSettings.preDelay;
  mono_stereo_mode = static_cast<MonoStereoMode>(localSettings.monoStereoMode);
  updateReverbScales(mono_stereo_mode);

  // Load makeup gain setting
  current_makeup_gain = static_cast<TremDelMakeUpGain>(localSettings.makeupGainMode);

  // Validate makeup gain value
  if (current_makeup_gain < TV_MAKEUP_GAIN_NONE ||
      current_makeup_gain > TV_MAKEUP_GAIN_HEAVY) {
    current_makeup_gain = TV_MAKEUP_GAIN_NORMAL;
  }

  verb.setPreDelay(plate_pre_delay);
  verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
  verb.setDecay(plate_decay);
  verb.setTankDiffusion(plate_tank_diffusion);
  verb.setTankFilterHighCutFrequency(plate_tank_damp_high);
  verb.setTankModSpeed(plate_tank_mod_speed * 8);
  verb.setTankModDepth(plate_tank_mod_depth * 15);
  verb.setTankModShape(plate_tank_mod_shape);
}

void saveSettings() {
	//Reference to local copy of settings stored in flash
	Settings &localSettings = saved_settings.GetSettings();

  localSettings.version = SETTINGS_VERSION;
  localSettings.decay = plate_decay;
  localSettings.diffusion = plate_tank_diffusion;
  localSettings.inputCutoffFreq = plate_input_damp_high;
  localSettings.tankCutoffFreq = plate_tank_damp_high;
  localSettings.tankModSpeed = plate_tank_mod_speed;
  localSettings.tankModDepth = plate_tank_mod_depth;
  localSettings.tankModShape = plate_tank_mod_shape;
  localSettings.preDelay = plate_pre_delay;

	trigger_settings_save = true;
}

void saveMonoStereoSettings() {
  Settings &localSettings = saved_settings.GetSettings();

  localSettings.monoStereoMode = mono_stereo_mode;
  localSettings.makeupGainMode = current_makeup_gain;  // NEW: Save makeup gain

  trigger_settings_save = true;
}

/// @brief Restore the reverb settings from the saved settings.
void restoreReverbSettings() {
	Settings &localSettings = saved_settings.GetSettings();

  plate_decay = localSettings.decay;
  plate_tank_diffusion = localSettings.diffusion;
  plate_input_damp_high = localSettings.inputCutoffFreq;
  plate_tank_damp_high = localSettings.tankCutoffFreq;
  plate_tank_mod_speed = localSettings.tankModSpeed;
  plate_tank_mod_depth = localSettings.tankModDepth;
  plate_tank_mod_shape = localSettings.tankModShape;
  plate_pre_delay = localSettings.preDelay;

  verb.setDecay(plate_decay);
  verb.setTankDiffusion(plate_tank_diffusion);
  verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
  verb.setTankFilterHighCutFrequency(plate_tank_damp_high);

  verb.setTankModSpeed(plate_tank_mod_speed * 8);
  verb.setTankModDepth(plate_tank_mod_depth * 15);
  verb.setTankModShape(plate_tank_mod_shape);
  verb.setPreDelay(plate_pre_delay);    
}

/// @brief Restore the mono-stereo settings from the saved settings.
void restoreMonoStereoSettings() {
  Settings &localSettings = saved_settings.GetSettings();

  mono_stereo_mode = static_cast<MonoStereoMode>(localSettings.monoStereoMode);
  current_makeup_gain = static_cast<TremDelMakeUpGain>(localSettings.makeupGainMode);  // NEW: Restore makeup gain
  updateReverbScales(mono_stereo_mode);
}

// Forward declarations for tap tempo functions
void enterTapTempoMode();
void exitTapTempoMode();
void handleTapTempoTap();
void checkTapTempoTimeout();
void checkDfuModeBothSwitches();

void handleNormalPress(Hothouse::Switches footswitch) {
  // Handle tap tempo mode
  if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    if (footswitch == Hothouse::FOOTSWITCH_1) {
      // Exit tap tempo mode
      exitTapTempoMode();
      return;
    } else if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Tap the tempo
      handleTapTempoTap();
      return;
    }
  }

  // Handle edit reverb mode
  if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Only save the settings if the RIGHT footswitch is pressed in edit mode.
    // The LEFT footswitch is used to exit edit mode without saving.
    if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Save the settings
      saveSettings();
    } else {
      restoreReverbSettings();
    }
    pedal_mode = PEDAL_MODE_NORMAL;
    return;
  }

  // Handle mono-stereo edit mode
  if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Only save the settings if the RIGHT footswitch is pressed in mono-stereo
    // edit mode. The LEFT footswitch is used to exit mono-stereo edit mode
    // without saving.
    if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Save the mono-stereo settings
      saveMonoStereoSettings();
    } else {
      restoreMonoStereoSettings();
    }
    pedal_mode = PEDAL_MODE_NORMAL;
    return;
  }

  // Normal mode bypass toggles
  if (footswitch == Hothouse::FOOTSWITCH_1) {
    bypass_verb = !bypass_verb;

    if (bypass_verb) {
      // Clear the reverb tails when the reverb is bypassed so if you
      // turn it back on, it starts fresh and doesn't sound weird.
      verb.clear();
    }
  } else {
    bypass_delay = !bypass_delay;
  }
}

void handleDoublePress(Hothouse::Switches footswitch) {
  // Ignore double presses in edit modes
  if (pedal_mode != PEDAL_MODE_NORMAL) {
    return;
  }

  // When double press is detected, a normal press was already detected and
  // processed, so reverse that right off the bat.
  handleNormalPress(footswitch);

  if (footswitch == Hothouse::FOOTSWITCH_1) {
    // CHANGED: Enter tap tempo mode (was: enter reverb edit mode)
    enterTapTempoMode();
  } else if (footswitch == Hothouse::FOOTSWITCH_2) {
    // UNCHANGED: Toggle tremolo bypass
    bypass_trem = !bypass_trem;
  }
}

void handleLongPress(Hothouse::Switches footswitch) {
  if (footswitch == Hothouse::FOOTSWITCH_1) {
    // Long-press on left footswitch: Enter reverb edit mode
    bypass_verb = false;  // Make sure reverb is ON
    pedal_mode = PEDAL_MODE_EDIT_REVERB;
  } else if (footswitch == Hothouse::FOOTSWITCH_2) {
    // Long-press on right footswitch: Enter mono-stereo config

    // Turn on reverb and turn off the other effects
    bypass_verb = false;
    bypass_delay = true;
    bypass_trem = true;
    pedal_mode = PEDAL_MODE_EDIT_MONO_STEREO;
  }
}

void enterTapTempoMode() {
  pedal_mode = PEDAL_MODE_TAP_TEMPO;
  tap_tempo_active = true;
  tap_tempo_last_tap_time = System::GetNow();
  // Don't clear existing tap tempo data - allow refinement
}

void exitTapTempoMode() {
  pedal_mode = PEDAL_MODE_NORMAL;
  tap_tempo_active = false;
}

void handleTapTempoTap() {
  uint32_t currentTime = System::GetNow();

  // Calculate interval from last tap
  if (tap_tempo_last_tap_time > 0) {
    uint32_t interval = currentTime - tap_tempo_last_tap_time;

    // Validate interval is in reasonable range
    if (interval >= TAP_TEMPO_MIN_INTERVAL_MS &&
        interval <= TAP_TEMPO_MAX_INTERVAL_MS) {

      tap_tempo_interval_ms = interval;

      // Convert to samples at 48kHz
      tap_tempo_delay_samples = (interval / 1000.0f) * 48000.0f;

      // Clamp to valid delay range
      tap_tempo_delay_samples = daisysp::fclamp(tap_tempo_delay_samples,
                                                 TAP_TEMPO_SAMPLES_MIN,
                                                 TAP_TEMPO_SAMPLES_MAX);

      // Enable tap tempo control
      tap_tempo_controls_delay = true;
      master_delay_time_samples = tap_tempo_delay_samples;
    }
  }

  tap_tempo_last_tap_time = currentTime;
}

void checkTapTempoTimeout() {
  if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    uint32_t currentTime = System::GetNow();

    // Exit if no activity for 5 seconds
    if ((currentTime - tap_tempo_last_tap_time) >= TAP_TEMPO_TIMEOUT_MS) {
      exitTapTempoMode();
    }
  }
}

void checkDfuModeBothSwitches() {
  // Check if both footswitches are currently pressed
  bool fs1Pressed = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
  bool fs2Pressed = hw.switches[Hothouse::FOOTSWITCH_2].Pressed();

  if (fs1Pressed && fs2Pressed) {
    if (!both_switches_pressed) {
      // Just started pressing both
      both_switches_press_start_time = System::GetNow();
      both_switches_pressed = true;
    } else {
      // Check how long both have been held
      uint32_t holdDuration = System::GetNow() - both_switches_press_start_time;

      if (holdDuration >= DFU_BOTH_SWITCHES_HOLD_TIME_MS) {
        // Enter DFU mode - flash LEDs to indicate
        for (int i = 0; i < 5; i++) {
          led_left.Set(1.0f);
          led_right.Set(0.0f);
          led_left.Update();
          led_right.Update();
          System::Delay(100);

          led_left.Set(0.0f);
          led_right.Set(1.0f);
          led_left.Update();
          led_right.Update();
          System::Delay(100);
        }

        System::ResetToBootloader();
      }
    }
  } else {
    // Reset tracking
    both_switches_pressed = false;
  }
}

inline float hardLimit100(const float &x) {
    return (x > 1.) ? 1. : ((x < -1.) ? -1. : x);
}

void quickLedFlash() {
  led_left.Set(1.0f);
  led_right.Set(1.0f);
  led_left.Update();
  led_right.Update();
  hw.DelayMs(500);
}

void audioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  static float tremVal;
  hw.ProcessAllControls();

  if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode

    // Blink the left & right LEDs
    {
      static uint32_t editCount = 0;
      static bool ledState = true;
      if (++editCount >= hw.AudioCallbackRate() / 2) {
        editCount = 0;
        ledState = !ledState;
        led_left.Set(ledState ? 1.0f : 0.0f);
        led_right.Set(ledState ? 1.0f : 0.0f);
      }
    }
  } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Mono-Stereo edit mode
    // Blink the left & right LEDs alternately to indicate mono-stereo edit mode
    static uint32_t monoStereoEditCount = 0;
    static bool ledState = true;
    if (++monoStereoEditCount >= hw.AudioCallbackRate() / 2) {
      monoStereoEditCount = 0;
      ledState = !ledState;
      led_left.Set(ledState ? 1.0f : 0.0f);
      led_right.Set(ledState ? 0.0f : 1.0f);
    }
  } else if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    // Tap tempo mode
    // LED_1: Solid on to indicate tap tempo mode
    led_left.Set(1.0f);

    // LED_2: Blink at current tempo (if tempo set)
    if (tap_tempo_interval_ms > 0) {
      uint32_t blink_phase = System::GetNow() % tap_tempo_interval_ms;
      float blink_threshold = tap_tempo_interval_ms * 0.1f;  // 10% duty cycle

      if (blink_phase < blink_threshold) {
        led_right.Set(1.0f);
      } else {
        led_right.Set(0.1f);  // Dim when off
      }
    } else {
      // No tempo set yet - slow pulse
      uint32_t slow_pulse = System::GetNow() % 1000;
      led_right.Set(slow_pulse < 500 ? 1.0f : 0.1f);
    }
  } else {
    // Normal mode
    led_left.Set(bypass_verb ? 0.0f : 1.0f);

    // Reduce number of LED Updates for pulsing trem LED
    {
      static int count = 0;
      // set led 100 times/sec
      if (++count == hw.AudioCallbackRate() / 100) {
        count = 0;
        // If just delay is on, show full-strength LED
        // If just trem is on, show 40% pulsing LED
        // If both are on, show 100% pulsing LED
        led_right.Set(bypass_trem ? bypass_delay ? 0.0f : 1.0 : bypass_delay ? tremVal * 0.4 : tremVal);
      }
    }
  }
  led_left.Update();
  led_right.Update();

  plate_wet = p_verb_amt.Process();

  if (pedal_mode == PEDAL_MODE_NORMAL) {
    osc.SetFreq(p_trem_speed.Process());
    static float depth = 0;
    depth = daisysp::fclamp(p_trem_depth.Process(), 0.f, 1.f);
    depth *= 0.5f;
    osc.SetAmp(depth);
    dc_os = 1.f - depth;

    // Get tremolo mode from SWITCH_2
    TremoloMode tremMode = K_TREMOLO_MODE_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];

    // Set oscillator waveform based on mode (not used for harmonic)
    if (tremMode == TREMOLO_SQUARE) {
      osc.SetWaveform(FlickOscillator::WAVE_SQUARE_ROUNDED);
    } else if (tremMode == TREMOLO_SINE || tremMode == TREMOLO_HARMONIC) {
      osc.SetWaveform(FlickOscillator::WAVE_SIN);
    }
    // For harmonic mode, waveform doesn't matter much (use sine)

    //
    // Delay with subdivision and tap tempo support
    //

    // Get delay subdivision from SWITCH_3
    DelaySubdivision subdivision = K_DELAY_SUBDIVISION_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)];

    // Calculate subdivision multiplier
    float subdivisionMultiplier = 1.0f;
    switch (subdivision) {
      case DELAY_SUBDIV_DOTTED_EIGHTH:
        subdivisionMultiplier = 1.5f;
        break;
      case DELAY_SUBDIV_QUARTER_TRIPLET:
        subdivisionMultiplier = 1.333333f;  // 4/3
        break;
      case DELAY_SUBDIV_NORMAL:
      default:
        subdivisionMultiplier = 1.0f;
        break;
    }

    // Determine master delay time source
    float delayTimeCurrentValue = hw.knobs[Hothouse::KNOB_4].Value();

    if (tap_tempo_controls_delay) {
      // Check for knob takeover (5% movement)
      if (fabs(delayTimeCurrentValue - delay_time_last_value) > KNOB_TAKEOVER_THRESHOLD) {
        // Knob has moved - take back control from tap tempo
        tap_tempo_controls_delay = false;
        master_delay_time_samples = p_delay_time.Process();
      } else {
        // Tap tempo still controls
        master_delay_time_samples = tap_tempo_delay_samples;
      }
    } else {
      // Normal knob control
      master_delay_time_samples = p_delay_time.Process();
    }

    delay_time_last_value = delayTimeCurrentValue;

    // Apply subdivision to master time
    float finalDelayTime = master_delay_time_samples * subdivisionMultiplier;

    // Clamp to valid range (important for subdivisions that could exceed MAX_DELAY)
    finalDelayTime = daisysp::fclamp(finalDelayTime, TAP_TEMPO_SAMPLES_MIN, (float)MAX_DELAY);

    // Set delay targets
    delay_l.delayTarget = finalDelayTime;
    delay_r.delayTarget = finalDelayTime;

    // Feedback unchanged
    delay_l.feedback = delay_r.feedback = p_delay_feedback.Process();
    delay_drywet = (int)p_delay_amt.Process();

    // Reverb dry/wet mode
    switch (K_REVERB_KNOB_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)]) {
      case REVERB_KNOB_ALL_DRY:
        plate_dry = 1.0;
        break;
      case REVERB_KNOB_DRY_WET_MIX:
        plate_dry = 1.0 - plate_wet;
        break;
      case REVERB_KNOB_ALL_WET:
        plate_dry = 0.0f;
        break;
    }
  } else if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode
    plate_dry = 1.0; // Always use dry 100% in edit mode
    plate_pre_delay = p_knob_2.Process() * 0.25;
    plate_decay = p_knob_3.Process();        
    plate_tank_diffusion = p_knob_4.Process();
    plate_input_damp_high = p_knob_5.Process() * 10.0; // Dattorro takes values for this between 0 and 10
    plate_tank_damp_high = p_knob_6.Process() * 10.0; // Dattorro takes values for this between 0 and 10

    //
    // Read in all of the toggle switch values
    //

    // Switch 1 - Tank Mod Speed
    static const float tank_mod_speed_values[] = {0.5f, 0.25f, 0.1f};
    plate_tank_mod_speed = tank_mod_speed_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)];

    // Switch 2 - Tank Mod Depth
    static const float tank_mod_depth_values[] = {0.5f, 0.25f, 0.1f};
    plate_tank_mod_depth = tank_mod_depth_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];

    // Switch 3 - Tank Mod Shape
    static const float tank_mod_shape_values[] = {0.5f, 0.25f, 0.1f};
    plate_tank_mod_shape = tank_mod_shape_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)];

    verb.setDecay(plate_decay);
    verb.setTankDiffusion(plate_tank_diffusion);
    verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
    verb.setTankFilterHighCutFrequency(plate_tank_damp_high);

    verb.setTankModSpeed(plate_tank_mod_speed * 8);
    verb.setTankModDepth(plate_tank_mod_depth * 15);
    verb.setTankModShape(plate_tank_mod_shape);
    verb.setPreDelay(plate_pre_delay);    
  } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Mono-Stereo edit mode
    // SWITCH_3: Read mono-stereo mode
    switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)) {
      case Hothouse::TOGGLESWITCH_MIDDLE:
        mono_stereo_mode = MS_MODE_MISO; // Mono In, Stereo Out
        break;
      case Hothouse::TOGGLESWITCH_UP:
        mono_stereo_mode = MS_MODE_SISO; // Stereo In, Stereo Out
        break;
      default:
        mono_stereo_mode = MS_MODE_MIMO; // Mono In, Mono Out
    }
    updateReverbScales(mono_stereo_mode);

    // SWITCH_2: Read makeup gain setting (NEW)
    current_makeup_gain = K_MAKEUP_GAIN_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
  }

  for (size_t i = 0; i < size; ++i) {
    float dryL = in[0][i];
    float dryR = in[1][i];
    float sL, sR;
    sL = dryL;
    if (mono_stereo_mode == MS_MODE_MIMO || mono_stereo_mode == MS_MODE_MISO) {
      // Use the mono signel (L) for both channels in MIMO and MISO modes
      sR = dryL;
    } else {
      // Use both L & R inputs in SISO mode
      sR = dryR;
    }

    // Get makeup gain values (now from global variable)
    float tremMakeupGain = 1.0f;
    float delayMakeupGain = 1.0f;

    switch (current_makeup_gain) {
      case TV_MAKEUP_GAIN_HEAVY:
        tremMakeupGain = 1.6f;   // +4dB for tremolo
        delayMakeupGain = 2.0f;  // +6dB for delay
        break;
      case TV_MAKEUP_GAIN_NORMAL:
        tremMakeupGain = 1.2f;   // +1.6dB for tremolo
        delayMakeupGain = 1.66f; // +4.4dB for delay
        break;
      case TV_MAKEUP_GAIN_NONE:
      default:
        tremMakeupGain = 1.0f;
        delayMakeupGain = 1.0f;
        break;
    }

    if (!bypass_delay) {
      float mixL = 0;
      float mixR = 0;
      float fDryWet = delay_drywet / 100.0f;

      // update delayline with feedback
      float sigL = delay_l.Process(sL);
      float sigR = delay_r.Process(sR);
      mixL += sigL;
      mixR += sigR;

      // apply drywet and attenuate
      sL = fDryWet * mixL * 0.333f + (1.0f - fDryWet) * sL * delayMakeupGain;
      sR = fDryWet * mixR * 0.333f + (1.0f - fDryWet) * sR * delayMakeupGain;
    }

    if (!bypass_trem) {
      // Get tremolo mode from SWITCH_2 (in normal mode)
      TremoloMode tremMode = TREMOLO_SINE;  // Default
      if (pedal_mode == PEDAL_MODE_NORMAL) {
        tremMode = K_TREMOLO_MODE_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
      }

      // Generate LFO sample
      float lfoSample = osc.Process();

      // Apply tremolo based on mode
      if (tremMode == TREMOLO_HARMONIC) {
        // === HARMONIC TREMOLO ===

        // Process left channel
        harmonic_filter_l.Process(sL);
        float lowL = harmonic_filter_l.Low();
        float highL = harmonic_filter_l.High();

        // Apply tremolo with opposite phase to each band
        float lowModL = lowL * (1.0f + lfoSample);
        float highModL = highL * (1.0f - lfoSample);  // Inverted phase
        sL = (lowModL + highModL) * tremMakeupGain;

        // Process right channel
        harmonic_filter_r.Process(sR);
        float lowR = harmonic_filter_r.Low();
        float highR = harmonic_filter_r.High();

        float lowModR = lowR * (1.0f + lfoSample);
        float highModR = highR * (1.0f - lfoSample);
        sR = (lowModR + highModR) * tremMakeupGain;

      } else {
        // === STANDARD TREMOLO (Square or Sine) ===

        // DC offset to make LFO unipolar (0 to peak)
        tremVal = dc_os + lfoSample;

        sL *= tremVal * tremMakeupGain;
        sR *= tremVal * tremMakeupGain;
      }
    }

    // Keep sending input to the reverb even if bypassed so that when it's
    // enabled again it will already have the current input signal already
    // being processed.

    left_input = hardLimit100(sL) * reverb_dry_scale_factor;
    right_input = hardLimit100(sR) * reverb_dry_scale_factor;

    verb.process(left_input * MINUS_18DB_GAIN * MINUS_20DB_GAIN * (1.0f + input_amplification * 7.0f) * clearPopCancelValue,
                  right_input * MINUS_18DB_GAIN * MINUS_20DB_GAIN * (1.0f + input_amplification * 7.0f) * clearPopCancelValue);

    if (!bypass_verb) {
      // left_output = ((left_input * plate_dry * 0.1) + (verb.getLeftOutput() * plate_wet * clearPopCancelValue));
      // right_output = ((right_input * plate_dry * 0.1) + (verb.getRightOutput() * plate_wet * clearPopCancelValue));
      left_output = ((left_input * plate_dry * reverb_reverse_scale_factor) + (verb.getLeftOutput() * plate_wet * clearPopCancelValue));
      right_output = ((right_input * plate_dry * reverb_reverse_scale_factor) + (verb.getRightOutput() * plate_wet * clearPopCancelValue));

      sL = left_output;
      sR = right_output;
    }

    if (mono_stereo_mode == MS_MODE_MIMO) {
      out[0][i] = (sL * 0.5) + (sR * 0.5); // Sum the processed left and right channels
      out[1][i] = 0.0f; // Mute the unused channel
    } else {
      // Send stereo output in MISO and SISO
      out[0][i] = sL;
      out[1][i] = sR;
    }
  }
}

int main() {
  hw.Init(true); // Init the CPU at full speed
  hw.SetAudioBlockSize(8);  // Number of samples handled per callback
  hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
  
  // Initialize LEDs
  led_left.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  led_right.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  //
  // Initialize Potentiometers
  //

  // The p_knob_n parameters are used to process the potentiometers when in reverb edit mode.
  p_knob_1.Init(hw.knobs[Hothouse::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
  p_knob_2.Init(hw.knobs[Hothouse::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
  p_knob_3.Init(hw.knobs[Hothouse::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
  p_knob_4.Init(hw.knobs[Hothouse::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
  p_knob_5.Init(hw.knobs[Hothouse::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
  p_knob_6.Init(hw.knobs[Hothouse::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

  p_verb_amt.Init(hw.knobs[Hothouse::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);

  p_trem_speed.Init(hw.knobs[Hothouse::KNOB_2], 0.2f, 16.0f, Parameter::LINEAR);
  p_trem_depth.Init(hw.knobs[Hothouse::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);

  p_delay_time.Init(hw.knobs[Hothouse::KNOB_4], hw.AudioSampleRate() * 0.05f, MAX_DELAY, Parameter::LOGARITHMIC);
  p_delay_feedback.Init(hw.knobs[Hothouse::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
  p_delay_amt.Init(hw.knobs[Hothouse::KNOB_6], 0.0f, 100.0f, Parameter::LINEAR);

  del_mem_l.Init();
  del_mem_r.Init();
  delay_l.del = &del_mem_l;
  delay_r.del = &del_mem_r;

  osc.Init(hw.AudioSampleRate());

  // Initialize harmonic tremolo filters (state variable filters for crossover)
  harmonic_filter_l.Init(hw.AudioSampleRate());
  harmonic_filter_r.Init(hw.AudioSampleRate());
  harmonic_filter_l.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonic_filter_r.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonic_filter_l.SetRes(0.5f);  // Minimal resonance for flat response
  harmonic_filter_r.SetRes(0.5f);

  //
  // Dattorro Reverb Initialization
  //
  // Zero out the InterpDelay buffers used by the plate reverb
  for(int i = 0; i < 50; i++) {
      for(int j = 0; j < 144000; j++) {
          sdramData[i][j] = 0.;
      }
  }
  // Set this to 1.0 or plate reverb won't work. This is defined in Dattorro's
  // InterpDelay.cpp file.
  hold = 1.;

  verb.setSampleRate(48000);
  verb.setTimeScale(plate_time_scale);
  verb.enableInputDiffusion(plate_diffusion_enabled);
  verb.setInputFilterLowCutoffPitch(plate_input_damp_low);
  verb.setTankFilterLowCutFrequency(plate_tank_damp_low);

  Settings defaultSettings = {
    SETTINGS_VERSION, // version
    plate_decay,
    plate_tank_diffusion,
    plate_input_damp_high,
    plate_tank_damp_high,
    plate_tank_mod_speed,
    plate_tank_mod_depth,
    plate_tank_mod_shape,
    plate_pre_delay,
    MS_MODE_MIMO,               // monoStereoMode
    TV_MAKEUP_GAIN_NORMAL       // makeupGainMode (NEW)
  };
  saved_settings.Init(defaultSettings);

  loadSettings();

  Hothouse::FootswitchCallbacks callbacks = {
    .HandleNormalPress = handleNormalPress,
    .HandleDoublePress = handleDoublePress,
    .HandleLongPress = handleLongPress
  };
  hw.RegisterFootswitchCallbacks(&callbacks);

  hw.StartAdc();
  hw.ProcessAllControls();
  if (hw.switches[Hothouse::FOOTSWITCH_2].RawState()) {
    is_factory_reset_mode = true;
  } else {
    hw.StartAudio(audioCallback);
  }
  
  while (true) {
    // Check for tap tempo timeout
    checkTapTempoTimeout();

    // Check for DFU mode (both switches held)
    checkDfuModeBothSwitches();

    if(trigger_settings_save) {
			saved_settings.Save(); // Writing locally stored settings to the external flash
			trigger_settings_save = false;
	} else if (is_factory_reset_mode) {
      hw.ProcessAllControls();

      static uint32_t lastLedToggleTime = 0;
      static bool ledToggle = false;
      static uint32_t blinkInterval = 1000;
      uint32_t now = System::GetNow();
      uint32_t elapsedTime = now - lastLedToggleTime;
      if (elapsedTime >= blinkInterval) {
        // Alternate the LED lights in factory reset mode
        lastLedToggleTime = now;
        ledToggle = !ledToggle;
        led_left.Set(ledToggle ? 1.0f : 0.0f);
        led_right.Set(ledToggle ? 0.0f : 1.0f);
        led_left.Update();
        led_right.Update();
      }

      float lowKnobThreshold = 0.05;
      float highKnobThreshold = 0.95;
      float blinkFasterAmount = 300; // each stage removes this many MS from the factory reset blinking
      float knob1Value = p_knob_1.Process();
      if (factory_reset_stage == 0 && knob1Value >= highKnobThreshold) {
        factory_reset_stage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factory_reset_stage == 1 && knob1Value <= lowKnobThreshold) {
        factory_reset_stage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factory_reset_stage == 2 && knob1Value >= highKnobThreshold) {
        factory_reset_stage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factory_reset_stage == 3 && knob1Value <= lowKnobThreshold) {
        saved_settings.RestoreDefaults();
        loadSettings();
        quickLedFlash();          

        hw.StartAudio(audioCallback);
        factory_reset_stage = 0;
        bypass_delay = true;
        bypass_trem = true;
        pedal_mode = PEDAL_MODE_NORMAL;
        is_factory_reset_mode = false;
      }
    }
    hw.DelayMs(10);
  }
  return 0;
}
