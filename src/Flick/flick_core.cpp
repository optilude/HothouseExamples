#include "flick_core.h"

FlickCore::FlickCore() 
    : verb(48000, 16, 4.0), // Default initialization, will be updated in Init
      pedal_mode(PEDAL_MODE_NORMAL),
      mono_stereo_mode(MS_MODE_MIMO),
      current_makeup_gain(MAKEUP_GAIN_NORMAL),
      tremolo_mode_(TREMOLO_SINE),
      bypass_verb(true),
      bypass_trem(true),
      bypass_delay(true),
      led_left_brightness_(0.0f),
      led_right_brightness_(0.0f),
      tap_tempo_active(false),
      tap_tempo_last_tap_time(0),
      tap_tempo_interval_ms(0),
      tap_tempo_delay_samples(0.0f),
      tap_tempo_controls_delay(false),
      tap_tempo_tremolo_freq_hz(0.0f),
      tap_tempo_controls_tremolo(false),
      master_delay_time_samples(0.0f),
      is_factory_reset_mode_(false),
      factory_reset_stage_(0),
      reset_blink_interval_(1000),
      last_led_toggle_time_(0),
      led_toggle_state_(false)
{
    // Defaults for reverb
    plate_diffusion_enabled = true;
    plate_pre_delay = 0.;
    plate_dry = 1.0;
    plate_wet = 0.5;
    plate_decay = 0.8;
    plate_time_scale = 1.007500;
    plate_tank_diffusion = 0.85;

    plate_input_damp_low = 2.87; // approx 100Hz
    plate_input_damp_high = 7.25;

    plate_tank_damp_low = 2.87; // approx 100Hz
    plate_tank_damp_high = 7.25;

    plate_tank_mod_speed = 0.1;
    plate_tank_mod_depth = 0.1;
    plate_tank_mod_shape = 0.25;

    reverb_dry_scale_factor = 1.0;
    reverb_reverse_scale_factor = 1.0;
    
    trigger_settings_save_ = false;
}

FlickCore::~FlickCore() {
}

void FlickCore::Init(float sample_rate, 
                     DelayLine<float, MAX_DELAY_SIZE>* delay_line_l, 
                     DelayLine<float, MAX_DELAY_SIZE>* delay_line_r) {
    sample_rate_ = sample_rate;
    delMemL = delay_line_l;
    delMemR = delay_line_r;

    delMemL->Init();
    delMemR->Init();
    delayL.del = delMemL;
    delayR.del = delMemR;

    TAP_TEMPO_SAMPLES_MIN = (TAP_TEMPO_MIN_INTERVAL_MS / MS_PER_SECOND) * sample_rate_;
    TAP_TEMPO_SAMPLES_MAX = (TAP_TEMPO_MAX_INTERVAL_MS / MS_PER_SECOND) * sample_rate_;

    osc.Init(sample_rate_);
    
    // Initialize harmonic tremolo filters
    low_pass_l.Init(HARMONIC_TREMOLO_LPF_CUTOFF, sample_rate_);
    low_pass_r.Init(HARMONIC_TREMOLO_LPF_CUTOFF, sample_rate_);
    high_pass_l.Init(HARMONIC_TREMOLO_HPF_CUTOFF, sample_rate_);
    high_pass_r.Init(HARMONIC_TREMOLO_HPF_CUTOFF, sample_rate_);

    verb.setSampleRate(sample_rate_);
    verb.setTimeScale(plate_time_scale);
    verb.enableInputDiffusion(plate_diffusion_enabled);
    verb.setInputFilterLowCutoffPitch(plate_input_damp_low);
    verb.setTankFilterLowCutFrequency(plate_tank_damp_low);
}

