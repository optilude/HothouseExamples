#pragma once
#ifndef FLICK_KNOB_TAKEOVER_H
#define FLICK_KNOB_TAKEOVER_H

#include <cmath>

namespace flick {

/**
 * @brief Implements soft takeover for knob controls
 * 
 * Soft takeover prevents parameter jumps when a knob's physical position
 * doesn't match the current parameter value (e.g., after loading settings,
 * switching modes, or using tap tempo to set a value).
 * 
 * Usage:
 * 1. Call capture() when entering a mode where soft takeover is needed
 * 2. Call checkTakeover() each time you read the knob
 * 3. Only apply the knob value when checkTakeover() returns true
 */
struct KnobTakeover {
    float entry_value;    // Knob position when control was suspended
    bool taken_over;      // Whether knob has moved enough to take control

    KnobTakeover() : entry_value(0.0f), taken_over(false) {}

    /**
     * @brief Reset takeover state and capture current knob position
     * @param current_value Current knob reading to capture
     */
    void capture(float current_value) {
        entry_value = current_value;
        taken_over = false;
    }

    /**
     * @brief Check if knob has moved enough to take over control
     * @param current_value Current knob reading
     * @param threshold Movement threshold (default 5%)
     * @return true if knob is actively controlling (taken over or just took over)
     */
    bool checkTakeover(float current_value, float threshold = 0.05f) {
        if (!taken_over) {
            if (fabs(current_value - entry_value) > threshold) {
                taken_over = true;
                return true;  // Just taken over
            }
            return false;     // Not yet taken over
        }
        return true;          // Already taken over
    }

    /**
     * @brief Check if the knob has taken over control
     */
    bool hasTakenOver() const { return taken_over; }

    /**
     * @brief Force takeover state (useful when resetting)
     */
    void setTakenOver(bool state) { taken_over = state; }
};

/**
 * @brief Detects when a switch position has changed from a captured state
 * 
 * Used in edit modes where switch changes should only take effect
 * after the user explicitly moves the switch.
 */
struct SwitchChangeDetector {
    int entry_position;   // Switch position when tracking started
    bool changed;         // Whether switch has been moved from entry position

    SwitchChangeDetector() : entry_position(-1), changed(false) {}

    /**
     * @brief Reset change state and capture current switch position
     * @param current_position Current switch position to capture
     */
    void capture(int current_position) {
        entry_position = current_position;
        changed = false;
    }

    /**
     * @brief Check if switch position has changed from entry position
     * @param current_position Current switch position
     * @return true if switch has been moved
     */
    bool checkChange(int current_position) {
        if (!changed && current_position != entry_position) {
            changed = true;
            return true;    // Just changed
        }
        return changed;     // Return current changed state
    }

    /**
     * @brief Check if switch has changed
     */
    bool hasChanged() const { return changed; }
};

} // namespace flick

#endif // FLICK_KNOB_TAKEOVER_H
