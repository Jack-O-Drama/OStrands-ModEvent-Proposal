#include "Hooks.h"

#include "Body.h"
#include "ExternalDrips.h"
#include "FluidManager.h"
#include "OStimBridge.h"
#include "SexLabBridge.h"
#include "Settings.h"
#include "StrandManager.h"

namespace OStrands::Hooks
{
	namespace
	{
		class FrameDriver : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static FrameDriver* Get()
			{
				static FrameDriver driver;
				return &driver;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const*, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				const auto now = std::chrono::steady_clock::now();
				if (now - _lastRun < 1ms) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const float realDelta = _lastRun.time_since_epoch().count() == 0 ? 0.0f :
					std::chrono::duration<float>(now - _lastRun).count();
				_lastRun = now;
				Heartbeat(now);
				ExternalDrips::Update();
				if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const float delta = RE::GetSecondsSinceLastFrame();
				Body::BeginFrame(delta);
				SexLabBridge::Update(delta);
				StrandManager::Get().Update(delta, realDelta);
				FluidManager::Get().Update(delta);
				CheckTestKey();
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			void CheckTestKey()
			{
				int key = 0;
				{
					auto settingsGuard = Settings::Lock();
					key = Settings::Get().fluidTestKey;
				}
				auto* input = RE::BSInputDeviceManager::GetSingleton();
				auto* keyboard = input ? input->GetKeyboard() : nullptr;
				bool  down = false;
				if (key > 0 && keyboard) {
					const auto& state = keyboard->GetRuntimeData().curState;
					down = static_cast<std::size_t>(key) < std::size(state) && (state[key] & 0x80) != 0;
				}
				if (down && !_testHeld && !Typing()) {
					OStimBridge::TestClimax();
				}
				_testHeld = down;
			}

			static bool Typing()
			{
				auto* ui = RE::UI::GetSingleton();
				auto* controls = RE::ControlMap::GetSingleton();
				return (ui && ui->IsMenuOpen(RE::Console::MENU_NAME)) || (controls && controls->GetRuntimeData().textEntryCount > 0);
			}

			void Heartbeat(std::chrono::steady_clock::time_point a_now)
			{
				++_calls;
				if (a_now - _lastBeat >= 10s) {
					auto settingsGuard = Settings::Lock();
					if (Settings::Get().debugLog) {
						logger::info("frame driver alive: {} updates in the last 10s", _calls);
					}
					_lastBeat = a_now;
					_calls = 0;
				}
			}

			std::chrono::steady_clock::time_point _lastRun{};
			std::chrono::steady_clock::time_point _lastBeat{ std::chrono::steady_clock::now() };
			int                                   _calls{ 0 };
			bool                                  _testHeld{ false };
		};
	}

	void Install()
	{
		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (!input) {
			logger::error("no input device manager; strands disabled");
			return;
		}
		input->PrependEventSink(FrameDriver::Get());
		logger::info("frame driver registered with the input device manager");
	}
}
