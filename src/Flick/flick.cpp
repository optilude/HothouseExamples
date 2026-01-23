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
constexpr int SETTINGS_VERSION = 4;

// Audio configuration constants
constexpr float SAMPLE_RATE = 48000.0f;  // Audio sample rate in Hz
constexpr size_t MAX_DELAY = static_cast<size_t>(SAMPLE_RATE * 2.0f); // 2 second max delay

// Tremolo constants
constexpr float TREMOLO_SPEED_MIN = 0.1f;   // Minimum tremolo speed in Hz
constexpr float TREMOLO_SPEED_MAX = 10.0f;  // Maximum tremolo speed in Hz
constexpr float TREMOLO_DEPTH_SCALE = 0.5f; // Scale factor for tremolo depth (0-0.5 range)
constexpr float TREMOLO_LED_BRIGHTNESS = 0.4f; // LED brightness when only tremolo is active

// Delay constants
constexpr float DELAY_TIME_MIN_SECONDS = 0.02f;  // Minimum delay time (20ms - enables doubling/slapback)
constexpr float DELAY_WET_MIX_ATTENUATION = 0.333f; // Attenuation for wet delay signal
constexpr float DELAY_DRY_WET_PERCENT_MAX = 100.0f; // Max value for dry/wet percentage

// LED constants
constexpr float TAP_TEMPO_BLINK_DUTY_CYCLE = 0.1f; // 10% duty cycle for tap tempo LED

Hothouse hw;

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

// Tap tempo constants
constexpr uint32_t TAP_TEMPO_TIMEOUT_MS = 5000;     // Exit tap tempo after 5 seconds
constexpr uint32_t TAP_TEMPO_MIN_INTERVAL_MS = 20;  // Min 20ms = 3000 BPM (enables doubling/slapback)
constexpr uint32_t TAP_TEMPO_MAX_INTERVAL_MS = 4000; // Max 4 seconds = 15 BPM
constexpr float MS_PER_SECOND = 1000.0f;            // Milliseconds per second conversion
constexpr float TAP_TEMPO_SAMPLES_MIN = (TAP_TEMPO_MIN_INTERVAL_MS / MS_PER_SECOND) * SAMPLE_RATE;  // 20ms
constexpr float TAP_TEMPO_SAMPLES_MAX = (TAP_TEMPO_MAX_INTERVAL_MS / MS_PER_SECOND) * SAMPLE_RATE;  // 4s

// DFU mode - both switches
constexpr uint32_t DFU_BOTH_SWITCHES_HOLD_TIME_MS = 5000;  // 5 seconds

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
  bool bypassReverb;        // Reverb bypass state (true = bypassed)
  bool bypassDelay;         // Delay bypass state (true = bypassed)
  bool bypassTremolo;       // Tremolo bypass state (true = bypassed)

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
      a.makeupGainMode == makeupGainMode &&
      a.bypassReverb == bypassReverb &&
      a.bypassDelay == bypassDelay &&
      a.bypassTremolo == bypassTremolo
    );
  }
};

//Persistent Storage Declaration. Using type Settings and passed the devices qspi handle
PersistentStorage<Settings> savedSettings(hw.seed.qspi);

FlickOscillator osc;
float dcOffset = 0;

DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delMemL;
DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delMemR;

Dattorro verb(SAMPLE_RATE, 16, 4.0);
PedalMode pedalMode = PEDAL_MODE_NORMAL;
MonoStereoMode monoStereoMode = MS_MODE_MIMO;

Parameter pVerbAmt;
Parameter pTremSpeed, pTremDepth;
Parameter pDelayTime, pDelayFeedback, pDelayAmt;

Parameter pKnob1, pKnob2, pKnob3, pKnob4, pKnob5, pKnob6;

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
    TREMOLO_HARMONIC,   // MIDDLE
    TREMOLO_SINE,       // DOWN
     
};

constexpr DelaySubdivision K_DELAY_SUBDIVISION_MAP[] = {
  DELAY_SUBDIV_DOTTED_EIGHTH,     // UP (0.75x - 3/4 of quarter note)
  DELAY_SUBDIV_NORMAL,            // MIDDLE (1.0x - quarter note)
  DELAY_SUBDIV_QUARTER_TRIPLET,   // DOWN (0.6666x - 2/3 of quarter note)
};

Delay delayL;
Delay delayR;
int delayDryWet;

float reverbTone;
float reverbFeedback;
float reverbSploodge;

// Bypass vars
Led ledLeft, ledRight;
bool bypassVerb = true;
bool bypassTrem = true;
bool bypassDelay = true;

