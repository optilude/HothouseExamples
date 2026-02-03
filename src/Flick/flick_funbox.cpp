// Flick for Funbox DIY DSP Platform
// Refactored with modular effect classes (ReverbEffect, TremoloEffect, DelayEffect).
//
// NOTE: This file is draft/untestable in this repository as it requires the 
// Funbox hardware abstraction layer. It serves as a template showing how the
// same FlickCore can be used with different hardware platforms.
//
// For the original Funbox implementation, see:
// https://github.com/joulupukki/FunBox/blob/joulupukki/add-flick/software/Flick/flick.cpp

#include "daisy.h"
#include "daisysp.h"
// #include "funbox_gpl.h"  // Funbox hardware abstraction (not available in this repo)
#include "flick_core.h"
#include "PlateauNEVersio/dsp/delays/InterpDelay.hpp"

// Funbox namespace (stub for compilation reference)
namespace flick {
    // This is a stub - actual implementation would come from funbox_gpl.h
    class Funbox {
    public:
        enum Switches { FOOTSWITCH_1, FOOTSWITCH_2 };
        enum Knobs { KNOB_1, KNOB_2, KNOB_3, KNOB_4, KNOB_5, KNOB_6 };
        enum LEDs { LED_1, LED_2 };
        enum Toggles { TOGGLESWITCH_1, TOGGLESWITCH_2, TOGGLESWITCH_3 };
        
        struct FootswitchCallbacks {
            void (*HandleNormalPress)(Switches);
            void (*HandleDoublePress)(Switches);
            void (*HandleLongPress)(Switches);
        };
        
        daisy::DaisySeed seed;
        daisy::AnalogControl knobs[6];
        daisy::Switch switches[2];
        
        void Init(bool) {}
        void SetAudioBlockSize(size_t) {}
        void SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate) {}
        float AudioSampleRate() { return 48000.0f; }
        void RegisterFootswitchCallbacks(FootswitchCallbacks*) {}
        void StartAdc() {}
        void StartAudio(daisy::AudioHandle::AudioCallback) {}
        void ProcessAllControls() {}
        void DelayMs(uint32_t) {}
        void CheckResetToBootloader() {}
        int GetToggleswitchPosition(Toggles) { return 1; }
    };
}

using flick::Funbox;
using daisy::AudioHandle;
using daisy::PersistentStorage;
using daisy::SaiHandle;
using daisy::System;
using daisysp::DelayLine;

Funbox hw;

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
void handleNormalPress(Funbox::Switches footswitch);
void handleDoublePress(Funbox::Switches footswitch);
void handleLongPress(Funbox::Switches footswitch);
// funbox checkReset logic is inside Funbox class usually

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
    // Funbox might use different sample rate default, but code says 48kHz
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    
    led_left.Init(hw.seed.GetPin(Funbox::LED_1), false);
    led_right.Init(hw.seed.GetPin(Funbox::LED_2), false);

    // Initialize Core controls
    // Use Funbox constants
    core.InitControls(&hw.knobs[Funbox::KNOB_1], &hw.knobs[Funbox::KNOB_2],
                      &hw.knobs[Funbox::KNOB_3], &hw.knobs[Funbox::KNOB_4],
                      &hw.knobs[Funbox::KNOB_5], &hw.knobs[Funbox::KNOB_6]);

    // Zero out the Dattorro reverb SDRAM buffers before initializing
    // Note: 50 buffers of 144000 samples each (defined in InterpDelay.hpp)
    for(int i = 0; i < 50; i++) {
        for(int j = 0; j < 144000; j++) {
            sdramData[i][j] = 0.f;
        }
    }
    // Set hold to 1.0 or plate reverb won't produce output
    hold = 1.f;

    // Initialize Core with pointer to delay lines
    core.Init(hw.AudioSampleRate(), &delMemL, &delMemR);

    // Set defaults
    core.RestoreDefaults();
    SavedSettings.Init(core.GetSettings()); // Init with core defaults

    loadSettings();

    Funbox::FootswitchCallbacks callbacks = {
        .HandleNormalPress = handleNormalPress,
        .HandleDoublePress = handleDoublePress,
        .HandleLongPress = handleLongPress
    };
    hw.RegisterFootswitchCallbacks(&callbacks);

    hw.StartAdc();
    hw.ProcessAllControls();

    if (hw.switches[Funbox::FOOTSWITCH_2].RawState()) {
        core.SetFactoryResetMode(true);
    } else {
        hw.StartAudio(AudioCallback);
    }

    while(true) {
        core.CheckTapTempoTimeout();
        
        // Funbox standard bootloader check - only in normal mode
        if (core.GetPedalMode() == PEDAL_MODE_NORMAL) {
            hw.CheckResetToBootloader();
        }

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

    int sw1 = hw.GetToggleswitchPosition(Funbox::TOGGLESWITCH_1);
    int sw2 = hw.GetToggleswitchPosition(Funbox::TOGGLESWITCH_2);
    int sw3 = hw.GetToggleswitchPosition(Funbox::TOGGLESWITCH_3);

    core.ProcessControls(sw1, sw2, sw3);
    
    // Core processes audio in place if we pass same buffers? 
    // Original core uses in_l, in_r, out_l, out_r.
    // We can pass in[0], in[1] as in.
    
    // Warning: ProcessAudio expects float*, Funbox gives const float* for InputBuffer?
    // AudioHandle::InputBuffer is const float**.
    core.ProcessAudio(in[0], in[1], out[0], out[1], size);
    
    // Update LEDs
    led_left.Set(core.GetLedLeftBrightness());
    led_right.Set(core.GetLedRightBrightness());
    led_left.Update();
    led_right.Update();
}

void handleNormalPress(Funbox::Switches footswitch) {
    // Map Funbox switch enum to int explicitly to match Core expectation
    // Assuming 0 = Left (Sw1), 1 = Right (Sw2)
    int idx = 0;
    if (footswitch == Funbox::FOOTSWITCH_1) idx = 0;
    else if (footswitch == Funbox::FOOTSWITCH_2) idx = 1;
    
    core.HandleNormalPress(idx);
}

void handleDoublePress(Funbox::Switches footswitch) {
    int idx = 0;
    if (footswitch == Funbox::FOOTSWITCH_1) idx = 0;
    else if (footswitch == Funbox::FOOTSWITCH_2) idx = 1;
    
    core.HandleDoublePress(idx);
}

void handleLongPress(Funbox::Switches footswitch) {
    int idx = 0;
    if (footswitch == Funbox::FOOTSWITCH_1) idx = 0;
    else if (footswitch == Funbox::FOOTSWITCH_2) idx = 1;

    core.HandleLongPress(idx);
}
