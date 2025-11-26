#pragma once

/**
 * @file state_machine.hpp
 * @brief State machine for detection and trigger management
 */

#include <chrono>
#include <functional>
#include <memory>

namespace micmap::core {

/**
 * @brief State machine configuration
 */
struct StateMachineConfig {
    std::chrono::milliseconds minDetectionDuration{500};  ///< Minimum detection time before trigger
    std::chrono::milliseconds cooldownDuration{300};      ///< Cooldown after trigger
    float detectionThreshold{0.7f};                       ///< Confidence threshold for detection
};

/**
 * @brief State machine states
 */
enum class State {
    Idle,       ///< Waiting for detection
    Training,   ///< Training mode active
    Detecting,  ///< Pattern detected, waiting for duration
    Triggered,  ///< Trigger fired
    Cooldown    ///< Cooldown period after trigger
};

/**
 * @brief Convert state to string
 */
inline const char* stateToString(State state) {
    switch (state) {
        case State::Idle: return "Idle";
        case State::Training: return "Training";
        case State::Detecting: return "Detecting";
        case State::Triggered: return "Triggered";
        case State::Cooldown: return "Cooldown";
        default: return "Unknown";
    }
}

/**
 * @brief Callback for trigger events
 */
using TriggerCallback = std::function<void()>;

/**
 * @brief Callback for state changes
 */
using StateChangeCallback = std::function<void(State oldState, State newState)>;

/**
 * @brief Interface for the state machine
 */
class IStateMachine {
public:
    virtual ~IStateMachine() = default;
    
    /**
     * @brief Configure the state machine
     * @param config Configuration parameters
     */
    virtual void configure(const StateMachineConfig& config) = 0;
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    virtual const StateMachineConfig& getConfig() const = 0;
    
    /**
     * @brief Update the state machine
     * @param detectionConfidence Current detection confidence (0.0 to 1.0)
     * @param deltaTime Time since last update
     */
    virtual void update(float detectionConfidence, std::chrono::milliseconds deltaTime) = 0;
    
    /**
     * @brief Get the current state
     * @return Current state
     */
    virtual State getCurrentState() const = 0;
    
    /**
     * @brief Get time in current state
     * @return Duration in current state
     */
    virtual std::chrono::milliseconds getTimeInState() const = 0;
    
    /**
     * @brief Set trigger callback
     * @param callback Function to call when trigger fires
     */
    virtual void setTriggerCallback(TriggerCallback callback) = 0;
    
    /**
     * @brief Set state change callback
     * @param callback Function to call on state changes
     */
    virtual void setStateChangeCallback(StateChangeCallback callback) = 0;
    
    /**
     * @brief Start training mode
     */
    virtual void startTraining() = 0;
    
    /**
     * @brief Stop training mode
     */
    virtual void stopTraining() = 0;
    
    /**
     * @brief Reset to idle state
     */
    virtual void reset() = 0;
    
    /**
     * @brief Check if in training mode
     * @return True if training
     */
    virtual bool isTraining() const = 0;
};

/**
 * @brief Create a state machine instance
 * @param config Initial configuration
 * @return Unique pointer to state machine
 */
std::unique_ptr<IStateMachine> createStateMachine(const StateMachineConfig& config = StateMachineConfig{});

} // namespace micmap::core