// Tap tempo state
bool tapTempoActive = false;
uint32_t tapTempoLastTapTime = 0;
uint32_t tapTempoIntervalMs = 0;
float tapTempoDelaySamples = 0.0f;
bool tapTempoControlsDelay = false;  // True when tap tempo overrides knob
float tapTempoTremoloFreqHz = 0.0f;
bool tapTempoControlsTremolo = false;  // True when tap tempo overrides tremolo knob

// Knob takeover for KNOB_4 (delay time) and KNOB_2 (tremolo speed)
float delayTimeLastValue = 0.0f;
float tremSpeedLastValue = 0.0f;
constexpr float KNOB_TAKEOVER_THRESHOLD = 0.05f;  // 5% movement required

// Master delay time (before subdivision multiplier)
float masterDelayTimeSamples = 0.0f;

// DFU mode detection
uint32_t bothSwitchesPressStartTime = 0;
bool bothSwitchesPressed = false;

// Current makeup gain setting (persisted)
TremDelMakeUpGain currentMakeupGain = TV_MAKEUP_GAIN_NORMAL;

// Harmonic tremolo state
using daisysp::Svf;
Svf harmonicFilterL;  // State variable filter for crossover
Svf harmonicFilterR;
constexpr float HARMONIC_TREMOLO_CROSSOVER_FREQ = 800.0f;  // Hz

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

constexpr float MINUS_18DB_GAIN = 0.12589254;
constexpr float MINUS_20DB_GAIN = 0.1;

float leftInput = 0.;
float rightInput = 0.;
float leftOutput = 0.;
float rightOutput = 0.;
float reverbDryScaleFactor = 1.0;
float reverbReverseScaleFactor = 1.0;

float inputAmplification = 1.0; // This isn't really used yet

bool triggerSettingsSave = false;

/// @brief Used at startup to control a factory reset.
///
/// This gets set to true in `main()` if footswitch 2 is depressed at boot.
/// The LED lights will start flashing alternatively. To exit this mode without
/// making any changes, press either footswitch.
///
/// To reset, rotate knob_1 to 100%, to 0%, to 100%, and back to 0%. This will
/// restore all defaults and then go into normal pedal mode.
bool isFactoryResetMode = false;

/// @brief Tracks the stage of knob_1 rotation in factory reset mode.
///
/// 0: User must rotate knob_1 to 100% to advance to the next stage.
/// 1: User must rotate knob_1 to 0% to advance to the next stage.
/// 2: User must rotate knob_1 to 100% to advance to the next stage.
/// 3: User must rotate knob_1 to 0% to complete the factory reset.
int factoryResetStage = 0;

