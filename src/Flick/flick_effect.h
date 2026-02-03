#pragma once
#ifndef FLICK_EFFECT_H
#define FLICK_EFFECT_H

#include "daisy.h"
#include "daisysp.h"

// Forward declarations
class FlickCore;

namespace flick {

/**
 * @brief Base class for all Flick effects (Reverb, Tremolo, Delay)
 * 
 * Each effect encapsulates its own:
 * - Audio processing logic
 * - Parameter handling
 * - Edit mode behavior
 * - Bypass state
 * 
 * The FlickCore orchestrates these effects but delegates the actual
 * processing and mode-specific behavior to each effect class.
 */
class Effect {
public:
    Effect() : bypassed_(true), sample_rate_(48000.0f) {}
    virtual ~Effect() {}

    /**
     * @brief Initialize the effect
     * @param sample_rate Audio sample rate
     */
    virtual void Init(float sample_rate) {
        sample_rate_ = sample_rate;
    }

    /**
     * @brief Process a single sample (stereo)
     * @param in_l Left input sample
     * @param in_r Right input sample  
     * @param out_l Left output sample (modified)
     * @param out_r Right output sample (modified)
     */
    virtual void ProcessSample(float in_l, float in_r, float& out_l, float& out_r) = 0;

    /**
     * @brief Called once per audio block to update parameters from controls
     * @param core Pointer to FlickCore for accessing shared state
     * @param sw1 Toggle switch 1 position (0-2)
     * @param sw2 Toggle switch 2 position (0-2)
     * @param sw3 Toggle switch 3 position (0-2)
     */
    virtual void ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) = 0;

    /**
     * @brief Called when entering edit mode for this effect
     */
    virtual void EnterEditMode() {}

    /**
     * @brief Called when exiting edit mode for this effect
     * @param save Whether to save changes (true) or restore previous values (false)
     */
    virtual void ExitEditMode(bool save) {}

    /**
     * @brief Check if effect is in edit mode
     */
    virtual bool IsInEditMode() const { return false; }

    // Bypass state
    bool IsBypassed() const { return bypassed_; }
    void SetBypassed(bool bypassed) { bypassed_ = bypassed; }
    void ToggleBypass() { 
        bypassed_ = !bypassed_;
        if (bypassed_) {
            OnBypassEnabled();
        }
    }

    /**
     * @brief Called when bypass is enabled (for cleanup like clearing tails)
     */
    virtual void OnBypassEnabled() {}

    /**
     * @brief Get the current LFO/modulation value for LED display
     * @return Value between 0.0 and 1.0 for LED brightness, or -1.0 if not applicable
     */
    virtual float GetLedModulationValue() const { return -1.0f; }

protected:
    bool bypassed_;
    float sample_rate_;
};

} // namespace flick

#endif // FLICK_EFFECT_H
