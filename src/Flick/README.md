# Flick

Contributed by Boyd Timothy, GitHub: [joulupukki](https://github.com/joulupukki)

This is a reverb, tremolo, and delay pedal. The original goal of this pedal was to displace the Strymon Flint (Reverb and Tremolo) and a delay pedal to save space on a small pedal board.

### Effects

**Platerra Reverb:** This is a plate reverb based on the Dattorro reverb.

**Tremolo:** Tremolo with sine wave, square wave, and harmonic tremolo settings. Harmonic tremolo splits the signal into high and low frequency bands and modulates them 180° out of phase for a swirling, phase-like effect.

**Delay:** Digital delay with tap tempo and delay subdivisions (normal, dotted eighth, quarter note triplet).

### Demo

The hardware used in this feature walkthrough video is build with Funbox hardware but the Hothouse version works the same (28 June 2025):

[![Demo Video](https://img.youtube.com/vi/pWW68mqj2iQ/0.jpg)](https://www.youtube.com/watch?v=pWW68mqj2iQ)

Updated demo video (6 January 2025):

[![Demo Video](https://img.youtube.com/vi/RR4Hccq0VbE/0.jpg)](https://www.youtube.com/watch?v=RR4Hccq0VbE)

### Controls (Normal Mode)

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Reverb Dry/Wet Amount |  |
| KNOB 2 | Tremolo Speed | Overridden by tap tempo until knob is moved >5%. |
| KNOB 3 | Tremolo Depth |  |
| KNOB 4 | Delay Time | Sets master tempo for delay subdivisions. Overridden by tap tempo until knob is moved >5%. |
| KNOB 5 | Delay Feedback |  |
| KNOB 6 | Delay Dry/Wet Amount |  |
| SWITCH 1 | Reverb knob function | **UP** - 0% Dry, 0-100% Wet<br/>**MIDDLE** - Dry/Wet Mix<br/>**DOWN** - 100% Dry, 0-100% Wet |
| SWITCH 2 | Tremolo Mode | **UP** - Square wave<br/>**MIDDLE** - Sine wave<br/>**DOWN** - Harmonic tremolo |
| SWITCH 3 | Delay Subdivision | **UP** - Dotted eighth (3/4)<br/>**MIDDLE** - Normal (1:1)<br/>**DOWN** - Quarter note triplet (2/3) |
| FOOTSWITCH 1 | Reverb On/Off | **Press:** Toggle reverb on/off<br/>**Double-press:** Enter Tap Tempo mode (see below)<br/>**Long press:** Enter Reverb Edit mode (see below) |
| FOOTSWITCH 2 | Delay/Tremolo On/Off | **Press:** Toggle delay on/off<br/>**Double-press:** Toggle tremolo on/off<br/>**Long press:** Enter Mono-Stereo Edit mode (see below)<br/><br/>**LED:**<br/>- 100% when only delay is active<br/>- 40% pulsing when only tremolo is active<br/>- 100% pulsing when both are active |

### DFU Mode (Firmware Update)

To enter DFU mode for firmware updates via USB:
- Hold both footswitches for 5+ seconds
- LEDs will flash alternately 5 times, then device enters bootloader mode
- Use `make program_dfu` to flash new firmware

### Controls (Tap Tempo Mode)

*Entered by double-pressing FOOTSWITCH 1 in normal mode.*

**LED Indication:**
- **LED 1 (left):** Pulses slowly
- **LED 2 (right):** Blinks at current tempo (slow pulse if no tempo set yet)

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| FOOTSWITCH 1 | **EXIT** Tap Tempo Mode | Returns to normal mode without changing tempo |
| FOOTSWITCH 2 | **TAP** Tempo | Each tap sets the delay time and tremolo speed based on interval between taps<br/>Tempo range: 50ms - 4 seconds (15-1200 BPM) |

**Behavior:**
- Tap tempo overrides KNOB 2 (tremolo speed) and KNOB 4 (delay time)
- Moving either knob more than 5% returns control to that knob independently
- Delay subdivisions (SWITCH 3) apply to tap tempo delay time
- Tremolo speed is set to the tap tempo frequency (clamped to 0.2-16 Hz)
- Mode auto-exits after 5 seconds of no taps
- Tap tempo does **not** persist across power cycles

### Controls (Reverb Edit Mode)

*Entered by long-pressing FOOTSWITCH 1. Both LEDs flash synchronously when in edit mode.*

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| KNOB 1 | Reverb Amount (Wet) | Not saved. Just here for convenience. |
| KNOB 2 | Pre Delay | 0 for Off, up to 0.25 |
| KNOB 3 | Decay |  |
| KNOB 4 | Tank Diffusion |  |
| KNOB 5 | Input High Cutoff Frequency |  |
| KNOB 6 | Tank High Cutoff Frequency |  |
| SWITCH 1 | Tank Mod Speed | **UP** - High<br/>**MIDDLE** - Medium<br/>**DOWN** - Low |
| SWITCH 2 | Tank Mod Depth | **UP** - High<br/>**MIDDLE** - Medium<br/>**DOWN** - Low |
| SWITCH 3 | Tank Mod Shape | **UP** - High<br/>**MIDDLE** - Medium<br/>**DOWN** - Low |
| FOOTSWITCH 1 | **CANCEL** & Exit | Discards parameter changes and exits Reverb Edit Mode |
| FOOTSWITCH 2 | **SAVE** & Exit | Saves all parameters and exits Reverb Edit Mode |

### Controls (Mono-Stereo Edit Mode)

*Entered by long-pressing FOOTSWITCH 2. Both LEDs flash alternately when in Mono-Stereo Edit mode.*

| CONTROL | DESCRIPTION | NOTES |
|-|-|-|
| SWITCH 2 | **Makeup Gain** | **UP** - Heavy (+6dB delay, +4dB tremolo)<br/>**MIDDLE** - Normal (+4.4dB delay, +1.6dB tremolo)<br/>**DOWN** - None (0dB) |
| SWITCH 3 | Mono-Stereo Mode | **UP** - Stereo In, Stereo Out<br/>**MIDDLE** - Mono In, Stereo Out<br/>**DOWN** - Mono In, Mono Out |
| FOOTSWITCH 1 | **CANCEL** & Exit | Discards parameter changes and exits Mono-Stereo Edit Mode |
| FOOTSWITCH 2 | **SAVE** & Exit | Saves makeup gain and mono-stereo settings, then exits |

**Note:** Makeup gain setting is **saved to flash** and persists across power cycles.

### Factory Reset (Restore default reverb parameters)

To enter factory reset mode, **press and hold** **Footswitch #2** when powering the pedal. The LED lights will alternatively blink slowly.

1. Rotate Knob #1 to 100%. The LEDs will quickly flash simultaneously and start blinking faster.
2. Rotate Knob #1 to 0%. The LEDs will quickly flash simultaneously and start blinking faster.
3. Rotate Knob #1 to 100%. The LEDs will quickly flash simultaneously and start blinking faster.
4. Rotate Knob #1 to 0%. The LEDs will quickly flash simultaneously, defaults will be restored, and the pedal will resume normal pedal mode.

To exit factory reset mode without resetting. Power off the pedal and power it back on.