inline void updateReverbScales(MonoStereoMode mode) {
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

void loadSettings() {

  // Reference to local copy of settings stored in flash
  Settings &localSettings = savedSettings.GetSettings();

  int savedVersion = localSettings.version;

  if (savedVersion != SETTINGS_VERSION) {
    // Something has changed. Load defaults!
    savedSettings.RestoreDefaults();
    loadSettings();
    return;
  }

  plateDecay = localSettings.decay;
  plateTankDiffusion = localSettings.diffusion;
  plateInputDampHigh = localSettings.inputCutoffFreq;
  plateTankDampHigh = localSettings.tankCutoffFreq;
  plateTankModSpeed = localSettings.tankModSpeed;
  plateTankModDepth = localSettings.tankModDepth;
  plateTankModShape = localSettings.tankModShape;
  platePreDelay = localSettings.preDelay;
  monoStereoMode = static_cast<MonoStereoMode>(localSettings.monoStereoMode);
  updateReverbScales(monoStereoMode);

  // Load makeup gain setting
  currentMakeupGain = static_cast<TremDelMakeUpGain>(localSettings.makeupGainMode);

  // Validate makeup gain value
  if (currentMakeupGain < TV_MAKEUP_GAIN_NONE ||
      currentMakeupGain > TV_MAKEUP_GAIN_HEAVY) {
    currentMakeupGain = TV_MAKEUP_GAIN_NORMAL;
  }

  // Load bypass states - defensive: default to bypassed (true) on any doubt
  // Boolean values are inherently safe (0 or 1), but we still validate defensively
  bypassVerb = localSettings.bypassReverb;
  bypassDelay = localSettings.bypassDelay;
  bypassTrem = localSettings.bypassTremolo;

  verb.setPreDelay(platePreDelay);
  verb.setInputFilterHighCutoffPitch(plateInputDampHigh);
  verb.setDecay(plateDecay);
  verb.setTankDiffusion(plateTankDiffusion);
  verb.setTankFilterHighCutFrequency(plateTankDampHigh);
  verb.setTankModSpeed(plateTankModSpeed * 8);
  verb.setTankModDepth(plateTankModDepth * 15);
  verb.setTankModShape(plateTankModShape);
}

void saveSettings() {
  //Reference to local copy of settings stored in flash
  Settings &localSettings = savedSettings.GetSettings();

  localSettings.version = SETTINGS_VERSION;
  localSettings.decay = plateDecay;
  localSettings.diffusion = plateTankDiffusion;
  localSettings.inputCutoffFreq = plateInputDampHigh;
  localSettings.tankCutoffFreq = plateTankDampHigh;
  localSettings.tankModSpeed = plateTankModSpeed;
  localSettings.tankModDepth = plateTankModDepth;
  localSettings.tankModShape = plateTankModShape;
  localSettings.preDelay = platePreDelay;

  triggerSettingsSave = true;
}

void saveMonoStereoSettings() {
  Settings &localSettings = savedSettings.GetSettings();

  localSettings.monoStereoMode = monoStereoMode;
  localSettings.makeupGainMode = currentMakeupGain;  // NEW: Save makeup gain

  triggerSettingsSave = true;
}

void saveBypassStates() {
  Settings &localSettings = savedSettings.GetSettings();

  localSettings.bypassReverb = bypassVerb;
  localSettings.bypassDelay = bypassDelay;
  localSettings.bypassTremolo = bypassTrem;

  triggerSettingsSave = true;
}

/// @brief Restore the reverb settings from the saved settings.
void restoreReverbSettings() {
  Settings &localSettings = savedSettings.GetSettings();

  plateDecay = localSettings.decay;
  plateTankDiffusion = localSettings.diffusion;
  plateInputDampHigh = localSettings.inputCutoffFreq;
  plateTankDampHigh = localSettings.tankCutoffFreq;
  plateTankModSpeed = localSettings.tankModSpeed;
  plateTankModDepth = localSettings.tankModDepth;
  plateTankModShape = localSettings.tankModShape;
  platePreDelay = localSettings.preDelay;

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
void restoreMonoStereoSettings() {
  Settings &localSettings = savedSettings.GetSettings();

  monoStereoMode = static_cast<MonoStereoMode>(localSettings.monoStereoMode);
  currentMakeupGain = static_cast<TremDelMakeUpGain>(localSettings.makeupGainMode);  // NEW: Restore makeup gain
  updateReverbScales(monoStereoMode);
}

// Forward declarations for tap tempo functions
void enterTapTempoMode();
void exitTapTempoMode();
void handleTapTempoTap();
void checkTapTempoTimeout();
void checkDfuModeBothSwitches();

void handleNormalPress(Hothouse::Switches footswitch) {
  // Handle tap tempo mode
  if (pedalMode == PEDAL_MODE_TAP_TEMPO) {
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
  if (pedalMode == PEDAL_MODE_EDIT_REVERB) {
    // Only save the settings if the RIGHT footswitch is pressed in edit mode.
    // The LEFT footswitch is used to exit edit mode without saving.
    if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Save the settings
      saveSettings();
    } else {
      restoreReverbSettings();
    }
    pedalMode = PEDAL_MODE_NORMAL;
    return;
  }

  // Handle mono-stereo edit mode
  if (pedalMode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Only save the settings if the RIGHT footswitch is pressed in mono-stereo
    // edit mode. The LEFT footswitch is used to exit mono-stereo edit mode
    // without saving.
    if (footswitch == Hothouse::FOOTSWITCH_2) {
      // Save the mono-stereo settings
      saveMonoStereoSettings();
    } else {
      restoreMonoStereoSettings();
    }
    pedalMode = PEDAL_MODE_NORMAL;
    return;
  }

  // Normal mode bypass toggles
  if (footswitch == Hothouse::FOOTSWITCH_1) {
    bypassVerb = !bypassVerb;

    if (bypassVerb) {
      // Clear the reverb tails when the reverb is bypassed so if you
      // turn it back on, it starts fresh and doesn't sound weird.
      verb.clear();
    }
  } else {
    bypassDelay = !bypassDelay;
  }

  // Save bypass state to persistent storage
  saveBypassStates();
}

void handleDoublePress(Hothouse::Switches footswitch) {
  // Ignore double presses in edit modes
  if (pedalMode != PEDAL_MODE_NORMAL) {
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
    bypassTrem = !bypassTrem;

    // Save bypass state to persistent storage
    saveBypassStates();
  }
}

void handleLongPress(Hothouse::Switches footswitch) {
  if (footswitch == Hothouse::FOOTSWITCH_1) {
    // Long-press on left footswitch: Enter reverb edit mode
    bypassVerb = false;  // Make sure reverb is ON
    pedalMode = PEDAL_MODE_EDIT_REVERB;
  } else if (footswitch == Hothouse::FOOTSWITCH_2) {
    // Long-press on right footswitch: Enter mono-stereo config

    // Turn on reverb and turn off the other effects
    bypassVerb = false;
    bypassDelay = true;
    bypassTrem = true;
    pedalMode = PEDAL_MODE_EDIT_MONO_STEREO;
  }
}

void enterTapTempoMode() {
  pedalMode = PEDAL_MODE_TAP_TEMPO;
  tapTempoActive = true;
  tapTempoLastTapTime = System::GetNow();
  // Don't clear existing tap tempo data - allow refinement

  // Set tap tempo control flags based on which effects are currently active
  bool delayActive = !bypassDelay;
  bool tremoloActive = !bypassTrem;

  if (!delayActive && !tremoloActive) {
    // Neither effect active: set tempo for both
    tapTempoControlsDelay = true;
    tapTempoControlsTremolo = true;
  } else if (delayActive && tremoloActive) {
    // Both effects active: set tempo for both
    tapTempoControlsDelay = true;
    tapTempoControlsTremolo = true;
  } else if (delayActive && !tremoloActive) {
    // Only delay active: set tempo for delay only
    tapTempoControlsDelay = true;
    tapTempoControlsTremolo = false;
  } else if (!delayActive && tremoloActive) {
    // Only tremolo active: set tempo for tremolo only
    tapTempoControlsDelay = false;
    tapTempoControlsTremolo = true;
  }
}

void exitTapTempoMode() {
  pedalMode = PEDAL_MODE_NORMAL;
  tapTempoActive = false;
}

void handleTapTempoTap() {
  uint32_t currentTime = System::GetNow();

  // Calculate interval from last tap
  if (tapTempoLastTapTime > 0) {
    uint32_t interval = currentTime - tapTempoLastTapTime;

    // Validate interval is in reasonable range
    if (interval >= TAP_TEMPO_MIN_INTERVAL_MS &&
        interval <= TAP_TEMPO_MAX_INTERVAL_MS) {

      tapTempoIntervalMs = interval;

      // Convert to delay samples at 48kHz
      tapTempoDelaySamples = (interval / MS_PER_SECOND) * SAMPLE_RATE;

      // Clamp to valid delay range
      tapTempoDelaySamples = daisysp::fclamp(tapTempoDelaySamples,
                                                 TAP_TEMPO_SAMPLES_MIN,
                                                 TAP_TEMPO_SAMPLES_MAX);

      // Convert to tremolo frequency (Hz)
      tapTempoTremoloFreqHz = MS_PER_SECOND / interval;

      // Clamp to tremolo speed range
      tapTempoTremoloFreqHz = daisysp::fclamp(tapTempoTremoloFreqHz, TREMOLO_SPEED_MIN, TREMOLO_SPEED_MAX);

      // Tap tempo control flags are set in enterTapTempoMode() based on which
      // effects were active when entering tap tempo mode. They remain set until
      // the user manually takes control by moving the relevant knob.
      masterDelayTimeSamples = tapTempoDelaySamples;
    }
  }

  tapTempoLastTapTime = currentTime;
}

void checkTapTempoTimeout() {
  if (pedalMode == PEDAL_MODE_TAP_TEMPO) {
    uint32_t currentTime = System::GetNow();

    // Exit if no activity for 5 seconds
    if ((currentTime - tapTempoLastTapTime) >= TAP_TEMPO_TIMEOUT_MS) {
      exitTapTempoMode();
    }
  }
}

void applyDelaySubdivisionAndSetTargets(float masterDelaySamples) {
  // Get delay subdivision from SWITCH_3
  DelaySubdivision subdivision = K_DELAY_SUBDIVISION_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)];

  // Calculate subdivision multiplier
  float subdivisionMultiplier = 1.0f;
  switch (subdivision) {
    case DELAY_SUBDIV_DOTTED_EIGHTH:
      subdivisionMultiplier = 0.75f;  // 3/4 of quarter note
      break;
    case DELAY_SUBDIV_QUARTER_TRIPLET:
      subdivisionMultiplier = 0.666666f;  // 2/3 of quarter note
      break;
    case DELAY_SUBDIV_NORMAL:
    default:
      subdivisionMultiplier = 1.0f;
      break;
  }

  // Apply subdivision to master time
  float finalDelayTime = masterDelaySamples * subdivisionMultiplier;

  // Clamp to valid range
  finalDelayTime = daisysp::fclamp(finalDelayTime, TAP_TEMPO_SAMPLES_MIN, (float)MAX_DELAY);

  // Set delay targets
  delayL.delayTarget = finalDelayTime;
  delayR.delayTarget = finalDelayTime;
}

