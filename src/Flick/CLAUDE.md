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
├── flick.cpp                          # Main application (775 lines)
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
└── README.md                          # User documentation
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

**[handle_normal_press()](flick.cpp:344-379)** - Footswitch bypass control
- Toggles effect bypass states
- Saves/cancels edit mode changes

**[handle_double_press()](flick.cpp:381-399)** - Enter edit modes
- Left footswitch: Reverb edit mode
- Right footswitch: Toggle tremolo

**[handle_long_press()](flick.cpp:401-411)** - Advanced features
- Right footswitch long press: Mono/Stereo configuration mode

#### Pedal Modes

**PEDAL_MODE_NORMAL** (line 46)
- Standard performance mode
- All knobs control their labeled functions
- Toggle switches control reverb mode, tremolo waveform, makeup gain

**PEDAL_MODE_EDIT_REVERB** (line 47)
- Activated by double-pressing left footswitch
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
- SWITCH_3 controls mono/stereo mode:
  - DOWN: MIMO (Mono In, Mono Out)
  - MIDDLE: MISO (Mono In, Stereo Out)
  - UP: SISO (Stereo In, Stereo Out)
- Left footswitch: Cancel and exit
- Right footswitch: Save and exit

### Settings Structure: [Settings](flick.cpp:58-86)

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
};
```

**Version Control**: When `SETTINGS_VERSION` (line 39) is incremented, saved settings are invalidated and defaults are restored on next boot.

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
| **KNOB_2** | Tremolo Speed | 0.2-16 Hz | Logarithmic curve |
| **KNOB_3** | Tremolo Depth | 0-100% | Linear |
| **KNOB_4** | Delay Time | 50ms-4sec | Logarithmic curve |
| **KNOB_5** | Delay Feedback | 0-100% | Linear |
| **KNOB_6** | Delay Mix | 0-100% | Linear |
| **SWITCH_1** | Reverb Knob Mode | 3-position | UP=Wet Only, MID=Mix, DOWN=Dry Only |
| **SWITCH_2** | Tremolo Waveform | 3-position | UP=Square, MID=Triangle, DOWN=Sine |
| **SWITCH_3** | Makeup Gain | 3-position | UP=Heavy (+6dB), MID=Normal (+2-4dB), DOWN=None |
| **FOOTSWITCH_1** | Reverb Bypass | Momentary | Double-press for edit mode |
| **FOOTSWITCH_2** | Delay Bypass | Momentary | Double-press for tremolo toggle |

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

### Makeup Gain

Applied to delay and tremolo to compensate for perceived volume loss ([lines 477, 583, 593](flick.cpp:477)):

**Delay Makeup Gain**:
- TV_MAKEUP_GAIN_NONE: 1.0x (0dB)
- TV_MAKEUP_GAIN_NORMAL: 1.66x (+4.4dB)
- TV_MAKEUP_GAIN_HEAVY: 2.0x (+6dB)

**Tremolo Makeup Gain**:
- TV_MAKEUP_GAIN_NONE: 1.0x (0dB)
- TV_MAKEUP_GAIN_NORMAL: 1.2x (+1.6dB)
- TV_MAKEUP_GAIN_HEAVY: 1.6x (+4dB)

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

**LED_1 (Left LED)** ([line 457](flick.cpp:457)):
- Normal mode: ON when reverb active, OFF when bypassed
- Edit reverb mode: Flashes synchronously with right LED
- Edit mono/stereo mode: Flashes alternately with right LED
- Factory reset mode: Flashes alternately, faster with each stage

**LED_2 (Right LED)** ([line 468](flick.cpp:468)):
- Delay only: 100% brightness
- Tremolo only: 40% pulsing at tremolo rate
- Both: 100% pulsing at tremolo rate
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
- Double-press detection (within ~300ms window)
- Long-press detection (>1 second)
- Press-and-hold for bootloader entry (2 seconds)

Callbacks are registered at [lines 701-706](flick.cpp:701-706).

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
| SETTINGS_VERSION | 1 | [line 39](flick.cpp:39) | Forces reset on structure change |
| MAX_DELAY | 96000 samples | [line 43](flick.cpp:43) | 4 seconds at 48kHz |
| minus18dBGain | 0.12589254 | [line 206](flick.cpp:206) | Input attenuation for reverb |
| minus20dBGain | 0.1 | [line 207](flick.cpp:207) | Additional input attenuation |
| Tremolo Speed | 0.2-16 Hz | [line 653](flick.cpp:653) | LFO frequency range |
| Delay Time | 0.05-4 sec | [line 656](flick.cpp:656) | Delay range |
| Pre-Delay | 0-0.25 sec | [line 511](flick.cpp:511) | Reverb pre-delay |

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
| Audio callback | [flick.cpp](flick.cpp) | 425-628 |
| Delay processing | [flick.cpp](flick.cpp) | 572-588 |
| Tremolo processing | [flick.cpp](flick.cpp) | 590-597 |
| Reverb processing | [flick.cpp](flick.cpp) | 599-617 |
| Settings structure | [flick.cpp](flick.cpp) | 58-86 |
| Normal mode controls | [flick.cpp](flick.cpp) | 479-507 |
| Edit mode controls | [flick.cpp](flick.cpp) | 508-557 |
| Footswitch handlers | [flick.cpp](flick.cpp) | 344-411 |
| Main initialization | [flick.cpp](flick.cpp) | 630-775 |
| Oscillator class | [flick_oscillator.h](flick_oscillator.h) | 10-132 |
| PolyBLEP algorithm | [flick_oscillator.cpp](flick_oscillator.cpp) | 79-90 |
| Dattorro interface | [Dattorro.hpp](PlateauNEVersio/Dattorro.hpp) | 162-235 |

### Control Quick Map (Normal Mode)

```
KNOB 1: Reverb Mix    SWITCH 1: Reverb Mode     FOOTSWITCH 1: Reverb On/Off
KNOB 2: Trem Speed    SWITCH 2: Trem Wave       FOOTSWITCH 2: Delay On/Off
KNOB 3: Trem Depth    SWITCH 3: Makeup Gain
KNOB 4: Delay Time
KNOB 5: Delay Feedback
KNOB 6: Delay Mix
```

---

*This documentation is intended for AI assistants and developers modifying the Flick effects pedal. Last updated: 2026-01-02*
