// EroBooks prototype addition, 2026-10-15. GPL-3.0.
#pragma once
namespace OStrands::ExternalDrips
{
    void Connect();
    void Update();
    void Reset();
    void Scene(std::int32_t thread, const std::vector<RE::ActorHandle>& actors);
    void EndScene(std::int32_t thread);
    bool InScene(RE::ActorHandle actor);
    void Status(const char* status);
}
