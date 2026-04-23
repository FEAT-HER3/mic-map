/**
 * @file state_machine.cpp
 * @brief State machine implementation
 */

#include "micmap/core/state_machine.hpp"
#include "micmap/common/logger.hpp"

namespace micmap::core {

/**
 * @brief State machine implementation
 */
class StateMachineImpl : public IStateMachine {
public:
    explicit StateMachineImpl(const StateMachineConfig& config)
        : config_(config)
        , currentState_(State::Idle)
        , timeInState_(0) {
    }
    
    ~StateMachineImpl() override = default;
    
    void configure(const StateMachineConfig& config) override {
        config_ = config;
        MICMAP_LOG_DEBUG("State machine configured: threshold=", config_.detectionThreshold,
                        ", minDuration=", config_.minDetectionDuration.count(), "ms");
    }
    
    const StateMachineConfig& getConfig() const override {
        return config_;
    }
    
    void update(float detectionConfidence, std::chrono::milliseconds deltaTime) override {
        timeInState_ += deltaTime;

        switch (currentState_) {
            case State::Idle:
                updateIdle(detectionConfidence);
                break;

            case State::Training:
                // Training state is managed externally
                break;

            case State::Detecting:
                updateDetecting(detectionConfidence);
                break;

            case State::Triggered:
                // One-tick latch: Triggered lasts a single update so the
                // callback observer has a chance to see the state. Next
                // tick drops straight into Cooldown.
                transitionTo(State::Cooldown);
                break;

            case State::Cooldown:
                updateCooldown();
                break;
        }
    }
    
    State getCurrentState() const override {
        return currentState_;
    }
    
    std::chrono::milliseconds getTimeInState() const override {
        return timeInState_;
    }
    
    void setTriggerCallback(TriggerCallback callback) override {
        triggerCallback_ = std::move(callback);
    }
    
    void setStateChangeCallback(StateChangeCallback callback) override {
        stateChangeCallback_ = std::move(callback);
    }
    
    void startTraining() override {
        if (currentState_ != State::Training) {
            transitionTo(State::Training);
            MICMAP_LOG_INFO("Training mode started");
        }
    }
    
    void stopTraining() override {
        if (currentState_ == State::Training) {
            transitionTo(State::Idle);
            MICMAP_LOG_INFO("Training mode stopped");
        }
    }
    
    void reset() override {
        transitionTo(State::Idle);
        MICMAP_LOG_DEBUG("State machine reset");
    }
    
    bool isTraining() const override {
        return currentState_ == State::Training;
    }
    
private:
    void transitionTo(State newState) {
        if (newState == currentState_) {
            return;
        }
        
        State oldState = currentState_;
        currentState_ = newState;
        timeInState_ = std::chrono::milliseconds(0);
        
        MICMAP_LOG_DEBUG("State transition: ", stateToString(oldState), 
                        " -> ", stateToString(newState));
        
        if (stateChangeCallback_) {
            stateChangeCallback_(oldState, newState);
        }
    }
    
    void updateIdle(float detectionConfidence) {
        if (detectionConfidence >= config_.detectionThreshold) {
            transitionTo(State::Detecting);
        }
    }
    
    void updateDetecting(float detectionConfidence) {
        if (detectionConfidence < config_.detectionThreshold) {
            // Lost detection before minimum duration
            transitionTo(State::Idle);
            return;
        }

        if (timeInState_ >= config_.minDetectionDuration) {
            // Detection held long enough -- fire a single tap event.
            transitionTo(State::Triggered);

            MICMAP_LOG_INFO("Tap fired after ", timeInState_.count(), "ms");

            if (triggerCallback_) {
                triggerCallback_();
            }
        }
    }

    // Cooldown (D-11): blocks next tap until duration elapses.
    void updateCooldown() {
        if (timeInState_ >= config_.cooldownDuration) {
            transitionTo(State::Idle);
        }
    }
    
    StateMachineConfig config_;
    State currentState_;
    std::chrono::milliseconds timeInState_;
    
    TriggerCallback triggerCallback_;
    StateChangeCallback stateChangeCallback_;
};

std::unique_ptr<IStateMachine> createStateMachine(const StateMachineConfig& config) {
    return std::make_unique<StateMachineImpl>(config);
}

} // namespace micmap::core