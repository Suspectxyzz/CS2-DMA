#pragma once

// =====================================================================
//  StageTimer — RAII stage timing utility (microseconds)
//
//  Records elapsed time between construction and destruction into the
//  target atomic counter. Use as a local variable at the start of a
//  read phase to measure its duration.
// =====================================================================

#include <atomic>
#include <chrono>
#include <cstdint>

class StageTimer
{
public:
    explicit StageTimer(std::atomic<int64_t>& targetUs)
        : m_TargetUs(targetUs)
        , m_Start(std::chrono::steady_clock::now())
    {
    }

    ~StageTimer()
    {
        auto end = std::chrono::steady_clock::now();
        int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - m_Start).count();
        m_TargetUs.store(us, std::memory_order_relaxed);
    }

    StageTimer(const StageTimer&) = delete;
    StageTimer& operator=(const StageTimer&) = delete;

private:
    std::atomic<int64_t>&                      m_TargetUs;
    std::chrono::steady_clock::time_point      m_Start;
};

// =====================================================================
//  Global stage timing counters (defined in Threads.cpp)
// =====================================================================
extern std::atomic<int64_t> g_stageMatrixUs;
extern std::atomic<int64_t> g_stageLocalUs;
extern std::atomic<int64_t> g_stageEntitiesUs;
extern std::atomic<int64_t> g_stageScatterUs;
extern std::atomic<int64_t> g_stageWeaponUs;
extern std::atomic<int64_t> g_stageBombUs;
extern std::atomic<int64_t> g_stageProjectileUs;
