// EroBooks prototype addition, 2026-10-15. GPL-3.0.
#include "ExternalDrips.h"
#include "ExternalDripsProtocol.h"
#include "StrandManager.h"
#include <cmath>
#include <map>
#include <algorithm>
#include <utility>

namespace OStrands::ExternalDrips
{
    namespace
    {
        std::mutex mutex;
        std::optional<float> pending;
        bool ping{ false };
        std::map<std::int32_t, std::vector<RE::ActorHandle>> scenes;
        std::string state{ "idle" };

        void Reply()
        {
            auto* source = SKSE::GetModCallbackEventSource();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!source || !player) return;
            SKSE::ModCallbackEvent event{ RE::BSFixedString("EBF_OStrands_Status"),
                RE::BSFixedString(state), 1.0f, player };
            source->SendEvent(&event);
        }

        class Listener : public RE::BSTEventSink<SKSE::ModCallbackEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
                RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
            {
                // API v1 deliberately accepts only requests sent by the player.
                if (!event || event->sender != RE::PlayerCharacter::GetSingleton() ||
                    !EqualASCII(event->eventName.c_str(), "EBF_OStrands_Request"))
                    return RE::BSEventNotifyControl::kContinue;
                const std::string_view command = event->strArg.c_str();
                std::scoped_lock guard(mutex);
                if (EqualASCII(command, "ping")) ping = true;
                else if (EqualASCII(command, "stop")) pending = 0.0f;
                else if (EqualASCII(command, "start")) {
                    pending = std::isfinite(event->numArg) && event->numArg > 0.0f ?
                        std::clamp(event->numArg, 1.0f, 30.0f) : 0.0f;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } listener;
    }
    void Connect()
    {
        if (auto* source = SKSE::GetModCallbackEventSource()) {
            source->AddEventSink(&listener);
            logger::info("EroBooks external drips prototype API v1 registered (player only)");
        }
    }
    void Update()
    {
        std::optional<float> request;
        bool respond;
        {
            std::scoped_lock guard(mutex);
            request.swap(pending);
            respond = std::exchange(ping, false);
        }
        // Scene graph changes occur only here/on the frame thread, not in the event sink.
        if (request) StrandManager::Get().RequestAmbient(*request);
        if (respond) Reply();
    }
    void Status(const char* value)
    {
        if (state == value) return;
        state = value;
        Reply();
    }
    void Scene(std::int32_t thread, const std::vector<RE::ActorHandle>& actors)
    {
        std::scoped_lock guard(mutex);
        scenes[thread] = actors;
    }
    void EndScene(std::int32_t thread)
    {
        std::scoped_lock guard(mutex);
        scenes.erase(thread);
    }
    bool InScene(RE::ActorHandle actor)
    {
        std::scoped_lock guard(mutex);
        for (const auto& [id, actors] : scenes)
            if (std::find(actors.begin(), actors.end(), actor) != actors.end()) return true;
        return false;
    }
    void Reset()
    {
        std::scoped_lock guard(mutex);
        pending.reset();
        ping = false;
        scenes.clear();
        state = "idle";
    }
}
