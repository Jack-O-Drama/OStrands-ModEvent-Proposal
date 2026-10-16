#include "Body.h"
#include "ExternalDrips.h"
#include "FluidManager.h"
#include "Hooks.h"
#include "OCumBridge.h"
#include "OStimBridge.h"
#include "SexLabBridge.h"
#include "Settings.h"
#include "StrandManager.h"

namespace {
	constexpr std::string_view kOldPlugin = "Data/SKSE/Plugins/OStrands.dll";

	bool OldPluginInstalled()
	{
		std::array<wchar_t, 1024> buffer{};
		const auto length = REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buffer.data(), static_cast<std::uint32_t>(buffer.size()));
		const std::filesystem::path self(std::wstring(buffer.data(), length));
		std::error_code error;
		return _wcsicmp(self.filename().c_str(), L"OStrands.dll") != 0 && std::filesystem::exists(kOldPlugin, error);
	}

	void OnStandDown(SKSE::MessagingInterface::Message* message)
	{
		if (message->type == SKSE::MessagingInterface::kDataLoaded) {
			RE::DebugMessageBox("OStrands: the old OStrands.dll from 0.7.0 or earlier is still in Data\\SKSE\\Plugins next to 0_OStrands.dll. "
								"Delete the old OStrands.dll and restart. OStrands stays off until then.");
		}
	}

	void InitLogging()
	{
		try {
			auto path = logger::log_directory();
			if (!path) {
				logger::warn("Log directory unavailable; keeping the default logger");
				return;
			}
			*path /= "OStrands.log";

			auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
			auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);
			log->set_pattern("[%H:%M:%S.%e] [%l] %v");
			spdlog::set_default_logger(std::move(log));
		} catch (const std::exception& error) {
			logger::warn("File logging unavailable; keeping the default logger: {}", error.what());
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case SKSE::MessagingInterface::kInputLoaded:
			OStrands::Hooks::Install();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			OStrands::OStimBridge::Connect();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			OStrands::ExternalDrips::Connect();
			OStrands::OCumBridge::Connect();
			OStrands::SexLabBridge::Connect();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			OStrands::ExternalDrips::Reset();
			OStrands::StrandManager::Get().Reset();
			OStrands::FluidManager::Get().Reset();
			OStrands::Body::Reset();
			OStrands::SexLabBridge::Reset();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
	SKSE::Init(skse, { .log = false });
	InitLogging();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} {} loaded on Skyrim {}", plugin->GetName(), plugin->GetVersion().string("."sv),
		REL::Module::get().version().string("."sv));

	if (OldPluginInstalled()) {
		logger::error("the old {} is still installed next to this plugin; delete it. OStrands stays off until then", kOldPlugin);
		if (auto* messaging = SKSE::GetMessagingInterface()) {
			messaging->RegisterListener(OnStandDown);
		}
		return true;
	}

	OStrands::Settings::Get().Load();
	auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::error("Could not register the SKSE message listener; plugin initialization failed");
		return false;
	}
	return true;
}