void checkDfuModeBothSwitches() {
  // Check if both footswitches are currently pressed
  bool fs1Pressed = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
  bool fs2Pressed = hw.switches[Hothouse::FOOTSWITCH_2].Pressed();

  if (fs1Pressed && fs2Pressed) {
    if (!bothSwitchesPressed) {
      // Just started pressing both
      bothSwitchesPressStartTime = System::GetNow();
      bothSwitchesPressed = true;
    } else {
      // Check how long both have been held
      uint32_t holdDuration = System::GetNow() - bothSwitchesPressStartTime;

      if (holdDuration >= DFU_BOTH_SWITCHES_HOLD_TIME_MS) {
        // Enter DFU mode - flash LEDs to indicate
        for (int i = 0; i < 5; i++) {
          ledLeft.Set(1.0f);
          ledRight.Set(0.0f);
          ledLeft.Update();
          ledRight.Update();
          System::Delay(100);

          ledLeft.Set(0.0f);
          ledRight.Set(1.0f);
          ledLeft.Update();
          ledRight.Update();
          System::Delay(100);
        }

        System::ResetToBootloader();
      }
    }
  } else {
    // Reset tracking
    bothSwitchesPressed = false;
  }
}

inline float hardLimit100(const float &x) {
  return (x > 1.) ? 1. : ((x < -1.) ? -1. : x);
}