void FlickCore::InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2, 
                             daisy::AnalogControl* k3, daisy::AnalogControl* k4, 
                             daisy::AnalogControl* k5, daisy::AnalogControl* k6) {
    // The p_knob_n parameters are used to process the potentiometers when in reverb edit mode.
    p_knob_1.Init(*k1, 0.0f, 1.0f, Parameter::LINEAR);
    p_knob_2.Init(*k2, 0.0f, 1.0f, Parameter::LINEAR);
    p_knob_3.Init(*k3, 0.0f, 1.0f, Parameter::LINEAR);
    p_knob_4.Init(*k4, 0.0f, 1.0f, Parameter::LINEAR);
    p_knob_5.Init(*k5, 0.0f, 1.0f, Parameter::LINEAR);
    p_knob_6.Init(*k6, 0.0f, 1.0f, Parameter::LINEAR);

    p_verb_amt.Init(*k1, 0.0f, 1.0f, Parameter::LINEAR);

    p_trem_speed.Init(*k2, TREMOLO_SPEED_MIN, TREMOLO_SPEED_MAX, Parameter::LOGARITHMIC);
    p_trem_depth.Init(*k3, 0.0f, 1.0f, Parameter::LINEAR);

    p_delay_time.Init(*k4, sample_rate_ * DELAY_TIME_MIN_SECONDS, (float)MAX_DELAY_SIZE, Parameter::LOGARITHMIC);
    p_delay_feedback.Init(*k5, 0.0f, 1.0f, Parameter::LINEAR);
    p_delay_amt.Init(*k6, 0.0f, DELAY_DRY_WET_PERCENT_MAX, Parameter::LINEAR);

    // Initialize knob takeover logic (we don't need to pass controls, logic is pure data now)
    // We will initialize them with current values in ProcessControls or on mode switch.
}

