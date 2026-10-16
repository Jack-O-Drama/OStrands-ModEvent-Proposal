// EroBooks prototype addition, 2026-10-15. GPL-3.0.
#pragma once
#include <algorithm>
#include <cmath>

namespace OStrands
{
    // Elapsed time is unpaused real time, never game hours or the physics clamp.
    class AmbientLease
    {
    public:
        void Request(float interval)
        {
            if (!std::isfinite(interval) || interval <= 0.0f) {
                Stop();
                return;
            }
            if (!Active()) timer = 0.1f;
            period = std::clamp(interval, 1.0f, 30.0f);
            remaining = 8.0f;
        }
        void Stop() { remaining = 0.0f; timer = 0.1f; }
        bool Active() const { return remaining > 0.0f; }
        bool Advance(float dt, bool eligible)
        {
            if (!Active() || !std::isfinite(dt) || dt <= 0.0f) return false;
            remaining = std::max(0.0f, remaining - dt);
            if (!Active()) return false;
            if (!eligible) { timer = 0.1f; return false; }
            timer -= dt;
            if (timer > 0.0f) return false;
            timer = period; // Never burst to catch up after a stall.
            return true;
        }
    private:
        float remaining{ 0.0f };
        float timer{ 0.1f };
        float period{ 4.0f };
    };
}
