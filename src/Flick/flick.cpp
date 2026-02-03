// Flick for Hothouse DIY DSP Platform
// Copyright (C) 2024 Boyd Timothy <btimothy@gmail.com>
// Refactored with modular effect classes (ReverbEffect, TremoloEffect, DelayEffect).

#include "daisy.h"
#include "daisysp.h"
#include "hothouse.h"
#include "flick_core.h"

using clevelandmusicco::Hothouse;
using daisy::AudioHandle;
using daisy::PersistentStorage;
using daisy::SaiHandle;
using daisy::System;
using daisysp::DelayLine;

Hothouse hw;

// Persistent Settings Storage
PersistentStorage<Settings> SavedSettings(hw.seed.qspi);

// Core Logic
FlickCore core;

// Delay Memory in SDRAM
DelayLine<float, MAX_DELAY_SIZE> DSY_SDRAM_BSS delMemL;
DelayLine<float, MAX_DELAY_SIZE> DSY_SDRAM_BSS delMemR;

// Global LEDs
daisy::Led led_left, led_right;

// Forward declarations
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size);
void handleNormalPress(Hothouse::Switches footswitch);
void handleDoublePress(Hothouse::Switches footswitch);
void handleLongPress(Hothouse::Switches footswitch);
void checkDfuModeBothSwitches();

void loadSettings() {
    Settings &localSettings = SavedSettings.GetSettings();
    core.SetSettings(localSettings);
}

void quickLedFlash() {
    led_left.Set(1.0f); led_right.Set(1.0f);
    led_left.Update(); led_right.Update();
    hw.DelayMs(500);
}

int main() {
    hw.Init(true);
    hw.SetAudioBlockSize(8);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    
    led_left.Init(hw.seed.GetPin(Hothouse::LED_1), false);
    led_right.Init(hw.seed.GetPin(Hothouse::LED_2), false);

    // Initialize Core controls
    core.InitControls(&hw.knobs[Hothouse::KNOB_1], &hw.knobs[Hothouse::KNOB_2],
                      &hw.knobs[Hothouse::KNOB_3], &hw.knobs[Hothouse::KNOB_4],
                      &hw.knobs[Hothouse::KNOB_5], &hw.knobs[Hothouse::KNOB_6]);

    core.Init(hw.AudioSampleRate(), &delMemL, &delMemR);

    // Set defaults
    core.RestoreDefaults();
    SavedSettings.Init(core.GetSettings()); // Init with core defaults

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
        core.SetFactoryResetMode(true);
    } else {
        hw.StartAudio(AudioCallback);
    }

    while(true) {
        core.CheckTapTempoTimeout();
        checkDfuModeBothSwitches();

        if (core.ShouldSaveSettings()) {
            SavedSettings.Save();
            core.ClearSaveFlag();
        } else if (core.IsFactoryResetMode()) {
            hw.ProcessAllControls();
            
            core.UpdateFactoryReset();
            
            led_left.Set(core.GetLedLeftBrightness());
            led_right.Set(core.GetLedRightBrightness());
            led_left.Update();
            led_right.Update();
            
            if (!core.IsFactoryResetMode()) {
                loadSettings();
                hw.StartAudio(AudioCallback);
            }
        }
        hw.DelayMs(10);
    }
    return 0;
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    hw.ProcessAllControls();

    int sw1 = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1);
    int sw2 = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_2);
    int sw3 = hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_3);

    core.ProcessControls(sw1, sw2, sw3);
    
    core.ProcessAudio(in[0], in[1], out[0], out[1], size);

    led_left.Set(core.GetLedLeftBrightness());
    led_right.Set(core.GetLedRightBrightness());
    led_left.Update();
    led_right.Update();
}

void handleNormalPress(Hothouse::Switches footswitch) {
    core.HandleNormalPress((int)footswitch);
}

void handleDoublePress(Hothouse::Switches footswitch) {
    core.HandleDoublePress((int)footswitch);
}

void handleLongPress(Hothouse::Switches footswitch) {
    core.HandleLongPress((int)footswitch);
}

// DFU mode detection
constexpr uint32_t DFU_BOTH_SWITCHES_HOLD_TIME_MS = 5000;
uint32_t both_switches_press_start_time = 0;
bool both_switches_pressed = false;

void checkDfuModeBothSwitches() {
  bool fs1 = hw.switches[Hothouse::FOOTSWITCH_1].Pressed();
  bool fs2 = hw.switches[Hothouse::FOOTSWITCH_2].Pressed();

  if (fs1 && fs2) {
    if (!both_switches_pressed) {
      both_switches_press_start_time = System::GetNow();
      both_switches_pressed = true;
    } else {
      if (System::GetNow() - both_switches_press_start_time >= DFU_BOTH_SWITCHES_HOLD_TIME_MS) {
        // Flash LEDs
        for (int i = 0; i < 5; i++) {
          led_left.Set(1.0f); led_right.Set(0.0f); led_left.Update(); led_right.Update();
          System::Delay(100);
          led_left.Set(0.0f); led_right.Set(1.0f); led_left.Update(); led_right.Update();
          System::Delay(100);
        }
        System::ResetToBootloader();
      }
    }
  } else {
    both_switches_pressed = false;
  }
}