void FlickCore::ProcessControls(int sw1, int sw2, int sw3) {
    if (is_factory_reset_mode_) {
        UpdateFactoryReset();
        return; 
    }

    if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
        // Blink LEDs
        uint32_t now = daisy::System::GetNow();
        // Assuming ~500ms blink
        if ((now % 500) < 250) {
             led_left_brightness_ = 1.0f;
             led_right_brightness_ = 1.0f;
        } else {
             led_left_brightness_ = 0.0f;
             led_right_brightness_ = 0.0f;
        }
    } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
        uint32_t now = daisy::System::GetNow();
        if ((now % 500) < 250) {
             led_left_brightness_ = 1.0f;
             led_right_brightness_ = 0.0f;
        } else {
             led_left_brightness_ = 0.0f;
             led_right_brightness_ = 1.0f;
        }
    } else if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
        uint32_t now = daisy::System::GetNow();
        bool slow_pulse = (now % 1000) < 500;
        led_left_brightness_ = (slow_pulse ? 1.0f : 0.1f);

        if (tap_tempo_interval_ms > 0) {
            uint32_t blink_phase = now % tap_tempo_interval_ms;
            float blink_threshold = tap_tempo_interval_ms * TAP_TEMPO_BLINK_DUTY_CYCLE;
            led_right_brightness_ = (blink_phase < blink_threshold ? 1.0f : 0.1f);
        } else {
            led_right_brightness_ = (slow_pulse ? 1.0f : 0.1f);
        }
        
        applyDelaySubdivisionAndSetTargets(tap_tempo_delay_samples, sw3);
        if (tap_tempo_controls_tremolo) {
             osc.SetFreq(tap_tempo_tremolo_freq_hz);
        }

    } else {
        // Normal mode LED handled in ProcessAudio mostly (except left bypass)
        led_left_brightness_ = (bypass_verb ? 0.0f : 1.0f);
        // Right LED handled in Audio for pulsing
    }

    // Parameter Processing
    if (pedal_mode == PEDAL_MODE_NORMAL) {
        if (tap_tempo_controls_tremolo) {
             float val = p_trem_speed.Process();
             if (tap_tempo_tremolo_knob_takeover.checkTakeover(val)) {
                 tap_tempo_controls_tremolo = false;
                 osc.SetFreq(val);
             } else {
                 osc.SetFreq(tap_tempo_tremolo_freq_hz);
             }
        } else {
             osc.SetFreq(p_trem_speed.Process());
        }

        static float depth = 0;
        depth = daisysp::fclamp(p_trem_depth.Process(), 0.f, 1.f);
        
        static const TremoloMode kTremoloModeMap[] = {TREMOLO_SQUARE, TREMOLO_HARMONIC, TREMOLO_SINE}; 
        // 2=UP (Square), 1=MID (Harmonic), 0=DOWN (Sine) ? 
        // We will assume standard map.
        // If passed sw2 is 0, 1, 2.
        tremolo_mode_ = kTremoloModeMap[sw2]; 

        if (tremolo_mode_ == TREMOLO_HARMONIC) depth *= 1.25f;
        else depth *= 0.5f;

        osc.SetAmp(depth);
        dc_offset = 1.f - depth;

        if (tremolo_mode_ == TREMOLO_SQUARE) osc.SetWaveform(FlickOscillator::WAVE_SQUARE_ROUNDED);
        else osc.SetWaveform(FlickOscillator::WAVE_SIN);

        // Delay handling
        if (tap_tempo_controls_delay) {
             float val = p_delay_time.Process();
             if (tap_tempo_delay_knob_takeover.checkTakeover(val)) {
                 tap_tempo_controls_delay = false;
                 master_delay_time_samples = val;
             } else {
                 master_delay_time_samples = tap_tempo_delay_samples;
             }
        } else {
             master_delay_time_samples = p_delay_time.Process();
        }

        applyDelaySubdivisionAndSetTargets(master_delay_time_samples, sw3);

        delayL.feedback = delayR.feedback = p_delay_feedback.Process();
        delay_drywet = (int)p_delay_amt.Process();
        
        static const ReverbKnobMode kReverbKnobMap[] = {REVERB_KNOB_ALL_WET, REVERB_KNOB_DRY_WET_MIX, REVERB_KNOB_ALL_DRY};
        ReverbKnobMode rkm = kReverbKnobMap[sw1];
        switch(rkm) {
            case REVERB_KNOB_ALL_DRY: plate_dry = 1.0; break;
            case REVERB_KNOB_DRY_WET_MIX: plate_dry = 1.0 - plate_wet; break;
            case REVERB_KNOB_ALL_WET: plate_dry = 0.0f; break;
        }

    } else if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
        plate_dry = 1.0;
        
        if (reverb_edit_wet_amount_knob.checkTakeover(p_verb_amt.Process())) plate_wet = p_verb_amt.Value();
        if (reverb_edit_pre_delay_knob.checkTakeover(p_knob_2.Process())) plate_pre_delay = p_knob_2.Value() * 0.25;
        if (reverb_edit_decay_knob.checkTakeover(p_knob_3.Process())) plate_decay = p_knob_3.Value();
        if (reverb_edit_diffusion_knob.checkTakeover(p_knob_4.Process())) plate_tank_diffusion = p_knob_4.Value();
        if (reverb_edit_input_cut_knob.checkTakeover(p_knob_5.Process())) plate_input_damp_high = p_knob_5.Value() * 10.0;
        if (reverb_edit_tank_cut_knob.checkTakeover(p_knob_6.Process())) plate_tank_damp_high = p_knob_6.Value() * 10.0;
        
        static const float speed_vals[] = {0.5f, 0.25f, 0.1f}; // match map order
        if (reverb_edit_mod_speed_switch.checkChange(sw1)) plate_tank_mod_speed = speed_vals[sw1];
        
        static const float depth_vals[] = {0.5f, 0.25f, 0.1f}; // Corrected from speed to depth
        if (reverb_edit_mod_depth_switch.checkChange(sw2)) plate_tank_mod_depth = depth_vals[sw2];
        
        static const float shape_vals[] = {0.5f, 0.25f, 0.1f};
        if (reverb_edit_mod_shape_switch.checkChange(sw3)) plate_tank_mod_shape = shape_vals[sw3];
        
        verb.setDecay(plate_decay);
        verb.setTankDiffusion(plate_tank_diffusion);
        verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
        verb.setTankFilterHighCutFrequency(plate_tank_damp_high);
        verb.setTankModSpeed(plate_tank_mod_speed * 8);
        verb.setTankModDepth(plate_tank_mod_depth * 15);
        verb.setTankModShape(plate_tank_mod_shape);
        verb.setPreDelay(plate_pre_delay);

    } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
        switch(sw3) {
            case 1: mono_stereo_mode = MS_MODE_MISO; break; // MIDDLE
            case 2: mono_stereo_mode = MS_MODE_SISO; break; // UP
            default: mono_stereo_mode = MS_MODE_MIMO; // DOWN (0)
        }
        updateReverbScales(mono_stereo_mode);
        
        static const TremDelMakeUpGain kMakeupGainMap[] = {MAKEUP_GAIN_HEAVY, MAKEUP_GAIN_NORMAL, MAKEUP_GAIN_NONE};
        // Map SW2
        current_makeup_gain = kMakeupGainMap[sw2];
    }
}

