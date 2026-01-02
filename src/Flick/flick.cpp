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
#define SETTINGS_VERSION 2

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
PersistentStorage<Settings> SavedSettings(hw.seed.qspi);

FlickOscillator osc;
float dc_os = 0;

DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delMemL;
DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delMemR;

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

constexpr ReverbKnobMode kReverbKnobMap[] = {
  REVERB_KNOB_ALL_WET,                        // UP
  REVERB_KNOB_DRY_WET_MIX,                    // MIDDLE
  REVERB_KNOB_ALL_DRY,                        // DOWN
};

constexpr TremDelMakeUpGain kMakeupGainMap[] = {
  TV_MAKEUP_GAIN_HEAVY,                       // UP
  TV_MAKEUP_GAIN_NORMAL,                      // MIDDLE
  TV_MAKEUP_GAIN_NONE,                        // DOWN
};

constexpr TremoloMode kTremoloModeMap[] = {
    TREMOLO_SQUARE,     // UP
    TREMOLO_SINE,       // MIDDLE
    TREMOLO_HARMONIC,   // DOWN
};

constexpr DelaySubdivision kDelaySubdivisionMap[] = {
  DELAY_SUBDIV_DOTTED_EIGHTH,     // UP (1.5x)
  DELAY_SUBDIV_NORMAL,            // MIDDLE (1.0x)
  DELAY_SUBDIV_QUARTER_TRIPLET,  // DOWN (1.333x)
};

Delay delayL;
Delay delayR;
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
float knob4_last_value = 0.0f;
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
Svf harmonic_filter_L;  // State variable filter for crossover
Svf harmonic_filter_R;
const float HARMONIC_TREMOLO_CROSSOVER_FREQ = 800.0f;  // Hz

// Reverb vars
bool plateDiffusionEnabled = true;
float platePreDelay = 0.;

float plateDelay = 0.0;

float plateDry = 1.0;
float plateWet = 0.5;

float plateDecay = 0.8;
float plateTimeScale = 1.007500;

float plateTankDiffusion = 0.85;

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
float plateInputDampLow = 2.87; // approx 100Hz
float plateInputDampHigh = 7.25;

float plateTankDampLow = 2.87; // approx 100Hz
float plateTankDampHigh = 7.25;

float plateTankModSpeed = 0.1;
float plateTankModDepth = 0.1;
float plateTankModShape = 0.25;

const float minus18dBGain = 0.12589254;
const float minus20dBGain = 0.1;

float leftInput = 0.;
float rightInput = 0.;
float leftOutput = 0.;
float rightOutput = 0.;
float reverbDryScaleFactor = 1.0;
float reverbReverseScaleFactor = 1.0;

float inputAmplification = 1.0; // This isn't really used yet

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

inline void update_reverb_scales(MonoStereoMode mode) {
  switch (mode) {
    case MS_MODE_MIMO:
      reverbDryScaleFactor = 5.0f; // Make the signal stronger for MIMO mode
      reverbReverseScaleFactor = 0.2f;
      break;
    case MS_MODE_MISO:
    case MS_MODE_SISO:
      reverbDryScaleFactor = 2.5f; // MISO and SISO modes
      reverbReverseScaleFactor = 0.4f;
      break;
  }
}

void load_settings() {

	// Reference to local copy of settings stored in flash
	Settings &LocalSettings = SavedSettings.GetSettings();

  int savedVersion = LocalSettings.version;

  if (savedVersion != SETTINGS_VERSION) {
    // Something has changed. Load defaults!
    SavedSettings.RestoreDefaults();
    load_settings();
    return;
  }

  plateDecay = LocalSettings.decay;
  plateTankDiffusion = LocalSettings.diffusion;
  plateInputDampHigh = LocalSettings.inputCutoffFreq;
  plateTankDampHigh = LocalSettings.tankCutoffFreq;
  plateTankModSpeed = LocalSettings.tankModSpeed;
  plateTankModDepth = LocalSettings.tankModDepth;
  plateTankModShape = LocalSettings.tankModShape;
  platePreDelay = LocalSettings.preDelay;
  mono_stereo_mode = static_cast<MonoStereoMode>(LocalSettings.monoStereoMode);
  update_reverb_scales(mono_stereo_mode);

  // Load makeup gain setting
  current_makeup_gain = static_cast<TremDelMakeUpGain>(LocalSettings.makeupGainMode);

  // Validate makeup gain value
  if (current_makeup_gain < TV_MAKEUP_GAIN_NONE ||
      current_makeup_gain > TV_MAKEUP_GAIN_HEAVY) {
    current_makeup_gain = TV_MAKEUP_GAIN_NORMAL;
  }

  verb.setPreDelay(platePreDelay);
  verb.setInputFilterHighCutoffPitch(plateInputDampHigh);
  verb.setDecay(plateDecay);
  verb.setTankDiffusion(plateTankDiffusion);
  verb.setTankFilterHighCutFrequency(plateTankDampHigh);
  verb.setTankModSpeed(plateTankModSpeed * 8);
  verb.setTankModDepth(plateTankModDepth * 15);
  verb.setTankModShape(plateTankModShape);
}

