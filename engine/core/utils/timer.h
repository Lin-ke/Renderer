#ifndef TIMER_H
#define TIMER_H

#include <chrono>

/**
 * @brief Simple high-precision timer for measuring frame delta time
 * 
 * Uses std::chrono::steady_clock for monotonic time measurements.
 */
class Timer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::duration<float, std::milli>;

    Timer() {
        reset();
    }

    /**
     * @brief Reset the timer to current time
     */
    void reset() {
        start_time_ = Clock::now();
        last_time_ = start_time_;
    }

    /**
     * @brief Get elapsed time in milliseconds since last call to this function or reset
     * @return Elapsed time in milliseconds
     */
    float get_elapsed_ms() {
        TimePoint now = Clock::now();
        Duration elapsed = now - last_time_;
        last_time_ = now;
        return elapsed.count();
    }

    /**
     * @brief Get elapsed time in seconds since last call to this function or reset
     * @return Elapsed time in seconds
     */
    float get_elapsed_sec() {
        return get_elapsed_ms() / 1000.0f;
    }

    /**
     * @brief Get total elapsed time in milliseconds since reset
     * @return Total elapsed time in milliseconds
     */
    float get_total_ms() const {
        Duration elapsed = Clock::now() - start_time_;
        return elapsed.count();
    }

    /**
     * @brief Get total elapsed time in seconds since reset
     * @return Total elapsed time in seconds
     */
    float get_total_sec() const {
        return get_total_ms() / 1000.0f;
    }

private:
    TimePoint start_time_;
    TimePoint last_time_;
};

#endif // TIMER_H