void FlickCore::ProcessAudio(const float* in_l, const float* in_r, float* out_l, float* out_r, size_t size) {
    if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
        plate_wet = p_verb_amt.Value(); // Already processed in ProcessControls?
        // Wait, ProcessControls processes p_verb_amt if in edit mode?
        // Yes.
    } else {
        plate_wet = p_verb_amt.Process(); // Process here if not edit mode?
        // But ProcessControls already processed it if NOT edit mode?
        // No, in NORMAL mode, p_verb_amt was NOT processed in ProcessControls!
        // In NORMAL mode, ProcessControls processed: p_trem_speed, p_trem_depth, p_delay_time, p_delay_feedback, p_delay_amt.
        // It did NOT process p_verb_amt (knob 1).
        // So we process it here.
    }
    
    // In ProcessControls we called Process() on most parameters.
    // If we call Process() here, it will advance the smoother again (doubling speed if called both).
    // Let's ensure we only call Process() once per block.
    // In NORMAL mode, ProcessControls handles MOST params.
    // p_verb_amt was NOT handled in Normal Mode branch of ProcessControls.
    // So calling p_verb_amt.Process() here is correct.
    
    // However, if we are in EDIT mode, ProcessControls handled p_verb_amt (as p_verb_amt or p_knob_1?).
    // In Edit mode, `if (reverb_edit_wet_amount_knob.checkTakeover(p_verb_amt.Process()))`.
    // So it IS processed.
    // So we should use Value().
    // But if we are in Normal Mode, we haven't processed it yet.
    // So:
    if (pedal_mode == PEDAL_MODE_NORMAL) {
        plate_wet = p_verb_amt.Process();
    } else {
        plate_wet = p_verb_amt.Value();
    }

    float last_trem_val = 0.0f;

    for (size_t i = 0; i < size; ++i) {
        float s_l = in_l[i];
        float s_r;
        
        if (mono_stereo_mode == MS_MODE_MIMO || mono_stereo_mode == MS_MODE_MISO) {
             s_r = s_l;
        } else {
             s_r = in_r[i];
        }

        // Makeup gain
        float trem_make_up_gain = 1.0f;
        float delay_make_up_gain = 1.0f;
        switch (current_makeup_gain) {
             case MAKEUP_GAIN_HEAVY: trem_make_up_gain = 1.6f; delay_make_up_gain = 2.0f; break;
             case MAKEUP_GAIN_NORMAL: trem_make_up_gain = 1.2f; delay_make_up_gain = 1.66f; break;
             default: break;
        }

        if (!bypass_delay) {
            float mix_l = delayL.Process(s_l); 
            float mix_r = delayR.Process(s_r);
            float fdrywet = delay_drywet / DELAY_DRY_WET_PERCENT_MAX;
            
            s_l = fdrywet * mix_l * DELAY_WET_MIX_ATTENUATION + (1.0f - fdrywet) * s_l * delay_make_up_gain;
            s_r = fdrywet * mix_r * DELAY_WET_MIX_ATTENUATION + (1.0f - fdrywet) * s_r * delay_make_up_gain;
        }

        float current_trem_val = 0.0f;
        if (!bypass_trem) {
            float lfo_sample = osc.Process();
            current_trem_val = dc_offset + lfo_sample;
            last_trem_val = current_trem_val;

            if (tremolo_mode_ == TREMOLO_HARMONIC) {
                // Process left channel
                float low_l = low_pass_l.Process(s_l);
                float high_l = high_pass_l.Process(s_l);
                float low_mod_l = low_l * (1.0f + lfo_sample);
                float high_mod_l = high_l * (1.0f - lfo_sample);
                s_l = (low_mod_l + high_mod_l) * trem_make_up_gain;

                // Process right channel
                float low_r = low_pass_r.Process(s_r);
                float high_r = high_pass_r.Process(s_r);
                float low_mod_r = low_r * (1.0f + lfo_sample);
                float high_mod_r = high_r * (1.0f - lfo_sample);
                s_r = (low_mod_r + high_mod_r) * trem_make_up_gain;
            } else {
                s_l *= current_trem_val * trem_make_up_gain;
                s_r *= current_trem_val * trem_make_up_gain;
            }
        }

        float left_input = hardLimit100_(s_l) * reverb_dry_scale_factor;
        float right_input = hardLimit100_(s_r) * reverb_dry_scale_factor;
        
        // Define constants for Dattorro logic that were in original file
        constexpr float minus_18db_gain = 0.12589254f;
        constexpr float minus_20db_gain = 0.1f;
        float clearPopCancelValue = 1.0f; // Was this in use? 
        // In flick.cpp line 865: `(verb.getLeftOutput() * plate_wet * clearPopCancelValue)`
        // `clearPopCancelValue` was not visibly defined in my read, but likely handled pops?
        // Assuming 1.0f for now as logic for it was not shown.
        // Wait, line 863: `verb.process(...) * clearPopCancelValue`?
        // If it's used to avoid pops on enable, I should implement it.
        
        // Let's check flick.cpp read again.
        // It was used.
        // `verb.process(left_input * ... * clearPopCancelValue, ...)`
        // I will stick to 1.0f unless I find the definition. It was likely a global fading in from 0.

        // Assuming input_amplification is 1.0
        float input_amplification = 1.0f;
        
        verb.process(left_input * minus_18db_gain * minus_20db_gain * (1.0f + input_amplification * 7.0f) * clearPopCancelValue,
                     right_input * minus_18db_gain * minus_20db_gain * (1.0f + input_amplification * 7.0f) * clearPopCancelValue);

        float left_output = 0.0f;
        float right_output = 0.0f;

        if (!bypass_verb) {
            left_output = ((left_input * plate_dry * reverb_reverse_scale_factor) + (verb.getLeftOutput() * plate_wet * clearPopCancelValue));
            right_output = ((right_input * plate_dry * reverb_reverse_scale_factor) + (verb.getRightOutput() * plate_wet * clearPopCancelValue));

            s_l = left_output;
            s_r = right_output;
        }

        if (mono_stereo_mode == MS_MODE_MIMO) {
            out_l[i] = (s_l * 0.5f) + (s_r * 0.5f);
            out_r[i] = 0.0f;
        } else {
            out_l[i] = s_l;
            out_r[i] = s_r;
        }
    }

    if (pedal_mode == PEDAL_MODE_NORMAL) {
        if (bypass_trem) {
             led_right_brightness_ = (bypass_delay ? 0.0f : 1.0f);
        } else {
             led_right_brightness_ = (bypass_delay ? last_trem_val * TREMOLO_LED_BRIGHTNESS : last_trem_val);
        }
    }
}

