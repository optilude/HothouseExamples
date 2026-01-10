# Flick - Guitar Effects Pedal for Daisy Seed / Hothouse Platform

## Project Overview

**Flick** is a professional-grade guitar effects pedal combining three high-quality effects (Reverb, Delay, Tremolo) into a single unit. Built for the Electrosmith Daisy Seed microcontroller running inside a Cleveland Music Co. Hothouse pedal enclosure.

- **Author**: Boyd Timothy (@joulupukki)
- **License**: GNU General Public License v3.0 or later
- **Platform**: STM32H750xx (Cortex-M7 @ 480 MHz)
- **Sample Rate**: 48 kHz
- **Audio Block Size**: 8 samples

**Purpose**: To replace multiple pedals (Strymon Flint + delay) with a single, space-efficient unit for small pedalboards.

## Hardware Platform

The Hothouse provides:
- **6 Potentiometers** (KNOB_1 through KNOB_6) - Analog control inputs
- **3 Three-Position Toggle Switches** (TOGGLESWITCH_1 through TOGGLESWITCH_3)
- **2 Momentary Footswitches** (FOOTSWITCH_1, FOOTSWITCH_2)
- **2 LEDs** (LED_1, LED_2) - Status indication
- **Stereo Audio I/O** - Via SAI interface
- **QSPI Flash** - For persistent parameter storage
- **64MB SDRAM** - For large delay buffers (reverb and delay lines)

## Architecture

### Signal Flow

```
Input (L/R)
    ↓
Mono/Stereo Input Selection
    ↓
Delay Effect (L/R parallel)
    ↓
Tremolo Modulation (L/R parallel)
    ↓
Dattorro Plate Reverb (stereo processing)
    ↓
Mono/Stereo Output Selection
    ↓
Output (L/R)
```

### File Structure

```
Flick/
├── flick.cpp                          # Main application (~1100 lines)
├── flick_oscillator.h/.cpp            # Tremolo waveform generator
├── PlateauNEVersio/                   # Dattorro reverb implementation
│   ├── Dattorro.hpp/.cpp              # High-level reverb interface
│   └── dsp/
│       ├── delays/                    # Delay line implementations
│       │   ├── AllpassFilter.hpp      # All-pass IIR filters
│       │   └── InterpDelay.hpp/.cpp   # Interpolated delay lines
│       ├── filters/                   # Filter modules
│       │   └── OnePoleFilters.hpp/.cpp
│       └── modulation/                # Modulation sources
│           └── LFO.hpp                # Triangle/Sawtooth LFO
├── Makefile                           # Build configuration
├── README.md                          # User documentation
├── CLAUDE.md                          # Developer/AI documentation (this file)
└── IMPLEMENTATION_PLAN.md            # Tap tempo & harmonic tremolo implementation plan
```

## Code Organization

### Main File: [flick.cpp](flick.cpp)

#### Key Global Objects
- `Hothouse hw` - Hardware abstraction layer (line 41)
- `Dattorro verb` - Reverb engine (line 97)
- `DelayLine<float, MAX_DELAY> delMemL/R` - Delay buffers in SDRAM (lines 94-95)
- `FlickOscillator osc` - Tremolo LFO (line 91)
- `PersistentStorage<Settings> SavedSettings` - Flash storage (line 89)

#### Core Functions

**[AudioCallback()](flick.cpp:425-628)** - Real-time audio processing
- Called at 48 kHz sample rate
- Processes all controls and effects
- Handles LED updates
- Implements three pedal modes

**[load_settings()](flick.cpp:252-285)** - Load parameters from flash
- Validates settings version
- Restores defaults if version mismatch
- Applies all reverb parameters to verb object

**[save_settings()](flick.cpp:287-302)** - Save parameters to flash
- Sets trigger flag for main loop to execute save
- Non-blocking to avoid audio glitches

**[handle_normal_press()](flick.cpp:420-457)** - Footswitch bypass control
- Handles tap tempo taps when in tap tempo mode
- Toggles effect bypass states
- Saves/cancels edit mode changes

**[handle_double_press()](flick.cpp:459-476)** - Enter special modes
- Left footswitch: Enter tap tempo mode
- Right footswitch: Toggle tremolo

**[handle_long_press()](flick.cpp:478-492)** - Advanced features
- Left footswitch long press: Reverb edit mode
- Right footswitch long press: Mono/Stereo configuration mode

**[enter_tap_tempo_mode()](flick.cpp:475-480)** - Enter tap tempo
- Activates tap tempo mode
- Preserves existing tempo data for refinement

**[handle_tap_tempo_tap()](flick.cpp:487-515)** - Process tempo taps
- Calculates delay time from tap intervals
- Validates tempo range (50ms - 4 seconds)
- Updates master delay time

**[check_tap_tempo_timeout()](flick.cpp:517-526)** - Auto-exit tap tempo
- Exits tap tempo mode after 5 seconds of inactivity

**[check_dfu_mode_both_switches()](flick.cpp:528-565)** - DFU bootloader entry
- Detects both footswitches held for 5+ seconds
- Flashes LEDs alternately as visual feedback
- Calls System::ResetToBootloader()

#### Pedal Modes