void save_settings() {
	//Reference to local copy of settings stored in flash
	Settings &LocalSettings = SavedSettings.GetSettings();

  LocalSettings.version = SETTINGS_VERSION;
  LocalSettings.decay = plateDecay;
  LocalSettings.diffusion = plateTankDiffusion;
  LocalSettings.inputCutoffFreq = plateInputDampHigh;
  LocalSettings.tankCutoffFreq = plateTankDampHigh;
  LocalSettings.tankModSpeed = plateTankModSpeed;
  LocalSettings.tankModDepth = plateTankModDepth;
  LocalSettings.tankModShape = plateTankModShape;
  LocalSettings.preDelay = platePreDelay;

	trigger_settings_save = true;
}

void save_mono_stereo_settings() {
  Settings &LocalSettings = SavedSettings.GetSettings();

  LocalSettings.monoStereoMode = mono_stereo_mode;
  LocalSettings.makeupGainMode = current_makeup_gain;  // NEW: Save makeup gain

  trigger_settings_save = true;
}

/// @brief Restore the reverb settings from the saved settings.
void restore_reverb_settings() {
	Settings &LocalSettings = SavedSettings.GetSettings();

  plateDecay = LocalSettings.decay;
  plateTankDiffusion = LocalSettings.diffusion;
  plateInputDampHigh = LocalSettings.inputCutoffFreq;
  plateTankDampHigh = LocalSettings.tankCutoffFreq;
  plateTankModSpeed = LocalSettings.tankModSpeed;
  plateTankModDepth = LocalSettings.tankModDepth;
  plateTankModShape = LocalSettings.tankModShape;
  platePreDelay = LocalSettings.preDelay;

  verb.setDecay(plateDecay);
  verb.setTankDiffusion(plateTankDiffusion);
  verb.setInputFilterHighCutoffPitch(plateInputDampHigh);
  verb.setTankFilterHighCutFrequency(plateTankDampHigh);

  verb.setTankModSpeed(plateTankModSpeed * 8);
  verb.setTankModDepth(plateTankModDepth * 15);
  verb.setTankModShape(plateTankModShape);
  verb.setPreDelay(platePreDelay);    
}

/// @brief Restore the mono-stereo settings from the saved settings.
void restore_mono_stereo_settings() {
  Settings &LocalSettings = SavedSettings.GetSettings();

  mono_stereo_mode = static_cast<MonoStereoMode>(LocalSettings.monoStereoMode);
  current_makeup_gain = static_cast<TremDelMakeUpGain>(LocalSettings.makeupGainMode);  // NEW: Restore makeup gain
  update_reverb_scales(mono_stereo_mode);
}

// Forward declarations for tap tempo functions
void enter_tap_tempo_mode();
void exit_tap_tempo_mode();
void handle_tap_tempo_tap();
void check_tap_tempo_timeout();
void check_dfu_mode_both_switches();