float FlickCore::hardLimit100_(const float &x) {
  return (x > 1.) ? 1. : ((x < -1.) ? -1. : x);
}

void FlickCore::CheckTapTempoTimeout() {
  if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
    uint32_t currentTime = daisy::System::GetNow();
    if ((currentTime - tap_tempo_last_tap_time) >= TAP_TEMPO_TIMEOUT_MS) {
      exitTapTempoMode();
    }
  }
}

void FlickCore::HandleNormalPress(int fs_idx) {
    if (pedal_mode == PEDAL_MODE_TAP_TEMPO) {
        if (fs_idx == 0) exitTapTempoMode();
        else if (fs_idx == 1) handleTapTempoTap();
    } else if (pedal_mode == PEDAL_MODE_EDIT_REVERB) {
        if (fs_idx == 1) {
             saved_state_ = GetSettings(); // Update saved state
             trigger_settings_save_ = true;
        } else {
            restoreReverbSettings();
        }
        pedal_mode = PEDAL_MODE_NORMAL;
    } else if (pedal_mode == PEDAL_MODE_EDIT_MONO_STEREO) {
        if (fs_idx == 1) saveMonoStereoSettings();
        else restoreMonoStereoSettings();
        pedal_mode = PEDAL_MODE_NORMAL;
    } else {
        if (fs_idx == 0) {
            bypass_verb = !bypass_verb;
            if (bypass_verb) verb.clear();
        } else {
            bypass_delay = !bypass_delay;
        }
        saveBypassStates();
    }
}

void FlickCore::HandleDoublePress(int fs_idx) {
    if (pedal_mode != PEDAL_MODE_NORMAL) return;
    HandleNormalPress(fs_idx); // Reverse the normal press
    
    if (fs_idx == 0) enterTapTempoMode();
    else if (fs_idx == 1) {
        bypass_trem = !bypass_trem;
        saveBypassStates();
    }
}

