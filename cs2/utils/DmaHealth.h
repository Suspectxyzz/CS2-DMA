#pragma once

// =====================================================================
//  DmaHealth — DMA runtime health state machine (thread-safe)
//
//  States:
//    Healthy   — normal operation
//    Degraded  — intermittent failures, still attempting reads
//    Failed    — sustained failure, requires reconnect
//
//  Transitions:
//    Healthy  -> Degraded : 8 consecutive failures
//    Degraded -> Healthy  : 15 consecutive successes
//    Degraded -> Failed   : 40 consecutive failures
//    Failed   -> Healthy  : NotifyReconnectSuccess() after reconnect
// =====================================================================

#include <chrono>
#include <cstdint>
#include <mutex>

namespace DmaHealth
{
    enum class DmaHealthState
    {
        Healthy  = 0,
        Degraded = 1,
        Failed   = 2,
    };

    struct DmaHealthStats
    {
        int      consecutiveFailures = 0;
        int      consecutiveSuccesses = 0;
        int64_t  totalFailures = 0;
        int64_t  totalSuccesses = 0;
        std::chrono::steady_clock::time_point degradedStartTime{};
        std::chrono::steady_clock::time_point failedStartTime{};
        std::chrono::steady_clock::time_point lastRecoveryTime{};
        int64_t  lastDegradedDurationMs = 0;
        int64_t  lastFailureDurationMs = 0;
    };

    class DmaHealthTracker
    {
    public:
        void RecordSuccess()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stats.consecutiveSuccesses++;
            m_Stats.consecutiveFailures = 0;
            m_Stats.totalSuccesses++;

            if (m_State == DmaHealthState::Degraded && m_Stats.consecutiveSuccesses >= 15)
            {
                TransitionToLocked(DmaHealthState::Healthy);
            }
        }

        void RecordFailure()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stats.consecutiveFailures++;
            m_Stats.consecutiveSuccesses = 0;
            m_Stats.totalFailures++;

            if (m_State == DmaHealthState::Healthy && m_Stats.consecutiveFailures >= 8)
            {
                TransitionToLocked(DmaHealthState::Degraded);
            }
            else if (m_State == DmaHealthState::Degraded && m_Stats.consecutiveFailures >= 40)
            {
                TransitionToLocked(DmaHealthState::Failed);
            }
        }

        void NotifyReconnectSuccess()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_State == DmaHealthState::Failed)
            {
                TransitionToLocked(DmaHealthState::Healthy);
            }
            m_Stats.consecutiveFailures = 0;
            m_Stats.consecutiveSuccesses = 0;
        }

        DmaHealthState GetState()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_State;
        }

        DmaHealthStats GetStats()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            return m_Stats;
        }

        void Reset()
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_State = DmaHealthState::Healthy;
            m_Stats = DmaHealthStats{};
        }

    private:
        // Caller must hold m_Mutex
        void TransitionToLocked(DmaHealthState newState)
        {
            if (m_State == newState) return;

            const auto now = std::chrono::steady_clock::now();

            // Leaving Degraded: record recovery time and duration
            if (m_State == DmaHealthState::Degraded)
            {
                m_Stats.lastRecoveryTime = now;
                m_Stats.lastDegradedDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_Stats.degradedStartTime).count();
            }

            // Leaving Failed: record recovery time and failure duration
            if (m_State == DmaHealthState::Failed)
            {
                m_Stats.lastRecoveryTime = now;
                m_Stats.lastFailureDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_Stats.failedStartTime).count();
            }

            // Entering Degraded: record start time
            if (newState == DmaHealthState::Degraded)
            {
                m_Stats.degradedStartTime = now;
            }

            // Entering Failed: record start time
            if (newState == DmaHealthState::Failed)
            {
                m_Stats.failedStartTime = now;
            }

            m_State = newState;
        }

        mutable std::mutex      m_Mutex;
        DmaHealthState          m_State = DmaHealthState::Healthy;
        DmaHealthStats          m_Stats{};
    };
}

// 全局实例（C++17 inline 变量，header-only，多 TU 共享一份）
inline DmaHealth::DmaHealthTracker g_dmaHealth;