void handle_normal_press(Hothouse::Switches footswitch) {
  // Handle tap tempo mode
  if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    if (footswitch == Hothouse::FOOTSWITCH_1) {
      // Exit tap tempo mode
      exit_tap_tempo_mode();
      return;
    } else if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Tap the tempo
      handle_tap_tempo_tap();
      return;
    }
  }

  // Handle edit reverb mode
  if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Only save the settings if the RIGHT footswitch is pressed in edit mode.
    // The LEFT footswitch is used to exit edit mode without saving.
    if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Save the settings
      save_settings();
    } else {
      restore_reverb_settings();
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
      save_mono_stereo_settings();
    } else {
      restore_mono_stereo_settings();
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

void handle_double_press(Hothouse::Switches footswitch) {
  // Ignore double presses in edit modes
  if (pedal_mode != PEDAL_MODE_NORMAL) {
    return;
  }

  // When double press is detected, a normal press was already detected and
  // processed, so reverse that right off the bat.
  handle_normal_press(footswitch);

  if (footswitch == Hothouse::FOOTSWITCH_1) {
    // CHANGED: Enter tap tempo mode (was: enter reverb edit mode)
    enter_tap_tempo_mode();
  } else if (footswitch == Hothouse::FOOTSWITCH_2) {
    // UNCHANGED: Toggle tremolo bypass
    bypass_trem = !bypass_trem;
  }
}

void handle_long_press(Hothouse::Switches footswitch) {
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

void enter_tap_tempo_mode() {
  pedal_mode = PEDAL_MODE_TAP_TEMPO;
  tap_tempo_active = true;
  tap_tempo_last_tap_time = System::GetNow();
  // Don't clear existing tap tempo data - allow refinement
}

void exit_tap_tempo_mode() {
  pedal_mode = PEDAL_MODE_NORMAL;
  tap_tempo_active = false;
}

void handle_tap_tempo_tap() {
  uint32_t current_time = System::GetNow();

  // Calculate interval from last tap
  if (tap_tempo_last_tap_time > 0) {
    uint32_t interval = current_time - tap_tempo_last_tap_time;

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

  tap_tempo_last_tap_time = current_time;
}

void check_tap_tempo_timeout() {
  if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    uint32_t current_time = System::GetNow();

    // Exit if no activity for 5 seconds
    if ((current_time - tap_tempo_last_tap_time) >= TAP_TEMPO_TIMEOUT_MS) {
      exit_tap_tempo_mode();
    }
  }
}

void check_dfu_mode_both_switches() {
  // Check if both footswitches are currently pressed
  bool fs1_pressed = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
  bool fs2_pressed = hw.switches[Hothouse::FOOTSWITCH_2].Pressed();

  if (fs1_pressed && fs2_pressed) {
    if (!both_switches_pressed) {
      // Just started pressing both
      both_switches_press_start_time = System::GetNow();
      both_switches_pressed = true;
    } else {
      // Check how long both have been held
      uint32_t hold_duration = System::GetNow() - both_switches_press_start_time;

      if (hold_duration >= DFU_BOTH_SWITCHES_HOLD_TIME_MS) {
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

inline float hardLimit100_(const float &x) {
    return (x > 1.) ? 1. : ((x < -1.) ? -1. : x);
}

void quick_led_flash() {
  led_left.Set(1.0f);
  led_right.Set(1.0f);
  led_left.Update();
  led_right.Update();
  hw.DelayMs(500);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  static float trem_val;
  hw.ProcessAllControls();

  if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode

    // Blink the left & right LEDs
    {
      static uint32_t edit_count = 0;
      static bool led_state = true;
      if (++edit_count >= hw.AudioCallbackRate() / 2) {
        edit_count = 0;
        led_state = !led_state;
        led_left.Set(led_state ? 1.0f : 0.0f);
        led_right.Set(led_state ? 1.0f : 0.0f);
      }
    }
  } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Mono-Stereo edit mode
    // Blink the left & right LEDs alternately to indicate mono-stereo edit mode
    static uint32_t mono_stereo_edit_count = 0;
    static bool led_state = true;
    if (++mono_stereo_edit_count >= hw.AudioCallbackRate() / 2) {
      mono_stereo_edit_count = 0;
      led_state = !led_state;
      led_left.Set(led_state ? 1.0f : 0.0f);
      led_right.Set(led_state ? 0.0f : 1.0f);
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
        led_right.Set(bypass_trem ? bypass_delay ? 0.0f : 1.0 : bypass_delay ? trem_val * 0.4 : trem_val);
      }
    }
  }
  led_left.Update();
  led_right.Update();

  plateWet = p_verb_amt.Process();

  if (pedal_mode == PEDAL_MODE_NORMAL) {
    osc.SetFreq(p_trem_speed.Process());
    static float depth = 0;
    depth = daisysp::fclamp(p_trem_depth.Process(), 0.f, 1.f);
    depth *= 0.5f;
    osc.SetAmp(depth);
    dc_os = 1.f - depth;

    // Get tremolo mode from SWITCH_2
    TremoloMode trem_mode = kTremoloModeMap[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];

    // Set oscillator waveform based on mode (not used for harmonic)
    if (trem_mode == TREMOLO_SQUARE) {
      osc.SetWaveform(FlickOscillator::WAVE_SQUARE_ROUNDED);
    } else if (trem_mode == TREMOLO_SINE) {
      osc.SetWaveform(FlickOscillator::WAVE_SIN);
    }
    // For harmonic mode, waveform doesn't matter much (use sine)

    //
    // Delay with subdivision and tap tempo support
    //

    // Get delay subdivision from SWITCH_3
    DelaySubdivision subdivision = kDelaySubdivisionMap[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)];

    // Calculate subdivision multiplier
    float subdivision_multiplier = 1.0f;
    switch (subdivision) {
      case DELAY_SUBDIV_DOTTED_EIGHTH:
        subdivision_multiplier = 1.5f;
        break;
      case DELAY_SUBDIV_QUARTER_TRIPLET:
        subdivision_multiplier = 1.333333f;  // 4/3
        break;
      case DELAY_SUBDIV_NORMAL:
      default:
        subdivision_multiplier = 1.0f;
        break;
    }

    // Determine master delay time source
    float current_knob4_value = hw.knobs[Hothouse::KNOB_4].Value();

    if (tap_tempo_controls_delay) {
      // Check for knob takeover (5% movement)
      if (fabs(current_knob4_value - knob4_last_value) > KNOB_TAKEOVER_THRESHOLD) {
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

    knob4_last_value = current_knob4_value;

    // Apply subdivision to master time
    float final_delay_time = master_delay_time_samples * subdivision_multiplier;

    // Clamp to valid range (important for subdivisions that could exceed MAX_DELAY)
    final_delay_time = daisysp::fclamp(final_delay_time, TAP_TEMPO_SAMPLES_MIN, (float)MAX_DELAY);

    // Set delay targets
    delayL.delayTarget = final_delay_time;
    delayR.delayTarget = final_delay_time;

    // Feedback unchanged
    delayL.feedback = delayR.feedback = p_delay_feedback.Process();
    delay_drywet = (int)p_delay_amt.Process();

    // Reverb dry/wet mode
    switch (kReverbKnobMap[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)]) {
      case REVERB_KNOB_ALL_DRY:
        plateDry = 1.0;
        break;
      case REVERB_KNOB_DRY_WET_MIX:
        plateDry = 1.0 - plateWet;
        break;
      case REVERB_KNOB_ALL_WET:
        plateDry = 0.0f;
        break;
    }
  } else if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode
    plateDry = 1.0; // Always use dry 100% in edit mode
    platePreDelay = p_knob_2.Process() * 0.25;
    plateDecay = p_knob_3.Process();        
    plateTankDiffusion = p_knob_4.Process();
    plateInputDampHigh = p_knob_5.Process() * 10.0; // Dattorro takes values for this between 0 and 10
    plateTankDampHigh = p_knob_6.Process() * 10.0; // Dattorro takes values for this between 0 and 10

    //
    // Read in all of the toggle switch values
    //

    // Switch 1 - Tank Mod Speed
    static const float tank_mod_speed_values[] = {0.5f, 0.25f, 0.1f};
    plateTankModSpeed = tank_mod_speed_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)];

    // Switch 2 - Tank Mod Depth
    static const float tank_mod_depth_values[] = {0.5f, 0.25f, 0.1f};
    plateTankModDepth = tank_mod_depth_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];

    // Switch 3 - Tank Mod Shape
    static const float tank_mod_shape_values[] = {0.5f, 0.25f, 0.1f};
    plateTankModShape = tank_mod_shape_values[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)];

    verb.setDecay(plateDecay);
    verb.setTankDiffusion(plateTankDiffusion);
    verb.setInputFilterHighCutoffPitch(plateInputDampHigh);
    verb.setTankFilterHighCutFrequency(plateTankDampHigh);

    verb.setTankModSpeed(plateTankModSpeed * 8);
    verb.setTankModDepth(plateTankModDepth * 15);
    verb.setTankModShape(plateTankModShape);
    verb.setPreDelay(platePreDelay);    
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
    update_reverb_scales(mono_stereo_mode);

    // SWITCH_2: Read makeup gain setting (NEW)
    current_makeup_gain = kMakeupGainMap[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
  }

  for (size_t i = 0; i < size; ++i) {
    float dry_L = in[0][i];
    float dry_R = in[1][i];
    float s_L, s_R;
    s_L = dry_L;
    if (mono_stereo_mode == MS_MODE_MIMO || mono_stereo_mode == MS_MODE_MISO) {
      // Use the mono signel (L) for both channels in MIMO and MISO modes
      s_R = dry_L;
    } else {
      // Use both L & R inputs in SISO mode
      s_R = dry_R;
    }

    // Get makeup gain values (now from global variable)
    float trem_makeup_gain = 1.0f;
    float delay_makeup_gain = 1.0f;

    switch (current_makeup_gain) {
      case TV_MAKEUP_GAIN_HEAVY:
        trem_makeup_gain = 1.6f;   // +4dB for tremolo
        delay_makeup_gain = 2.0f;  // +6dB for delay
        break;
      case TV_MAKEUP_GAIN_NORMAL:
        trem_makeup_gain = 1.2f;   // +1.6dB for tremolo
        delay_makeup_gain = 1.66f; // +4.4dB for delay
        break;
      case TV_MAKEUP_GAIN_NONE:
      default:
        trem_makeup_gain = 1.0f;
        delay_makeup_gain = 1.0f;
        break;
    }

    if (!bypass_delay) {
      float mixL = 0;
      float mixR = 0;
      float fdrywet = delay_drywet / 100.0f;

      // update delayline with feedback
      float sigL = delayL.Process(s_L);
      float sigR = delayR.Process(s_R);
      mixL += sigL;
      mixR += sigR;

      // apply drywet and attenuate
      s_L = fdrywet * mixL * 0.333f + (1.0f - fdrywet) * s_L * delay_makeup_gain;
      s_R = fdrywet * mixR * 0.333f + (1.0f - fdrywet) * s_R * delay_makeup_gain;
    }

    if (!bypass_trem) {
      // Get tremolo mode from SWITCH_2 (in normal mode)
      TremoloMode trem_mode = TREMOLO_SINE;  // Default
      if (pedal_mode == PEDAL_MODE_NORMAL) {
        trem_mode = kTremoloModeMap[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
      }

      // Generate LFO sample
      float lfo_sample = osc.Process();

      // Apply tremolo based on mode
      if (trem_mode == TREMOLO_HARMONIC) {
        // === HARMONIC TREMOLO ===

        // Process left channel
        harmonic_filter_L.Process(s_L);
        float low_L = harmonic_filter_L.Low();
        float high_L = harmonic_filter_L.High();

        // Apply tremolo with opposite phase to each band
        float low_mod_L = low_L * (1.0f + lfo_sample);
        float high_mod_L = high_L * (1.0f - lfo_sample);  // Inverted phase
        s_L = (low_mod_L + high_mod_L) * trem_makeup_gain;

        // Process right channel
        harmonic_filter_R.Process(s_R);
        float low_R = harmonic_filter_R.Low();
        float high_R = harmonic_filter_R.High();

        float low_mod_R = low_R * (1.0f + lfo_sample);
        float high_mod_R = high_R * (1.0f - lfo_sample);
        s_R = (low_mod_R + high_mod_R) * trem_makeup_gain;

      } else {
        // === STANDARD TREMOLO (Square or Sine) ===

        // DC offset to make LFO unipolar (0 to peak)
        trem_val = dc_os + lfo_sample;

        s_L *= trem_val * trem_makeup_gain;
        s_R *= trem_val * trem_makeup_gain;
      }
    }

    // Keep sending input to the reverb even if bypassed so that when it's
    // enabled again it will already have the current input signal already
    // being processed.
    
    leftInput = hardLimit100_(s_L) * reverbDryScaleFactor;
    rightInput = hardLimit100_(s_R) * reverbDryScaleFactor;

    verb.process(leftInput * minus18dBGain * minus20dBGain * (1.0f + inputAmplification * 7.0f) * clearPopCancelValue,
                  rightInput * minus18dBGain * minus20dBGain * (1.0f + inputAmplification * 7.0f) * clearPopCancelValue);

    if (!bypass_verb) {
      // leftOutput = ((leftInput * plateDry * 0.1) + (verb.getLeftOutput() * plateWet * clearPopCancelValue));
      // rightOutput = ((rightInput * plateDry * 0.1) + (verb.getRightOutput() * plateWet * clearPopCancelValue));
      leftOutput = ((leftInput * plateDry * reverbReverseScaleFactor) + (verb.getLeftOutput() * plateWet * clearPopCancelValue));
      rightOutput = ((rightInput * plateDry * reverbReverseScaleFactor) + (verb.getRightOutput() * plateWet * clearPopCancelValue));

      s_L = leftOutput;
      s_R = rightOutput;
    }

    if (mono_stereo_mode == MS_MODE_MIMO) {
      out[0][i] = (s_L * 0.5) + (s_R * 0.5); // Sum the processed left and right channels
      out[1][i] = 0.0f; // Mute the unused channel
    } else {
      // Send stereo output in MISO and SISO
      out[0][i] = s_L;
      out[1][i] = s_R;
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

  delMemL.Init();
  delMemR.Init();
  delayL.del = &delMemL;
  delayR.del = &delMemR;

  osc.Init(hw.AudioSampleRate());

  // Initialize harmonic tremolo filters (state variable filters for crossover)
  harmonic_filter_L.Init(hw.AudioSampleRate());
  harmonic_filter_R.Init(hw.AudioSampleRate());
  harmonic_filter_L.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonic_filter_R.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonic_filter_L.SetRes(0.5f);  // Minimal resonance for flat response
  harmonic_filter_R.SetRes(0.5f);

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
  verb.setTimeScale(plateTimeScale);
  verb.enableInputDiffusion(plateDiffusionEnabled);
  verb.setInputFilterLowCutoffPitch(plateInputDampLow);
  verb.setTankFilterLowCutFrequency(plateTankDampLow);

  Settings defaultSettings = {
    SETTINGS_VERSION, // version
    plateDecay,
    plateTankDiffusion,
    plateInputDampHigh,
    plateTankDampHigh,
    plateTankModSpeed,
    plateTankModDepth,
    plateTankModShape,
    platePreDelay,
    MS_MODE_MIMO,               // monoStereoMode
    TV_MAKEUP_GAIN_NORMAL       // makeupGainMode (NEW)
  };
  SavedSettings.Init(defaultSettings);

  load_settings();

  Hothouse::FootswitchCallbacks callbacks = {
    .HandleNormalPress = handle_normal_press,
    .HandleDoublePress = handle_double_press,
    .HandleLongPress = handle_long_press
  };
  hw.RegisterFootswitchCallbacks(&callbacks);

  hw.StartAdc();
  hw.ProcessAllControls();
  if (hw.switches[Hothouse::FOOTSWITCH_2].RawState()) {
    is_factory_reset_mode = true;
  } else {
    hw.StartAudio(AudioCallback);
  }
  
  while (true) {
    // Check for tap tempo timeout
    check_tap_tempo_timeout();

    // Check for DFU mode (both switches held)
    check_dfu_mode_both_switches();

    if(trigger_settings_save) {
			SavedSettings.Save(); // Writing locally stored settings to the external flash
			trigger_settings_save = false;
		} else if (is_factory_reset_mode) {
      hw.ProcessAllControls();

      static uint32_t last_led_toggle_time = 0;
      static bool led_toggle = false;
      static uint32_t blink_interval = 1000;
      uint32_t now = System::GetNow();
      uint32_t elapsed_time = now - last_led_toggle_time;
      if (elapsed_time >= blink_interval) {
        // Alternate the LED lights in factory reset mode
        last_led_toggle_time = now;
        led_toggle = !led_toggle;
        led_left.Set(led_toggle ? 1.0f : 0.0f);
        led_right.Set(led_toggle ? 0.0f : 1.0f);
        led_left.Update();
        led_right.Update();
      }

      float low_knob_threshold = 0.05;
      float high_knob_threshold = 0.95;
      float blink_faster_amount = 300; // each stage removes this many MS from the factory reset blinking
      float knob_1_value = p_knob_1.Process();
      if (factory_reset_stage == 0 && knob_1_value >= high_knob_threshold) {
        factory_reset_stage++;
        blink_interval -= blink_faster_amount; // make the blinking faster as a UI feedback that the stage has been met
        quick_led_flash();          
      } else if (factory_reset_stage == 1 && knob_1_value <= low_knob_threshold) {
        factory_reset_stage++;
        blink_interval -= blink_faster_amount; // make the blinking faster as a UI feedback that the stage has been met
        quick_led_flash();          
      } else if (factory_reset_stage == 2 && knob_1_value >= high_knob_threshold) {
        factory_reset_stage++;
        blink_interval -= blink_faster_amount; // make the blinking faster as a UI feedback that the stage has been met
        quick_led_flash();          
      } else if (factory_reset_stage == 3 && knob_1_value <= low_knob_threshold) {
        SavedSettings.RestoreDefaults();
        load_settings();
        quick_led_flash();          

        hw.StartAudio(AudioCallback);
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