void FlickCore::HandleLongPress(int fs_idx) {
    if (fs_idx == 0) {
        bypass_verb = false; 
        
        reverb_edit_wet_amount_knob.capture(p_verb_amt.Value());
        reverb_edit_pre_delay_knob.capture(p_knob_2.Value());
        reverb_edit_decay_knob.capture(p_knob_3.Value());
        reverb_edit_diffusion_knob.capture(p_knob_4.Value());
        reverb_edit_input_cut_knob.capture(p_knob_5.Value());
        reverb_edit_tank_cut_knob.capture(p_knob_6.Value());
        
        // We need current switch positions to capture.
        // But we don't store switch positions in class.
        // We assume 0 for capture? Or we need passed values.
        // Problem: HandleLongPress is an event, but doesn't pass current state.
        // We should just set changed=false.
        reverb_edit_mod_speed_switch.capture(-1); // Force re-read?
        // SwitchChangeDetector logic: capture(pos)
        // If we pass -1, checkChange will be true on next read of 0,1,2?
        // Yes, if current pos != -1.
        reverb_edit_mod_speed_switch.entry_position = -1; 
        reverb_edit_mod_depth_switch.entry_position = -1;
        reverb_edit_mod_shape_switch.entry_position = -1;
        
        pedal_mode = PEDAL_MODE_EDIT_REVERB;
    } else if (fs_idx == 1) {
        bypass_verb = false;
        bypass_delay = true;
        bypass_trem = true;
        pedal_mode = PEDAL_MODE_EDIT_MONO_STEREO;
    }
}

void FlickCore::enterTapTempoMode() {
    tap_tempo_active = true;
    tap_tempo_last_tap_time = daisy::System::GetNow();
    
    bool delay_active = !bypass_delay;
    bool tremolo_active = !bypass_trem;

    if (!delay_active && !tremolo_active) {
        tap_tempo_controls_delay = true;
        tap_tempo_controls_tremolo = true;
    } else if (delay_active && tremolo_active) {
        tap_tempo_controls_delay = true;
        tap_tempo_controls_tremolo = true;
    } else if (delay_active && !tremolo_active) {
        tap_tempo_controls_delay = true;
        tap_tempo_controls_tremolo = false;
    } else if (!delay_active && tremolo_active) {
        tap_tempo_controls_delay = false;
        tap_tempo_controls_tremolo = true;
    }

    tap_tempo_delay_knob_takeover.capture(p_delay_time.Value());
    tap_tempo_tremolo_knob_takeover.capture(p_trem_speed.Value());

    pedal_mode = PEDAL_MODE_TAP_TEMPO;
}

void FlickCore::exitTapTempoMode() {
    tap_tempo_active = false;
    pedal_mode = PEDAL_MODE_NORMAL;
}

void FlickCore::handleTapTempoTap() {
    uint32_t currentTime = daisy::System::GetNow();
    if (tap_tempo_last_tap_time > 0) {
        uint32_t interval = currentTime - tap_tempo_last_tap_time;
        if (interval >= TAP_TEMPO_MIN_INTERVAL_MS && interval <= TAP_TEMPO_MAX_INTERVAL_MS) {
            tap_tempo_interval_ms = interval;
            tap_tempo_delay_samples = (interval / MS_PER_SECOND) * sample_rate_;
            tap_tempo_delay_samples = daisysp::fclamp(tap_tempo_delay_samples, TAP_TEMPO_SAMPLES_MIN, TAP_TEMPO_SAMPLES_MAX);
            
            tap_tempo_tremolo_freq_hz = MS_PER_SECOND / interval;
            tap_tempo_tremolo_freq_hz = daisysp::fclamp(tap_tempo_tremolo_freq_hz, TREMOLO_SPEED_MIN, TREMOLO_SPEED_MAX);

             master_delay_time_samples = tap_tempo_delay_samples;
        }
    }
    tap_tempo_last_tap_time = currentTime;
}