**PEDAL_MODE_NORMAL** (line 46)
- Standard performance mode
- All knobs control their labeled functions
- SWITCH_1: Reverb knob mode (dry/wet/mix)
- SWITCH_2: Tremolo mode (Square/Sine/**Harmonic**)
- SWITCH_3: Delay subdivision (Dotted 8th/Normal/Quarter triplet)

**PEDAL_MODE_TAP_TEMPO** (line 49)
- Activated by double-pressing left footswitch
- LED_1: Solid on
- LED_2: Blinks at current tempo (or slow pulse if no tempo set)
- FOOTSWITCH_1: Exit tap tempo mode
- FOOTSWITCH_2: Tap to set tempo for both delay and tremolo
- Tempo range: 50ms - 4 seconds (15-1200 BPM)
- Auto-exits after 5 seconds of no taps
- Knob takeover: Moving KNOB_2 (tremolo) or KNOB_4 (delay) >5% returns control to that knob independently
- Tap tempo does **not** persist across restarts

**PEDAL_MODE_EDIT_REVERB** (line 47)
- Activated by long-pressing left footswitch
- LEDs flash synchronously (both on/off together)
- Knobs control advanced reverb parameters:
  - KNOB_1: Reverb wet amount (not saved)
  - KNOB_2: Pre-delay (0-250ms)
  - KNOB_3: Decay time
  - KNOB_4: Tank diffusion
  - KNOB_5: Input high-cut frequency (0-10 pitch scale)
  - KNOB_6: Tank high-cut frequency (0-10 pitch scale)
- Toggle switches control modulation:
  - SWITCH_1: Tank mod speed (0.5/0.25/0.1)
  - SWITCH_2: Tank mod depth (0.5/0.25/0.1)
  - SWITCH_3: Tank mod shape (0.5/0.25/0.1)
- Left footswitch: Cancel and exit
- Right footswitch: Save and exit

**PEDAL_MODE_EDIT_MONO_STEREO** (line 48)
- Activated by long-pressing right footswitch
- LEDs flash alternately (left/right pattern)
- SWITCH_2: Makeup gain (Heavy/Normal/None) - **NEW**
- SWITCH_3: Mono/stereo mode (MIMO/MISO/SISO)
- Left footswitch: Cancel and exit
- Right footswitch: Save and exit (saves makeup gain + mono/stereo mode)

### Settings Structure: [Settings](flick.cpp:81-111)

Persistent parameters stored in QSPI flash:
```cpp
struct Settings {
    int version;               // SETTINGS_VERSION for validation
    float decay;               // Reverb decay time (0-1)
    float diffusion;           // Tank diffusion (0-1)
    float inputCutoffFreq;     // Input high-cut (0-10 pitch)
    float tankCutoffFreq;      // Tank high-cut (0-10 pitch)
    float tankModSpeed;        // LFO speed scaling (0.1-0.5)
    float tankModDepth;        // LFO depth scaling (0.1-0.5)
    float tankModShape;        // LFO shape (0.1-0.5)
    float preDelay;            // Pre-delay time (0-0.25)
    int monoStereoMode;        // MS_MODE_MIMO/MISO/SISO
    int makeupGainMode;        // TV_MAKEUP_GAIN_NONE/NORMAL/HEAVY (NEW)
};
```

**Version Control**: When `SETTINGS_VERSION` (line 39) is incremented, saved settings are invalidated and defaults are restored on next boot. Current version is **3**.

### Oscillator: [flick_oscillator.h](flick_oscillator.h) / [flick_oscillator.cpp](flick_oscillator.cpp)

Implements band-limited waveform generation for tremolo effect.

**Waveform Types** (lines 17-28):
- `WAVE_SIN` - Sine wave (smooth tremolo)
- `WAVE_TRI` - Triangle wave
- `WAVE_SQUARE_ROUNDED` - Soft square wave (default for tremolo)
- `WAVE_POLYBLEP_*` - Band-limited versions to reduce aliasing

**PolyBLEP Algorithm** ([flick_oscillator.cpp:79-90](flick_oscillator.cpp:79-90))
- Polynomial Band-Limited Step
- Reduces aliasing in discontinuous waveforms
- Used for square and sawtooth waves

**Key Methods**:
- `Init(sample_rate)` - Initialize with sample rate
- `SetFreq(freq)` - Set tremolo speed in Hz
- `SetAmp(amp)` - Set tremolo depth (0-1)
- `SetWaveform(wf)` - Select waveform type
- `Process()` - Generate next sample

### Reverb: [PlateauNEVersio/Dattorro.hpp](PlateauNEVersio/Dattorro.hpp)

Jon Dattorro's 1997 plate reverb algorithm adapted from Valley Audio Soft.

**Dattorro Class** - High-level interface:
- `process(leftInput, rightInput)` - Process stereo input
- `getLeftOutput()` / `getRightOutput()` - Retrieve processed audio
- `clear()` - Reset reverb state (clears tails)

**Key Parameters**:
- `setPreDelay(time)` - Pre-delay in seconds (0-0.25)
- `setDecay(decay)` - Decay time multiplier (0-1)
- `setTankDiffusion(diffusion)` - Density control (0-1)
- `setInputFilterHighCutoffPitch(pitch)` - Input damping (0-10 pitch scale)
- `setTankFilterHighCutFrequency(freq)` - Tank damping (0-10 frequency scale)
- `setTankModSpeed(speed)` - Chorus modulation rate (0-8)
- `setTankModDepth(depth)` - Chorus modulation depth (0-15)
- `setTankModShape(shape)` - LFO asymmetry (0-1)

**Architecture**:
1. **Input Stage**: DC blocking, high-pass, low-pass filters
2. **Pre-delay**: Configurable delay before reverb
3. **Input Diffusion**: 4 all-pass filters (141, 107, 379, 277 samples at 29.761 kHz)
4. **Tank**: Dattorro1997Tank with cross-coupled delay lines
5. **Output**: DC blocking filters

**Dattorro1997Tank** - Core reverb engine:
- **Left Channel**: 2 all-pass + 2 delay lines
- **Right Channel**: 2 all-pass + 2 delay lines
- **Cross-Coupling**: 7 tap points mixed between channels
- **Modulation**: 4 independent LFOs (0.10, 0.15, 0.12, 0.18 Hz)
- **Filters**: One-pole high-pass and low-pass in tank loops

**SDRAM Usage**: Reverb delay lines are stored in external SDRAM via `InterpDelay` class. The buffer is initialized at [flick.cpp:671-674](flick.cpp:671-674).

### Delay Effect: [Delay struct](flick.cpp:107-123)

Simple digital delay with feedback.

**Structure**:
```cpp
struct Delay {
    DelayLine<float, MAX_DELAY> *del;  // Pointer to delay buffer
    float currentDelay;                 // Current delay time (smoothed)
    float delayTarget;                  // Target delay time
    float feedback;                     // Feedback amount (0-1)

    float Process(float in);            // Process one sample
};
```

**Smoothing**: Uses one-pole filter (`fonepole`) to smooth delay time changes and prevent artifacts.

**Max Delay**: 4 seconds (96,000 samples at 48 kHz) defined at [line 43](flick.cpp:43).

**Stereo Processing**: Two independent delay lines (`delayL`, `delayR`) for true stereo delay.

## Control Mapping

### Normal Mode Controls

| Control | Function | Range | Notes |
|---------|----------|-------|-------|
| **KNOB_1** | Reverb Dry/Wet | 0-100% | Behavior depends on SWITCH_1 |
| **KNOB_2** | Tremolo Speed | 0.2-16 Hz | Logarithmic curve; overridden by tap tempo |
| **KNOB_3** | Tremolo Depth | 0-100% | Linear |
| **KNOB_4** | Delay Time (Master Tempo) | 50ms-4sec | Logarithmic curve; overridden by tap tempo |
| **KNOB_5** | Delay Feedback | 0-100% | Linear |
| **KNOB_6** | Delay Mix | 0-100% | Linear |
| **SWITCH_1** | Reverb Knob Mode | 3-position | UP=Wet Only, MID=Mix, DOWN=Dry Only |
| **SWITCH_2** | Tremolo Mode | 3-position | UP=Square, MID=Sine, DOWN=**Harmonic** |
| **SWITCH_3** | Delay Subdivision | 3-position | UP=Dotted 8th (1.5x), MID=Normal (1:1), DOWN=Quarter triplet (1.333x) |
| **FOOTSWITCH_1** | Reverb Bypass | Momentary | Press=bypass, Double=tap tempo, Long=reverb edit |
| **FOOTSWITCH_2** | Delay Bypass | Momentary | Press=bypass, Double=tremolo toggle, Long=mono-stereo edit |
| **BOTH FOOTSWITCHES** | DFU Mode | Hold 5+ sec | Enters USB bootloader for firmware updates |

### Parameter Scaling

**Reverb Amount** ([line 475](flick.cpp:475)):
```cpp
plateWet = p_verb_amt.Process();  // 0-1 range
```

**Tremolo** ([lines 480-487](flick.cpp:480-487)):
```cpp
osc.SetFreq(p_trem_speed.Process());      // 0.2-16 Hz
depth = fclamp(p_trem_depth.Process(), 0.f, 1.f) * 0.5f;
osc.SetAmp(depth);                        // 0-0.5 amplitude
dc_os = 1.f - depth;                      // DC offset for bipolar->unipolar
```

**Delay** ([lines 492-494](flick.cpp:492-494)):
```cpp
delayL.delayTarget = p_delay_time.Process();       // 2400-96000 samples
delayL.feedback = p_delay_feedback.Process();      // 0-1
delay_drywet = (int)p_delay_amt.Process();        // 0-100
```

### Reverb Dry/Wet Modes

Controlled by SWITCH_1 in normal mode ([lines 497-507](flick.cpp:497-507)):

**REVERB_KNOB_ALL_DRY** (Switch DOWN):
- Dry signal always 100%
- Knob_1 controls wet from 0-100%
- Total output = dry + wet (can exceed unity gain)

**REVERB_KNOB_DRY_WET_MIX** (Switch MIDDLE):
- `plateDry = 1.0 - plateWet`
- Crossfade between dry and wet
- Constant perceived loudness

**REVERB_KNOB_ALL_WET** (Switch UP):
- Dry signal always 0%
- Knob_1 controls wet from 0-100%
- Pure reverb output

### Tap Tempo ([lines 487-540](flick.cpp:487-540))

**Entry**: Double-press FOOTSWITCH_1 in normal mode

**Operation**:
- Each tap of FOOTSWITCH_2 calculates interval from previous tap
- Interval converted to delay samples: `(interval / MS_PER_SECOND) * 48000.0f`
- Interval converted to tremolo frequency: `MS_PER_SECOND / interval` (Hz)
- Valid range: 50ms - 4 seconds (TAP_TEMPO_SAMPLES_MIN/MAX)
- Sets both `master_delay_time_samples` (multiplied by subdivision) and `tapTempoTremoloFreqHz`
- Tremolo frequency is clamped to 0.2 - 16 Hz range
- Does **not** persist across restarts - relies on knob positions on startup

**Knob Takeover**:
```cpp
// Delay knob (KNOB_4) takeover
float current_knob4_value = hw.knobs[Hothouse::KNOB_4].Value();
if (tap_tempo_controls_delay) {
  if (fabs(current_knob4_value - knob4_last_value) > KNOB_TAKEOVER_THRESHOLD) {
    tap_tempo_controls_delay = false;  // Knob takes back control
  }
}

// Tremolo speed knob (KNOB_2) takeover - independent
float tremSpeedCurrentValue = hw.knobs[Hothouse::KNOB_2].Value();
if (tapTempoControlsTremolo) {
  if (fabs(tremSpeedCurrentValue - tremSpeedLastValue) > KNOB_TAKEOVER_THRESHOLD) {
    tapTempoControlsTremolo = false;  // Knob takes back control
  }
}
```
- Threshold: 5% movement (KNOB_TAKEOVER_THRESHOLD = 0.05f)
- Both knobs are independent - taking control of one doesn't affect the other

**Auto-Exit**: 5 seconds of inactivity (TAP_TEMPO_TIMEOUT_MS)

**LED Indication**:
- LED_1: Solid on
- LED_2: Blinks at tempo (10% duty cycle) or slow pulse if no tempo set

### Delay Subdivisions ([lines 693-746](flick.cpp:693-746))

**Multipliers**:
```cpp
DelaySubdivision subdivision = kDelaySubdivisionMap[TOGGLESWITCH_3];
switch (subdivision) {
  case DELAY_SUBDIV_DOTTED_EIGHTH:
    subdivision_multiplier = 1.5f;      // UP position
    break;
  case DELAY_SUBDIV_QUARTER_TRIPLET:
    subdivision_multiplier = 1.333333f; // DOWN position
    break;
  case DELAY_SUBDIV_NORMAL:
  default:
    subdivision_multiplier = 1.0f;      // MIDDLE position
    break;
}
```

**Application**:
```cpp
float final_delay_time = master_delay_time_samples * subdivision_multiplier;
final_delay_time = daisysp::fclamp(final_delay_time, TAP_TEMPO_SAMPLES_MIN, (float)MAX_DELAY);
delayL.delayTarget = final_delay_time;
delayR.delayTarget = final_delay_time;
```

Subdivisions apply to both knob-controlled and tap tempo delay times.

### Harmonic Tremolo ([lines 870-901](flick.cpp:870-901))

**Concept**: Splits audio into high and low frequency bands, applies tremolo with opposite phase to each band.

**Implementation**:
```cpp
if (trem_mode == TREMOLO_HARMONIC) {
  // Process through state variable filter
  harmonic_filter_L.Process(s_L);
  float low_L = harmonic_filter_L.Low();
  float high_L = harmonic_filter_L.High();

  // Apply tremolo with opposite phase
  float low_mod_L = low_L * (1.0f + lfo_sample);
  float high_mod_L = high_L * (1.0f - lfo_sample);  // Inverted
  s_L = (low_mod_L + high_mod_L) * trem_makeup_gain;
}
```

**Parameters**:
- Crossover frequency: 800 Hz (HARMONIC_TREMOLO_CROSSOVER_FREQ)
- Filter type: State Variable Filter (Svf) from DaisySP
- Filter resonance: 0.5 (minimal resonance for flat response)
- Initialized at [lines 982-988](flick.cpp:982-988)

**Effect**: Creates swirling, phase-like modulation similar to vintage Fender Vibrato.

### Makeup Gain ([lines 823-841](flick.cpp:823-841))

Applied to delay and tremolo to compensate for perceived volume loss. Now controlled by `current_makeup_gain` global variable (set in mono-stereo edit mode, persisted to flash).

**Delay Makeup Gain**:
- TV_MAKEUP_GAIN_NONE: 1.0x (0dB)
- TV_MAKEUP_GAIN_NORMAL: 1.66x (+4.4dB)
- TV_MAKEUP_GAIN_HEAVY: 2.0x (+6dB)

**Tremolo Makeup Gain**:
- TV_MAKEUP_GAIN_NONE: 1.0x (0dB)
- TV_MAKEUP_GAIN_NORMAL: 1.2x (+1.6dB)
- TV_MAKEUP_GAIN_HEAVY: 1.6x (+4dB)

**Note**: Makeup gain setting moved from SWITCH_3 (normal mode) to SWITCH_2 (mono-stereo edit mode) and is now **persisted across restarts**.

## Mono/Stereo Signal Routing

Three modes controlled by `mono_stereo_mode` enum ([lines 51-55](flick.cpp:51-55)):

**MS_MODE_MIMO** (Mono In, Mono Out):
- Input: Uses left channel only ([line 565](flick.cpp:565))
- Processing: Stereo (both L/R process same input)
- Output: Mix L+R to left channel, mute right ([lines 620-621](flick.cpp:620-621))
- Reverb scaling: 5.0x dry, 0.2x reverse ([lines 241-242](flick.cpp:241-242))

**MS_MODE_MISO** (Mono In, Stereo Out):
- Input: Uses left channel only ([line 565](flick.cpp:565))
- Processing: Stereo (both L/R process same input)
- Output: Stereo ([lines 624-625](flick.cpp:624-625))
- Reverb scaling: 2.5x dry, 0.4x reverse ([lines 246-247](flick.cpp:246-247))

**MS_MODE_SISO** (Stereo In, Stereo Out):
- Input: Uses both L and R channels ([lines 568-569](flick.cpp:568-569))
- Processing: True stereo
- Output: Stereo ([lines 624-625](flick.cpp:624-625))
- Reverb scaling: 2.5x dry, 0.4x reverse ([lines 246-247](flick.cpp:246-247))

## LED Indicators

**LED_1 (Left LED)** ([line 650](flick.cpp:650)):
- Normal mode: ON when reverb active, OFF when bypassed
- Tap tempo mode: Solid ON
- Edit reverb mode: Flashes synchronously with right LED
- Edit mono/stereo mode: Flashes alternately with right LED
- Factory reset mode: Flashes alternately, faster with each stage
- DFU mode entry: Flashes alternately with LED_2 (5 cycles)

**LED_2 (Right LED)** ([line 661](flick.cpp:661)):
- Normal mode with delay only: 100% brightness
- Normal mode with tremolo only: 40% pulsing at tremolo rate
- Normal mode with both: 100% pulsing at tremolo rate
- Tap tempo mode: Blinks at current tempo (10% duty cycle) or slow pulse if no tempo set
- Edit modes: Same as LED_1

## Build System

### [Makefile](Makefile)

**Build Command**:
```bash
make clean && make
```

**Output Files** (in `build/`):
- `flick.bin` - Binary firmware (110 KB) - for DFU flashing
- `flick.elf` - Debug executable (2 MB)
- `flick.hex` - Intel HEX format (308 KB)
- `flick.map` - Linker map with symbols (808 KB)

**Programming**:
```bash
make program        # Via ST-Link (SWD)
make program_dfu    # Via DFU bootloader (USB)
```

**Dependencies**:
- `../hothouse.cpp` - Hardware abstraction layer (parent directory)
- `PlateauNEVersio/` - Reverb implementation
- `../../libDaisy` - Daisy Seed HAL
- `../../DaisySP` - DSP library

**Compiler Flags** (from libDaisy):
- `-std=c++17` - C++17 standard
- `-O2` or `-Ofast` - Optimization level
- `-DSTM32H750xx` - Target MCU
- ARM Cortex-M7 specific flags

## Development Workflow

### VSCode Integration

The project includes full VSCode configuration:

**Build Tasks** ([.vscode/tasks.json](.vscode/tasks.json)):
- `Ctrl+Shift+B` - Build project
- `build_and_program` - Flash via ST-Link
- `build_and_program_dfu` - Flash via DFU
- `build_debug` - Debug build with symbols

**Debugging** ([.vscode/launch.json](.vscode/launch.json)):
- `F5` - Build, flash, and start debugging
- Uses OpenOCD + ST-Link
- Breakpoints, watch variables, call stack inspection
- SVD file for peripheral register viewing

**IntelliSense** ([.vscode/c_cpp_properties.json](.vscode/c_cpp_properties.json)):
- Full code completion
- Include paths configured for libDaisy and DaisySP
- ARM cross-compiler integration

### Factory Reset Feature

**Entering Factory Reset Mode** ([flick.cpp:710-714](flick.cpp:710-714)):
1. Hold FOOTSWITCH_2 during power-up
2. LEDs flash alternately (slowly)

**Reset Sequence** ([flick.cpp:720-765](flick.cpp:720-765)):
1. Turn KNOB_1 to 100% (LEDs flash, blink faster)
2. Turn KNOB_1 to 0% (LEDs flash, blink faster)
3. Turn KNOB_1 to 100% (LEDs flash, blink faster)
4. Turn KNOB_1 to 0% (Settings reset, enter normal mode)

**Exit Without Reset**:
- Power cycle the pedal

**What Gets Reset**:
- All reverb parameters to defaults ([lines 686-696](flick.cpp:686-696))
- Mono/stereo mode (defaults to first boot state)

## Common Modification Scenarios

### Adding a New Effect

1. **Create effect object** in global scope (near [line 97](flick.cpp:97))
2. **Add bypass variable** (near [line 165](flick.cpp:165))
3. **Initialize effect** in `main()` (near [line 665](flick.cpp:665))
4. **Add to signal chain** in `AudioCallback()` (near [line 559](flick.cpp:559))
5. **Map controls** in normal mode section ([lines 479-507](flick.cpp:479-507))
6. **Add footswitch handler** in callbacks ([lines 344-411](flick.cpp:344-411))

### Changing Control Mapping

**Knob Parameters** are defined at [lines 101-105](flick.cpp:101-105) and initialized at [lines 643-658](flick.cpp:643-658):
```cpp
p_verb_amt.Init(hw.knobs[Hothouse::KNOB_1], 0.0f, 1.0f, Parameter::LINEAR);
```

Change range by modifying min/max values or curve type (LINEAR/LOGARITHMIC/EXPONENTIAL).

**Toggle Switches** use lookup arrays ([lines 137-153](flick.cpp:137-153)):
```cpp
constexpr ReverbKnobMode kReverbKnobMap[] = {
    REVERB_KNOB_ALL_WET,     // UP
    REVERB_KNOB_DRY_WET_MIX, // MIDDLE
    REVERB_KNOB_ALL_DRY,     // DOWN
};
```

Modify arrays to change switch behavior.

### Adjusting Reverb Character

**Default Values** ([lines 159-214](flick.cpp:159-214)):
```cpp
float plateDecay = 0.8;                // Shorter = less reverb tail
float plateTankDiffusion = 0.85;       // Higher = more dense/smooth
float plateInputDampHigh = 7.25;       // Lower = darker reverb
float plateTankDampHigh = 7.25;        // Lower = darker reverb
float plateTankModSpeed = 0.1;         // LFO speed multiplier
float plateTankModDepth = 0.1;         // LFO depth multiplier
float plateTankModShape = 0.25;        // LFO asymmetry
```

**Scaling Factors** ([lines 238-249](flick.cpp:238-249)):
- `reverbDryScaleFactor` - Input gain to reverb
- `reverbReverseScaleFactor` - Dry signal mixing back with wet

**Attenuation** ([line 606](flick.cpp:606)):
```cpp
verb.process(leftInput * minus18dBGain * minus20dBGain * ...)
```
Adjust gain constants (defined at [lines 206-207](flick.cpp:206-207)) to change reverb input level.

### Adding Persistent Parameters

1. **Add field to Settings struct** ([lines 58-86](flick.cpp:58-86))
2. **Increment SETTINGS_VERSION** ([line 39](flick.cpp:39))
3. **Update inequality operator** ([lines 72-85](flick.cpp:72-85))
4. **Initialize in defaultSettings** ([lines 686-696](flick.cpp:686-696))
5. **Save in save_settings()** ([lines 287-302](flick.cpp:287-302))
6. **Load in load_settings()** ([lines 252-285](flick.cpp:252-285))

### Changing Delay Time Range

**Current Range**: 50ms to 4 seconds ([line 656](flick.cpp:656))

To increase maximum delay:
1. Change `MAX_DELAY` constant ([line 43](flick.cpp:43))
2. Verify SDRAM size is sufficient (current: 64MB)
3. Rebuild project

Note: Longer delays consume more SDRAM, potentially limiting reverb quality.

### Modifying Tremolo Waveforms

**Waveform Selection** ([lines 149-153](flick.cpp:149-153)):
```cpp
constexpr int kWaveformMap[] = {
    FlickOscillator::WAVE_SQUARE_ROUNDED,  // UP
    FlickOscillator::WAVE_TRI,             // MIDDLE
    FlickOscillator::WAVE_SIN,             // DOWN
};
```

Available waveforms from [flick_oscillator.h:17-28](flick_oscillator.h:17-28):
- `WAVE_SIN` - Smooth sine wave
- `WAVE_TRI` - Triangle wave
- `WAVE_SQUARE` - Hard square wave
- `WAVE_POLYBLEP_SQUARE` - Band-limited square
- `WAVE_SQUARE_ROUNDED` - Soft square (default)

Change array values to swap waveforms.

## Important Implementation Details

### Audio Processing Order

Effects are processed in this order within `AudioCallback()` ([lines 559-627](flick.cpp:559-627)):

1. **Mono/Stereo Input Selection** ([lines 564-570](flick.cpp:564-570))
2. **Delay** ([lines 572-588](flick.cpp:572-588))
3. **Tremolo** ([lines 590-597](flick.cpp:590-597))
4. **Reverb** ([lines 599-617](flick.cpp:599-617))
5. **Mono/Stereo Output Selection** ([lines 619-626](flick.cpp:619-626))

To reorder effects, move the corresponding code blocks.

### Reverb Always Processes

Even when bypassed, the reverb continues processing input ([lines 599-607](flick.cpp:599-607)):
```cpp
// Keep sending input to the reverb even if bypassed so that when it's
// enabled again it will already have the current input signal already
// being processed.
verb.process(leftInput * ..., rightInput * ...);

if (!bypass_verb) {
    // Apply reverb output
}
```

This prevents a "dry spell" when re-enabling reverb. The tail is cleared on bypass ([lines 370-374](flick.cpp:370-374)).

### Hard Limiting

Input to reverb is hard-limited to prevent clipping ([line 603](flick.cpp:603)):
```cpp
leftInput = hardLimit100_(s_L) * reverbDryScaleFactor;
```

`hardLimit100_()` function ([lines 413-415](flick.cpp:413-415)) clamps to ±1.0.

### Pop Cancellation

The reverb implementation uses `clearPopCancelValue` (defined in Dattorro code) to fade in/out when clearing to prevent clicks.

### SDRAM Initialization

Critical for reverb to work ([lines 671-678](flick.cpp:671-678)):
```cpp
// Zero out the InterpDelay buffers used by the plate reverb
for(int i = 0; i < 50; i++) {
    for(int j = 0; j < 144000; j++) {
        sdramData[i][j] = 0.;
    }
}
hold = 1.;  // Must be set or reverb won't work
```

These are global variables defined in `InterpDelay.cpp`.

### Footswitch Debouncing

The Hothouse hardware library handles:
- Debouncing
- Double-press detection (within ~600ms window)
- Long-press detection (>1 second)

Callbacks are registered at [lines 1018-1022](flick.cpp:1018-1022).

### DFU Mode (Bootloader Entry)

**Old behavior** (single footswitch): Hothouse HAL provided `CheckResetToBootloader()` which monitored FOOTSWITCH_1 for 2-second hold.

**New behavior** (both footswitches): Custom implementation `check_dfu_mode_both_switches()` [lines 528-565](flick.cpp:528-565):
- Monitors both FOOTSWITCH_1 and FOOTSWITCH_2
- Requires **5 seconds** of simultaneous pressing
- Flashes LEDs alternately (5 cycles) as visual feedback
- Calls `System::ResetToBootloader()` to enter USB bootloader
- Called from main loop at [line 1046](flick.cpp:1046)

This change frees up FOOTSWITCH_1 long-press for reverb edit mode and makes DFU entry more intentional.

### Non-Blocking Settings Save

Settings are saved asynchronously in the main loop ([lines 717-719](flick.cpp:717-719)):
```cpp
if(trigger_settings_save) {
    SavedSettings.Save();
    trigger_settings_save = false;
}
```

This prevents audio glitches from blocking flash writes in the audio callback.

## Key Constants and Ranges

| Constant | Value | Location | Purpose |
|----------|-------|----------|---------|
| **Core Configuration** ||||
| SETTINGS_VERSION | 3 | [line 39](flick.cpp:39) | Forces reset on structure change |
| SAMPLE_RATE | 48000.0f | [line 42](flick.cpp:42) | Audio sample rate in Hz |
| MAX_DELAY | SAMPLE_RATE * 2 | [line 43](flick.cpp:43) | 4 seconds max delay (96000 samples) |
| **Tremolo** ||||
| TREMOLO_SPEED_MIN | 0.2 Hz | [line 46](flick.cpp:46) | Minimum tremolo speed |
| TREMOLO_SPEED_MAX | 16.0 Hz | [line 47](flick.cpp:47) | Maximum tremolo speed |
| TREMOLO_DEPTH_SCALE | 0.5 | [line 48](flick.cpp:48) | Depth scaling (0-0.5 range) |
| TREMOLO_LED_BRIGHTNESS | 0.4 | [line 49](flick.cpp:49) | LED brightness when only tremolo active |
| HARMONIC_TREMOLO_CROSSOVER_FREQ | 800 Hz | [line 246](flick.cpp:246) | Harmonic tremolo filter cutoff |
| **Delay** ||||
| DELAY_TIME_MIN_SECONDS | 0.05 sec | [line 52](flick.cpp:52) | Minimum delay time (50ms) |
| DELAY_WET_MIX_ATTENUATION | 0.333 | [line 53](flick.cpp:53) | Attenuation for wet delay signal |
| DELAY_DRY_WET_PERCENT_MAX | 100.0 | [line 54](flick.cpp:54) | Max dry/wet percentage |
| **Tap Tempo** ||||
| TAP_TEMPO_TIMEOUT_MS | 5000 ms | [line 87](flick.cpp:87) | Auto-exit tap tempo |
| TAP_TEMPO_MIN_INTERVAL_MS | 50 ms | [line 88](flick.cpp:88) | Min tap interval (1200 BPM) |
| TAP_TEMPO_MAX_INTERVAL_MS | 4000 ms | [line 89](flick.cpp:89) | Max tap interval (15 BPM) |
| MS_PER_SECOND | 1000.0 | [line 90](flick.cpp:90) | Milliseconds per second conversion |
| TAP_TEMPO_SAMPLES_MIN | Calculated | [line 91](flick.cpp:91) | 50ms in samples (2400 @ 48kHz) |
| TAP_TEMPO_SAMPLES_MAX | Calculated | [line 92](flick.cpp:92) | 4s in samples (192000 @ 48kHz) |
| TAP_TEMPO_BLINK_DUTY_CYCLE | 0.1 | [line 57](flick.cpp:57) | 10% duty cycle for LED blink |
| KNOB_TAKEOVER_THRESHOLD | 0.05 | [line 230](flick.cpp:230) | 5% knob movement to exit tap tempo |
| **Other** ||||
| DFU_BOTH_SWITCHES_HOLD_TIME_MS | 5000 ms | [line 95](flick.cpp:95) | Both switches for DFU mode |
| MINUS_18DB_GAIN | 0.12589254 | [line 285](flick.cpp:285) | Input attenuation for reverb |
| MINUS_20DB_GAIN | 0.1 | [line 286](flick.cpp:286) | Additional input attenuation |

## Debugging Tips

### Enable Debug Build
```bash
make clean && make DEBUG=1
```
Adds symbols and reduces optimization for easier debugging.

### Check SDRAM Initialization
If reverb sounds broken, verify:
1. `sdramData` buffer is zeroed ([lines 671-674](flick.cpp:671-674))
2. `hold = 1.0` is set ([line 678](flick.cpp:678))
3. No SDRAM allocation errors in linker map

### Audio Glitches
- Reduce audio block size from 8 to 4 or lower
- Check for blocking operations in `AudioCallback()`
- Verify CPU isn't hitting 100% utilization

### Parameter Not Saving
- Check `SETTINGS_VERSION` matches saved version
- Verify `trigger_settings_save` flag is being set
- Check flash write completion in main loop

### LED Not Responding
- Verify LED update rate limiters ([lines 437, 449, 463](flick.cpp:437))
- Check LED initialization in `main()` ([lines 636-637](flick.cpp:636-637))
- Ensure LED Update() is called ([lines 472-473](flick.cpp:472-473))

## Performance Characteristics

**CPU Usage**: Approximately 40-50% at 48kHz sample rate (estimated based on similar Daisy Seed projects)

**Memory Usage**:
- **Flash**: ~110 KB code
- **RAM**: ~8 KB (primarily for audio buffers and state)
- **SDRAM**: ~39 MB (reverb delay lines + dual 4-second delay buffers)

**Latency**:
- Audio block size: 8 samples = 0.167ms
- System latency: <1ms (minimal processing delay)

## License and Credits

**Project License**: GNU GPL v3.0 or later

**Dependencies**:
- **Electrosmith libDaisy** - MIT License
- **Electrosmith DaisySP** - MIT License
- **Valley Audio Plateau Reverb** - GPL v3.0 (Dattorro implementation)
- **VCV Rack** - BSD 3-Clause License
- **Mutable Instruments Grids** - GPL v3.0

**Original Algorithm**:
- Jon Dattorro, "Effect Design, Part 1: Reverberator and Other Filters" (1997)

---

## Quick Reference

### File Line Number Reference

| Feature | File | Lines |
|---------|------|-------|
| Audio callback | [flick.cpp](flick.cpp) | 598-920 |
| Tap tempo mode entry | [flick.cpp](flick.cpp) | 475-480 |
| Tap tempo tap handler | [flick.cpp](flick.cpp) | 487-515 |
| Tap tempo timeout check | [flick.cpp](flick.cpp) | 517-526 |
| DFU mode (both switches) | [flick.cpp](flick.cpp) | 528-565 |
| Delay processing | [flick.cpp](flick.cpp) | 843-857 |
| Delay subdivision logic | [flick.cpp](flick.cpp) | 693-746 |
| Tremolo processing (standard) | [flick.cpp](flick.cpp) | 893-900 |
| Harmonic tremolo processing | [flick.cpp](flick.cpp) | 870-891 |
| Reverb processing | [flick.cpp](flick.cpp) | 903-917 |
| Settings structure | [flick.cpp](flick.cpp) | 81-111 |
| Normal mode controls | [flick.cpp](flick.cpp) | 670-759 |
| Edit mode controls | [flick.cpp](flick.cpp) | 761-808 |
| Footswitch handlers | [flick.cpp](flick.cpp) | 420-492 |
| Main initialization | [flick.cpp](flick.cpp) | 939-1100 |
| Harmonic filter init | [flick.cpp](flick.cpp) | 982-988 |
| Oscillator class | [flick_oscillator.h](flick_oscillator.h) | 10-132 |
| PolyBLEP algorithm | [flick_oscillator.cpp](flick_oscillator.cpp) | 79-90 |
| Dattorro interface | [Dattorro.hpp](PlateauNEVersio/Dattorro.hpp) | 162-235 |

### Control Quick Map (Normal Mode)

```
KNOB 1: Reverb Mix         SWITCH 1: Reverb Mode         FS1 Press:  Reverb On/Off
KNOB 2: Trem Speed         SWITCH 2: Trem Mode           FS1 Double: Tap Tempo Mode
KNOB 3: Trem Depth         SWITCH 3: Delay Subdivision   FS1 Long:   Reverb Edit
KNOB 4: Delay Time         (UP=Dotted 8th, MID=Normal,   FS2 Press:  Delay On/Off
KNOB 5: Delay Feedback      DOWN=Quarter Triplet)        FS2 Double: Tremolo On/Off
KNOB 6: Delay Mix                                        FS2 Long:   Mono-Stereo Edit
                                                          Both 5s:    DFU Mode
```

### New Features (2026-01-10)

- **Tap Tempo**: Double-press FS1, tap FS2 to set **both delay and tremolo** tempo (50ms-4s range)
  - Delay and tremolo knobs independently return control with >5% movement
  - Does not persist across restarts (safer, prevents state misalignment)
- **Delay Subdivisions**: Dotted eighth (1.5x), normal (1:1), quarter triplet (1.333x)
- **Harmonic Tremolo**: Splits signal at 800Hz, modulates high/low bands 180° out of phase
- **Makeup Gain Persistence**: Now saved to flash, set in mono-stereo edit mode (SWITCH_2)
- **DFU Mode Update**: Both footswitches held 5+ seconds (was: FS1 long press)
- **Code Quality**: Named constants (`SAMPLE_RATE`, `MS_PER_SECOND`) replace magic numbers
- **Settings Version**: Current version is 3

---

*This documentation is intended for AI assistants and developers modifying the Flick effects pedal. Last updated: 2026-01-10*