void quickLedFlash() {
  ledLeft.Set(1.0f);
  ledRight.Set(1.0f);
  ledLeft.Update();
  ledRight.Update();
  hw.DelayMs(500);
}

void audioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  static float tremVal;
  hw.ProcessAllControls();

  if (pedalMode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode
    // Blink the left & right LEDs

    static uint32_t editCount = 0;
    static bool ledState = true;
    if (++editCount >= hw.AudioCallbackRate() / 2) {
      editCount = 0;
      ledState = !ledState;
      ledLeft.Set(ledState ? 1.0f : 0.0f);
      ledRight.Set(ledState ? 1.0f : 0.0f);
    }
  } else if (pedalMode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Mono-Stereo edit mode
    // Blink the left & right LEDs alternately to indicate mono-stereo edit mode
    static uint32_t monoStereoEditCount = 0;
    static bool ledState = true;
    if (++monoStereoEditCount >= hw.AudioCallbackRate() / 2) {
      monoStereoEditCount = 0;
      ledState = !ledState;
      ledLeft.Set(ledState ? 1.0f : 0.0f);
      ledRight.Set(ledState ? 0.0f : 1.0f);
    }
  } else if (pedalMode == PEDAL_MODE_TAP_TEMPO) {
    // Tap tempo mode
    // LED_1: Slow pulse to indicate tap tempo mode
    uint32_t slow_pulse = System::GetNow() % 1000;
    ledLeft.Set(slow_pulse < 500 ? 1.0f : 0.1f);

    // LED_2: Blink at current tempo (if tempo set)
    if (tapTempoIntervalMs > 0) {
      uint32_t blink_phase = System::GetNow() % tapTempoIntervalMs;
      float blink_threshold = tapTempoIntervalMs * TAP_TEMPO_BLINK_DUTY_CYCLE;

      if (blink_phase < blink_threshold) {
        ledRight.Set(1.0f);
      } else {
        ledRight.Set(0.1f);  // Dim when off
      }
    } else {
      // No tempo set yet - slow pulse
      uint32_t slow_pulse = System::GetNow() % 1000;
      ledRight.Set(slow_pulse < 500 ? 1.0f : 0.1f);
    }

    // Apply tap tempo delay time immediately while in tap tempo mode
    applyDelaySubdivisionAndSetTargets(tapTempoDelaySamples);

    // Also apply tremolo frequency in tap tempo mode
    if (tapTempoControlsTremolo) {
      osc.SetFreq(tapTempoTremoloFreqHz);
    }
  } else {
    // Normal mode
    ledLeft.Set(bypassVerb ? 0.0f : 1.0f);

    // Reduce number of LED Updates for pulsing trem LED
    static int count = 0;
    // set led 100 times/sec
    if (++count == hw.AudioCallbackRate() / 100) {
      count = 0;
      // If just delay is on, show full-strength LED
      // If just trem is on, show 40% pulsing LED
      // If both are on, show 100% pulsing LED
      ledRight.Set(bypassTrem ? bypassDelay ? 0.0f : 1.0 : bypassDelay ? tremVal * TREMOLO_LED_BRIGHTNESS : tremVal);
    }
  }

  ledLeft.Update();
  ledRight.Update();

  plateWet = pVerbAmt.Process();

  if (pedalMode == PEDAL_MODE_NORMAL) {
    // Tremolo speed with tap tempo support
    float tremSpeedCurrentValue = hw.knobs[Hothouse::KNOB_2].Value();

    if (tapTempoControlsTremolo) {
      // Check for knob takeover (5% movement)
      if (fabs(tremSpeedCurrentValue - tremSpeedLastValue) > KNOB_TAKEOVER_THRESHOLD) {
        // Knob has moved - take back control from tap tempo
        tapTempoControlsTremolo = false;
        osc.SetFreq(pTremSpeed.Process());
        tremSpeedLastValue = tremSpeedCurrentValue; // Update on takeover
      } else {
        // Tap tempo still controls tremolo speed
        osc.SetFreq(tapTempoTremoloFreqHz);
        // Don't update tremSpeedLastValue while tap tempo is in control
      }
    } else {
      // Normal knob control
      osc.SetFreq(pTremSpeed.Process());
      tremSpeedLastValue = tremSpeedCurrentValue; // Track knob position
    }

    static float depth = 0;
    depth = daisysp::fclamp(pTremDepth.Process(), 0.f, 1.f);
    depth *= TREMOLO_DEPTH_SCALE;
    osc.SetAmp(depth);
    dcOffset = 1.f - depth;

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

    // Determine master delay time source
    float delayTimeCurrentValue = hw.knobs[Hothouse::KNOB_4].Value();

    if (tapTempoControlsDelay) {
      // Check for knob takeover (5% movement)
      if (fabs(delayTimeCurrentValue - delayTimeLastValue) > KNOB_TAKEOVER_THRESHOLD) {
        // Knob has moved - take back control from tap tempo
        tapTempoControlsDelay = false;
        masterDelayTimeSamples = pDelayTime.Process();
        delayTimeLastValue = delayTimeCurrentValue; // Update on takeover
      } else {
        // Tap tempo still controls
        masterDelayTimeSamples = tapTempoDelaySamples;
        // Don't update delayTimeLastValue while tap tempo is in control
      }
    } else {
      // Normal knob control
      masterDelayTimeSamples = pDelayTime.Process();
      delayTimeLastValue = delayTimeCurrentValue; // Track knob position
    }

    // Apply subdivision and set delay targets
    applyDelaySubdivisionAndSetTargets(masterDelayTimeSamples);

    // Feedback unchanged
    delayL.feedback = delayR.feedback = pDelayFeedback.Process();
    delayDryWet = (int)pDelayAmt.Process();

    // Reverb dry/wet mode
    switch (K_REVERB_KNOB_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)]) {
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
  } else if (pedalMode == PEDAL_MODE_EDIT_REVERB) {
    // Edit mode
    plateDry = 1.0; // Always use dry 100% in edit mode
    platePreDelay = pKnob2.Process() * 0.25;
    plateDecay = pKnob3.Process();
    plateTankDiffusion = pKnob4.Process();
    plateInputDampHigh = pKnob5.Process() * 10.0; // Dattorro takes values for this between 0 and 10
    plateTankDampHigh = pKnob6.Process() * 10.0; // Dattorro takes values for this between 0 and 10

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
  } else if (pedalMode == PEDAL_MODE_EDIT_MONO_STEREO) {
    // Mono-Stereo edit mode
    // SWITCH_3: Read mono-stereo mode
    switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3)) {
      case Hothouse::TOGGLESWITCH_MIDDLE:
        monoStereoMode = MS_MODE_MISO; // Mono In, Stereo Out
        break;
      case Hothouse::TOGGLESWITCH_UP:
        monoStereoMode = MS_MODE_SISO; // Stereo In, Stereo Out
        break;
      default:
        monoStereoMode = MS_MODE_MIMO; // Mono In, Mono Out
    }
    updateReverbScales(monoStereoMode);

    // SWITCH_2: Read makeup gain setting (NEW)
    currentMakeupGain = K_MAKEUP_GAIN_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
  }

  for (size_t i = 0; i < size; ++i) {
    float dryL = in[0][i];
    float dryR = in[1][i];
    float sL, sR;
    sL = dryL;
    if (monoStereoMode == MS_MODE_MIMO || monoStereoMode == MS_MODE_MISO) {
      // Use the mono signel (L) for both channels in MIMO and MISO modes
      sR = dryL;
    } else {
      // Use both L & R inputs in SISO mode
      sR = dryR;
    }

    // Get makeup gain values (now from global variable)
    float tremMakeupGain = 1.0f;
    float delayMakeupGain = 1.0f;

    switch (currentMakeupGain) {
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

    if (!bypassDelay) {
      float mixL = 0;
      float mixR = 0;
      float fDryWet = delayDryWet / DELAY_DRY_WET_PERCENT_MAX;

      // update delayline with feedback
      float sigL = delayL.Process(sL);
      float sigR = delayR.Process(sR);
      mixL += sigL;
      mixR += sigR;

      // apply drywet and attenuate
      sL = fDryWet * mixL * DELAY_WET_MIX_ATTENUATION + (1.0f - fDryWet) * sL * delayMakeupGain;
      sR = fDryWet * mixR * DELAY_WET_MIX_ATTENUATION + (1.0f - fDryWet) * sR * delayMakeupGain;
    }

    if (!bypassTrem) {
      // Get tremolo mode from SWITCH_2 (in normal mode)
      TremoloMode tremMode = TREMOLO_SINE;  // Default
      if (pedalMode == PEDAL_MODE_NORMAL) {
        tremMode = K_TREMOLO_MODE_MAP[hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2)];
      }

      // Generate LFO sample
      float lfoSample = osc.Process();

      // DC offset to make LFO unipolar (0 to peak) - for LED display
      tremVal = dcOffset + lfoSample;

      // Apply tremolo based on mode
      if (tremMode == TREMOLO_HARMONIC) {
        // === HARMONIC TREMOLO ===

        // Process left channel
        harmonicFilterL.Process(sL);
        float lowL = harmonicFilterL.Low();
        float highL = harmonicFilterL.High();

        // Apply tremolo with opposite phase to each band
        float lowModL = lowL * (1.0f + lfoSample);
        float highModL = highL * (1.0f - lfoSample);  // Inverted phase
        sL = (lowModL + highModL) * tremMakeupGain;

        // Process right channel
        harmonicFilterR.Process(sR);
        float lowR = harmonicFilterR.Low();
        float highR = harmonicFilterR.High();

        float lowModR = lowR * (1.0f + lfoSample);
        float highModR = highR * (1.0f - lfoSample);
        sR = (lowModR + highModR) * tremMakeupGain;

      } else {
        // === STANDARD TREMOLO (Square or Sine) ===

        sL *= tremVal * tremMakeupGain;
        sR *= tremVal * tremMakeupGain;
      }
    }

    // Keep sending input to the reverb even if bypassed so that when it's
    // enabled again it will already have the current input signal already
    // being processed.

    leftInput = hardLimit100(sL) * reverbDryScaleFactor;
    rightInput = hardLimit100(sR) * reverbDryScaleFactor;

    verb.process(leftInput * MINUS_18DB_GAIN * MINUS_20DB_GAIN * (1.0f + inputAmplification * 7.0f) * clearPopCancelValue,
                  rightInput * MINUS_18DB_GAIN * MINUS_20DB_GAIN * (1.0f + inputAmplification * 7.0f) * clearPopCancelValue);

    if (!bypassVerb) {
      // leftOutput = ((leftInput * plateDry * 0.1) + (verb.getLeftOutput() * plateWet * clearPopCancelValue));
      // rightOutput = ((rightInput * plateDry * 0.1) + (verb.getRightOutput() * plateWet * clearPopCancelValue));
      leftOutput = ((leftInput * plateDry * reverbReverseScaleFactor) + (verb.getLeftOutput() * plateWet * clearPopCancelValue));
      rightOutput = ((rightInput * plateDry * reverbReverseScaleFactor) + (verb.getRightOutput() * plateWet * clearPopCancelValue));

      sL = leftOutput;
      sR = rightOutput;
    }

    if (monoStereoMode == MS_MODE_MIMO) {
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
  ledLeft.Init(hw.seed.GetPin(Hothouse::LED_1), false);
  ledRight.Init(hw.seed.GetPin(Hothouse::LED_2), false);

  //
  // Initialize Potentiometers
  //

  // The pKnob_n parameters are used to process the potentiometers when in reverb edit mode.
  pKnob1.Init(hw.knobs[Hothouse::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
  pKnob2.Init(hw.knobs[Hothouse::KNOB_2], 0.0f, 1.0f, Parameter::LINEAR);
  pKnob3.Init(hw.knobs[Hothouse::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);
  pKnob4.Init(hw.knobs[Hothouse::KNOB_4], 0.0f, 1.0f, Parameter::LINEAR);
  pKnob5.Init(hw.knobs[Hothouse::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
  pKnob6.Init(hw.knobs[Hothouse::KNOB_6], 0.0f, 1.0f, Parameter::LINEAR);

  pVerbAmt.Init(hw.knobs[Hothouse::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);

  pTremSpeed.Init(hw.knobs[Hothouse::KNOB_2], TREMOLO_SPEED_MIN, TREMOLO_SPEED_MAX, Parameter::LOGARITHMIC);
  pTremDepth.Init(hw.knobs[Hothouse::KNOB_3], 0.0f, 1.0f, Parameter::LINEAR);

  pDelayTime.Init(hw.knobs[Hothouse::KNOB_4], hw.AudioSampleRate() * DELAY_TIME_MIN_SECONDS, MAX_DELAY, Parameter::LOGARITHMIC);
  pDelayFeedback.Init(hw.knobs[Hothouse::KNOB_5], 0.0f, 1.0f, Parameter::LINEAR);
  pDelayAmt.Init(hw.knobs[Hothouse::KNOB_6], 0.0f, DELAY_DRY_WET_PERCENT_MAX, Parameter::LINEAR);

  delMemL.Init();
  delMemR.Init();
  delayL.del = &delMemL;
  delayR.del = &delMemR;

  osc.Init(hw.AudioSampleRate());

  // Initialize harmonic tremolo filters (state variable filters for crossover)
  harmonicFilterL.Init(hw.AudioSampleRate());
  harmonicFilterR.Init(hw.AudioSampleRate());
  harmonicFilterL.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonicFilterR.SetFreq(HARMONIC_TREMOLO_CROSSOVER_FREQ);
  harmonicFilterL.SetRes(0.5f);  // Minimal resonance for flat response
  harmonicFilterR.SetRes(0.5f);

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

  verb.setSampleRate(SAMPLE_RATE);
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
    TV_MAKEUP_GAIN_NORMAL,      // makeupGainMode
    true,                       // bypassReverb (defensive default: bypassed)
    true,                       // bypassDelay (defensive default: bypassed)
    true                        // bypassTremolo (defensive default: bypassed)
  };
  savedSettings.Init(defaultSettings);

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
    isFactoryResetMode = true;
  } else {
    hw.StartAudio(audioCallback);
  }
  
  while (true) {
    // Check for tap tempo timeout
    checkTapTempoTimeout();

    // Check for DFU mode (both switches held)
    checkDfuModeBothSwitches();

    if(triggerSettingsSave) {
      savedSettings.Save(); // Writing locally stored settings to the external flash
      triggerSettingsSave = false;
    } else if (isFactoryResetMode) {
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
        ledLeft.Set(ledToggle ? 1.0f : 0.0f);
        ledRight.Set(ledToggle ? 0.0f : 1.0f);
        ledLeft.Update();
        ledRight.Update();
      }

      float lowKnobThreshold = 0.05;
      float highKnobThreshold = 0.95;
      float blinkFasterAmount = 300; // each stage removes this many MS from the factory reset blinking
      float knob1Value = pKnob1.Process();
      if (factoryResetStage == 0 && knob1Value >= highKnobThreshold) {
        factoryResetStage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factoryResetStage == 1 && knob1Value <= lowKnobThreshold) {
        factoryResetStage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factoryResetStage == 2 && knob1Value >= highKnobThreshold) {
        factoryResetStage++;
        blinkInterval -= blinkFasterAmount; // make the blinking faster as a UI feedback that the stage has been met
        quickLedFlash();
      } else if (factoryResetStage == 3 && knob1Value <= lowKnobThreshold) {
        savedSettings.RestoreDefaults();
        loadSettings();
        quickLedFlash();          

        hw.StartAudio(audioCallback);
        factoryResetStage = 0;
        bypassDelay = true;
        bypassTrem = true;
        pedalMode = PEDAL_MODE_NORMAL;
        isFactoryResetMode = false;
      }
    }
    hw.DelayMs(10);
  }

  return 0;
}