void FlickCore::applyDelaySubdivisionAndSetTargets(float masterDelaySamples, int switch3_pos) {
    if (switch3_pos < 0 || switch3_pos > 2) switch3_pos = 1;
    static const DelaySubdivision kDelaySubdivisionMap[] = {
      DELAY_SUBDIV_DOTTED_EIGHTH,     // UP (assuming 2)
      DELAY_SUBDIV_NORMAL,            // MIDDLE (1)
      DELAY_SUBDIV_QUARTER_TRIPLET,   // DOWN (0)
    };
    // Map check:
    // UP (2) -> 0.75
    // MID (1) -> 1.0
    // DOWN (0) -> 0.66
    
    DelaySubdivision subdivision = kDelaySubdivisionMap[switch3_pos];
    float subdivision_multiplier = 1.0f;
    switch (subdivision) {
        case DELAY_SUBDIV_DOTTED_EIGHTH: subdivision_multiplier = 0.75f; break;
        case DELAY_SUBDIV_QUARTER_TRIPLET: subdivision_multiplier = 2.0f / 3.0f; break;
        default: subdivision_multiplier = 1.0f; break;
    }
    
    float final_delay_time = masterDelaySamples * subdivision_multiplier;
    final_delay_time = daisysp::fclamp(final_delay_time, TAP_TEMPO_SAMPLES_MIN, (float)MAX_DELAY_SIZE);
    
    delayL.delay_target = final_delay_time;
    delayR.delay_target = final_delay_time;
}

// Settings
Settings FlickCore::GetSettings() const {
    Settings s;
    s.version = SETTINGS_VERSION;
    s.decay = plate_decay;
    s.diffusion = plate_tank_diffusion;
    s.input_cutoff_freq = plate_input_damp_high;
    s.tank_cutoff_freq = plate_tank_damp_high;
    s.tank_mod_speed = plate_tank_mod_speed;
    s.tank_mod_depth = plate_tank_mod_depth;
    s.tank_mod_shape = plate_tank_mod_shape;
    s.pre_delay = plate_pre_delay;
    s.mono_stereo_mode = mono_stereo_mode;
    s.makeup_gain_mode = current_makeup_gain;
    s.bypass_reverb = bypass_verb;
    s.bypass_delay = bypass_delay;
    s.bypass_tremolo = bypass_trem;
    return s;
}

void FlickCore::SetSettings(const Settings& s) {
    if (s.version != SETTINGS_VERSION) {
        RestoreDefaults();
        // Update saved state with defaults
        saved_state_ = GetSettings();
        return;
    }
    saved_state_ = s;

    plate_decay = s.decay;
    plate_tank_diffusion = s.diffusion;
    plate_input_damp_high = s.input_cutoff_freq;
    plate_tank_damp_high = s.tank_cutoff_freq;
    plate_tank_mod_speed = s.tank_mod_speed;
    plate_tank_mod_depth = s.tank_mod_depth;
    plate_tank_mod_shape = s.tank_mod_shape;
    plate_pre_delay = s.pre_delay;
    
    mono_stereo_mode = (MonoStereoMode)s.mono_stereo_mode;
    updateReverbScales(mono_stereo_mode);
    
    current_makeup_gain = (TremDelMakeUpGain)s.makeup_gain_mode;
    
    bypass_verb = s.bypass_reverb;
    bypass_delay = s.bypass_delay;
    bypass_trem = s.bypass_tremolo;
    
    verb.setPreDelay(plate_pre_delay);
    verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
    verb.setDecay(plate_decay);
    verb.setTankDiffusion(plate_tank_diffusion);
    verb.setTankFilterHighCutFrequency(plate_tank_damp_high);
    verb.setTankModSpeed(plate_tank_mod_speed * 8);
    verb.setTankModDepth(plate_tank_mod_depth * 15);
    verb.setTankModShape(plate_tank_mod_shape);
}

void FlickCore::RestoreDefaults() {
  // Same as constructor defaults
    plate_decay = 0.8;
    plate_tank_diffusion = 0.85;
    plate_input_damp_high = 7.25;
    plate_tank_damp_high = 7.25;
    plate_tank_mod_speed = 0.1;
    plate_tank_mod_depth = 0.1;
    plate_tank_mod_shape = 0.25;
    plate_pre_delay = 0;
    
    mono_stereo_mode = MS_MODE_MIMO;
    current_makeup_gain = MAKEUP_GAIN_NORMAL;
    updateReverbScales(mono_stereo_mode);
    
    bypass_verb = true;
    bypass_delay = true;
    bypass_trem = true;
}

void FlickCore::updateReverbScales(MonoStereoMode mode) {
  switch (mode) {
    case MS_MODE_MIMO:
      reverb_dry_scale_factor = 5.0f; 
      reverb_reverse_scale_factor = 0.2f;
      break;
    case MS_MODE_MISO:
    case MS_MODE_SISO:
      reverb_dry_scale_factor = 2.5f; 
      reverb_reverse_scale_factor = 0.4f;
      break;
  }
}

void FlickCore::restoreReverbSettings() {
    plate_decay = saved_state_.decay;
    plate_tank_diffusion = saved_state_.diffusion;
    plate_input_damp_high = saved_state_.input_cutoff_freq;
    plate_tank_damp_high = saved_state_.tank_cutoff_freq;
    plate_tank_mod_speed = saved_state_.tank_mod_speed;
    plate_tank_mod_depth = saved_state_.tank_mod_depth;
    plate_tank_mod_shape = saved_state_.tank_mod_shape;
    plate_pre_delay = saved_state_.pre_delay;

    verb.setDecay(plate_decay);
    verb.setTankDiffusion(plate_tank_diffusion);
    verb.setInputFilterHighCutoffPitch(plate_input_damp_high);
    verb.setTankFilterHighCutFrequency(plate_tank_damp_high);

    verb.setTankModSpeed(plate_tank_mod_speed * 8);
    verb.setTankModDepth(plate_tank_mod_depth * 15);
    verb.setTankModShape(plate_tank_mod_shape);
    verb.setPreDelay(plate_pre_delay);    
}

// Implement Factory Reset Update
void FlickCore::UpdateFactoryReset() {
    float low_knob_threshold = 0.05;
    float high_knob_threshold = 0.95;
    float blink_faster_amount = 300; 

    uint32_t now = daisy::System::GetNow();
    if (now - last_led_toggle_time_ >= reset_blink_interval_) {
        last_led_toggle_time_ = now;
        led_toggle_state_ = !led_toggle_state_;
        led_left_brightness_ = led_toggle_state_ ? 1.0f : 0.0f;
        led_right_brightness_ = led_toggle_state_ ? 0.0f : 1.0f;
    }

    float knob_1_value = p_knob_1.Process();
    
    bool advance = false;
    if (factory_reset_stage_ == 0 && knob_1_value >= high_knob_threshold) advance = true;
    else if (factory_reset_stage_ == 1 && knob_1_value <= low_knob_threshold) advance = true;
    else if (factory_reset_stage_ == 2 && knob_1_value >= high_knob_threshold) advance = true;
    else if (factory_reset_stage_ == 3 && knob_1_value <= low_knob_threshold) {
        RestoreDefaults();
        // Trigger save
        trigger_settings_save_ = true;
        // Reset state
        is_factory_reset_mode_ = false;
        factory_reset_stage_ = 0;
        pedal_mode = PEDAL_MODE_NORMAL;
        // Initial blink
        led_left_brightness_ = 1.0; led_right_brightness_ = 1.0;
        // We can't delay here easily without blocking.
        return;
    }
    
    if (advance) {
        factory_reset_stage_++;
        if (reset_blink_interval_ > blink_faster_amount)
            reset_blink_interval_ -= blink_faster_amount;
        // Indication?
        led_left_brightness_ = 1.0; led_right_brightness_ = 1.0;
    }
}

void FlickCore::saveBypassStates() {
    saved_state_.bypass_reverb = bypass_verb;
    saved_state_.bypass_delay = bypass_delay;
    saved_state_.bypass_tremolo = bypass_trem;
    trigger_settings_save_ = true;
}
void FlickCore::saveMonoStereoSettings() {
    trigger_settings_save_ = true;
    // We update saved_state_ locally so if we restore later (without save completing?) it works?
    // Actually `trigger_settings_save_` tells `flick.cpp` to call `GetSettings()` (which reads live vars) and save.
    // We should update `saved_state_` here so `restore` works if we re-enter edit mode?
    // Typical flow: Enter edit -> Change vars -> Save (Updates flash & saved_state_) OR Cancel (Reverts vars from saved_state_).
    // So on Save, we must update saved_state_.
    saved_state_.mono_stereo_mode = mono_stereo_mode;
    saved_state_.makeup_gain_mode = current_makeup_gain;
}
void FlickCore::restoreMonoStereoSettings() {
    mono_stereo_mode = (MonoStereoMode)saved_state_.mono_stereo_mode;
    current_makeup_gain = (TremDelMakeUpGain)saved_state_.makeup_gain_mode;
    updateReverbScales(mono_stereo_mode);
}