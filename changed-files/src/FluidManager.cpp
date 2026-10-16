#include "FluidManager.h"

#include "Body.h"
#include "ExternalDrips.h"
#include "Settings.h"
#include "fluid/Clusters.h"

#include <format>
#include <numbers>
#include <unordered_set>

namespace OStrands
{
	using namespace Anatomy;

	namespace
	{
		constexpr float       kUnitsPerMeter = 70.0f;
		constexpr float       kSpacing = 0.006f;
		constexpr float       kVolume = kSpacing * kSpacing * kSpacing;
		constexpr float       kKernel = 2.0f * kSpacing;
		constexpr float       kThreshold = 1.75f;
		constexpr float       kBulkEnter = 3.0f;
		constexpr float       kBulkLeave = 2.5f;
		constexpr float       kCovered = 0.3f;
		constexpr float       kPieceRadius = 4.0f * kSpacing;
		constexpr float       kSlotSpacing = 0.9f;
		constexpr float       kDribbleSpacing = 0.6f;
		constexpr std::size_t kBlobMinimum = 6;
		constexpr float       kSmoothing = 0.05f;
		constexpr float       kStep = 1.0f / 120.0f;
		constexpr int         kMaxSteps = 6;
		constexpr std::size_t kMaxDomains = 6;
		constexpr std::size_t kMaxBlobs = 96;
		constexpr std::size_t kMaxRopes = 64;
		constexpr std::size_t kMaxDebugShapes = 256;
		constexpr float       kDomainReach = 300.0f;
		constexpr float       kDomainStep = 80.0f;
		constexpr float       kGroundAbove = 80.0f;
		constexpr float       kGroundBelow = 300.0f;
		constexpr int         kGroundBudget = 24;
		constexpr float       kStreak = 1.5f;
		constexpr float       kStreakStart = 0.3f;
		constexpr float       kMaxStretch = 3.0f;
		constexpr float       kSquash = 0.45f;
		constexpr float       kGlobDisc = 0.8f;
		constexpr float       kGroundSearch = 400.0f;
		constexpr float       kLoadMemory = 6.0f;
		constexpr float       kNozzleOffset = 0.4f;
		constexpr float       kMaxNozzleSpeed = 2.0f;
		constexpr float       kDryLoad = 0.01f;
		constexpr float       kTypicalLoad = 0.2f;
		constexpr float       kAfterScene = 1.0f;
		constexpr float       kHangTime = 4.0f;
		constexpr float       kCarry = 0.6f;
		constexpr float       kCarrySmoothing = 0.08f;
		constexpr float       kTailStart = 0.55f;
		constexpr float       kTailSlowdown = 0.85f;
		constexpr float       kOozeSpeed = 0.15f;
		constexpr float       kHoleRadius = 2.0f;
		constexpr float       kNozzleHole = 1.0f;
		constexpr float       kSkinCell = 0.07f;
		constexpr float       kSkinMargin = 0.3f;
		constexpr std::size_t kWrapDots = 90;
		constexpr float       kSpinning = 4.0f * std::numbers::pi_v<float>;
		constexpr float       kExitDepth = 0.6f;
		constexpr float       kGushShare = 0.35f;
		constexpr float       kGushRate = 40.0f;
		constexpr float       kSeep = 0.03f;
		constexpr float       kLeak = 0.12f;
		constexpr float       kLeakSpeed = 0.1f;
		constexpr float       kReservoirLife = 90.0f;
		constexpr float       kDressedGrace = 2.5f;
		constexpr int         kGlobMin = 12;
		constexpr int         kGlobMax = 40;
		constexpr float       kMarkerRadius = 0.35f;
		constexpr float       kPointerLength = 3.0f;

		float Taper(float a_progress)
		{
			const float t = std::clamp((a_progress - kTailStart) / (1.0f - kTailStart), 0.0f, 1.0f);
			return 1.0f - kTailSlowdown * t * t * (3.0f - 2.0f * t);
		}

		constexpr const char* kBlobPath = "OStrands\\semen_blob.nif";
		constexpr const char* kRopePath = "OStrands\\semen_rope.nif";
		constexpr const char* kCollisionPath = "OStrands\\collision.nif";

		using Vec3 = Fluid::Vec3;

		struct MeshNames
		{
			RE::BSFixedString blobShape{ "BlobShape" };
			RE::BSFixedString ropeShape{ "RopeShape" };

			static const MeshNames& Get()
			{
				static const MeshNames names;
				return names;
			}
		};

		RE::NiPoint3 Normalized(RE::NiPoint3 v, const RE::NiPoint3& fallback)
		{
			return v.Unitize() > 1e-5f ? v : fallback;
		}

		Vec3 ToVec(const RE::NiPoint3& a_point) { return { a_point.x, a_point.y, a_point.z }; }

		Fluid::Parameters SemenParameters()
		{
			const auto& settings = Settings::Get();
			return { 4, settings.semenCohesion, settings.semenViscosity, 9.81f, settings.semenStretch, 30.0f, settings.semenStickiness };
		}

		const char* OpeningName(Opening a_opening)
		{
			return a_opening == Opening::kVagina ? "vagina" : a_opening == Opening::kAnus ? "anus" : "mouth";
		}

		bool TipInside(RE::Actor* a_giver, const RE::NiPoint3& a_center, const RE::NiPoint3& a_inward)
		{
			const auto tip = Body::TipOf(a_giver);
			return tip && (tip->point - a_center).Dot(a_inward) > 0.0f;
		}

		RE::NiPoint3 ToPoint(Vec3 a_vec) { return { a_vec.x, a_vec.y, a_vec.z }; }

		RE::NiMatrix3 Rotation(const GeometryMath::Frame& a_frame)
		{
			const auto& [x, y, z] = a_frame;
			RE::NiMatrix3 m;
			m.entry[0][0] = x.x; m.entry[1][0] = x.y; m.entry[2][0] = x.z;
			m.entry[0][1] = y.x; m.entry[1][1] = y.y; m.entry[2][1] = y.z;
			m.entry[0][2] = z.x; m.entry[1][2] = z.y; m.entry[2][2] = z.z;
			return m;
		}

		RE::NiMatrix3 ArcRotation(Vec3 a_from, Vec3 a_to)
		{
			RE::NiMatrix3 m;
			const float   c = Fluid::Dot(a_from, a_to);
			if (c < -0.999f) {
				Vec3 axis = std::abs(a_from.x) < 0.9f ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ 0.0f, 1.0f, 0.0f };
				axis = Fluid::Normalize(axis - a_from * Fluid::Dot(axis, a_from), { 0.0f, 0.0f, 1.0f });
				const float a[3]{ axis.x, axis.y, axis.z };
				for (int r = 0; r < 3; ++r) {
					for (int col = 0; col < 3; ++col) {
						m.entry[r][col] = 2.0f * a[r] * a[col] - (r == col ? 1.0f : 0.0f);
					}
				}
				return m;
			}
			const Vec3  v = Fluid::Cross(a_from, a_to);
			const float k = 1.0f / (1.0f + c);
			m.entry[0][0] = 1.0f - k * (v.y * v.y + v.z * v.z);
			m.entry[0][1] = -v.z + k * v.x * v.y;
			m.entry[0][2] = v.y + k * v.x * v.z;
			m.entry[1][0] = v.z + k * v.x * v.y;
			m.entry[1][1] = 1.0f - k * (v.x * v.x + v.z * v.z);
			m.entry[1][2] = -v.x + k * v.y * v.z;
			m.entry[2][0] = -v.y + k * v.x * v.z;
			m.entry[2][1] = v.x + k * v.y * v.z;
			m.entry[2][2] = 1.0f - k * (v.x * v.x + v.y * v.y);
			return m;
		}

		std::optional<float> CastDown(RE::bhkWorld* a_world, RE::NiPoint3 a_from, const RE::NiPoint3& a_to)
		{
			RE::bhkWorld* world = a_world;
			if (!world) {
				return std::nullopt;
			}
			const float          scale = RE::bhkWorld::GetWorldScale();
			const RE::NiPoint3   to = a_to;
			RE::NiPoint3         from = a_from;
			RE::BSReadLockGuard  guard(world->worldLock);
			for (int attempt = 0; attempt < 32 && from.z > to.z; ++attempt) {
				RE::bhkPickData pick{};
				pick.rayInput.from = RE::hkVector4(from * scale);
				pick.rayInput.to = RE::hkVector4(to * scale);
				pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraPick);
				pick.rayInput.enableShapeCollectionFilter = true;
				if (!world->PickObject(pick) || !pick.rayOutput.HasHit() || !pick.rayOutput.rootCollidable) {
					return std::nullopt;
				}
				const float fraction = pick.rayOutput.hitFraction;
				if (!std::isfinite(fraction) || fraction < 0.0f || fraction > 1.0f) {
					return std::nullopt;
				}
				const RE::NiPoint3 hit = from + (to - from) * fraction;
				const auto&        collidable = *pick.rayOutput.rootCollidable;
				const auto         layer = collidable.GetCollisionLayer();
				auto*              ref = RE::TESHavokUtilities::FindCollidableRef(collidable);
				if ((ref && ref->As<RE::Actor>()) || layer == RE::COL_LAYER::kBiped || layer == RE::COL_LAYER::kCharController ||
					layer == RE::COL_LAYER::kBipedNoCC || layer == RE::COL_LAYER::kDeadBip) {
					from = hit - RE::NiPoint3{ 0.0f, 0.0f, 2.0f };
					continue;
				}
				return hit.z;
			}
			return std::nullopt;
		}

		std::optional<float> GroundBelow(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_point)
		{
			return CastDown(a_cell ? a_cell->GetbhkWorld() : nullptr, a_point, a_point - RE::NiPoint3{ 0.0f, 0.0f, kGroundSearch });
		}

		Vec3 Stretch(Vec3 a_value, Vec3 a_axis, float a_factor)
		{
			const float side = 1.0f / std::sqrt(a_factor);
			return a_value * side + a_axis * (Fluid::Dot(a_value, a_axis) * (a_factor - side));
		}
	}

	FluidManager& FluidManager::Get()
	{
		static FluidManager manager;
		return manager;
	}

	void FluidManager::SetOCum(bool a_installed)
	{
		_ocum = a_installed;
	}

	void FluidManager::Climax(std::int32_t a_thread, RE::ActorHandle a_actor, std::vector<Pair> a_pairs, std::vector<RE::ActorHandle> a_actors)
	{
		std::scoped_lock guard(_lock);
		_pending.push_back({ Event::Type::kClimax, a_thread, a_actor, std::move(a_pairs), 0.0f, std::move(a_actors) });
	}

	void FluidManager::Load(RE::ActorHandle a_actor, float a_fraction)
	{
		std::scoped_lock guard(_lock);
		_pending.push_back({ Event::Type::kLoad, 0, a_actor, {}, a_fraction });
	}

	void FluidManager::EndThread(std::int32_t a_thread)
	{
		ExternalDrips::EndScene(a_thread);
		std::scoped_lock guard(_lock);
		_pending.push_back({ Event::Type::kEnd, a_thread });
	}

	void FluidManager::Scene(std::int32_t a_thread, std::vector<RE::ActorHandle> a_actors)
	{
		ExternalDrips::Scene(a_thread, a_actors);
		std::scoped_lock guard(_lock);
		_pending.push_back({ Event::Type::kScene, a_thread, {}, {}, 0.0f, std::move(a_actors) });
	}

	void FluidManager::Reset()
	{
		{
			std::scoped_lock guard(_lock);
			_pending.clear();
		}
		_emitters.clear();
		_reservoirs.clear();
		for (auto& domain : _domains) {
			Release(*domain);
		}
		_domains.clear();
		_loads.clear();
		_scenes.clear();
		HideShapes();
	}

	bool FluidManager::SemenEnabled() const
	{
		const int mode = Settings::Get().semen;
		return mode > 0 || (mode < 0 && _ocum);
	}

	void FluidManager::Apply(Event& a_event)
	{
		const auto& settings = Settings::Get();
		switch (a_event.type) {
		case Event::Type::kClimax:
			StartClimax(a_event);
			break;
		case Event::Type::kScene:
			_scenes[a_event.thread] = a_event.actors;
			break;
		case Event::Type::kEnd:
			_scenes.erase(a_event.thread);
			std::erase_if(_emitters, [&](Emitter& a_emitter) {
				if (a_emitter.thread != a_event.thread) {
					return false;
				}
				if (a_emitter.pulses.empty()) {
					return true;
				}
				const float cutoff = a_emitter.clock + kAfterScene;
				const auto  first = std::min(a_emitter.pulse + 1, a_emitter.pulses.size());
				const auto  late = std::find_if(a_emitter.pulses.begin() + first, a_emitter.pulses.end(), [&](const Pulse& a_pulse) { return a_pulse.at > cutoff; });
				a_emitter.pulses.erase(late, a_emitter.pulses.end());
				return false;
			});
			break;
		case Event::Type::kLoad:
			{
				const float fraction = std::isfinite(a_event.value) ? std::max(a_event.value, 0.0f) : kTypicalLoad;
				_loads[a_event.actor.native_handle()] = { fraction, 0.0f };
				const float amount = fraction < kDryLoad ? 0.0f : std::clamp(fraction / kTypicalLoad, 0.2f, 2.0f) * settings.semenAmount;
				for (auto& emitter : _emitters) {
					if (emitter.actor != a_event.actor || emitter.loaded) {
						continue;
					}
					emitter.loaded = true;
					emitter.wait = 0.0f;
					if (emitter.pulses.empty()) {
						emitter.amount = amount;
					} else if (amount <= 0.0f) {
						emitter.pulses.resize(emitter.pulse + (emitter.emitted > 0 ? 1 : 0));
					} else {
						const float scale = amount / std::max(emitter.amount, 0.01f);
						for (std::size_t p = emitter.pulse + 1; p < emitter.pulses.size(); ++p) {
							emitter.pulses[p].particles = std::max(1, static_cast<int>(std::lround(emitter.pulses[p].particles * scale)));
						}
						emitter.amount = amount;
					}
				}
				if (settings.debugLog) {
					const auto actor = a_event.actor.get();
					logger::info("OCum load for {}: {:.0f}% of storage, amount x{:.2f}{}", actor ? actor->GetDisplayFullName() : "?",
						fraction * 100.0f, amount, amount <= 0.0f ? " (dry)" : "");
				}
			}
			break;
		}
	}

	void FluidManager::StartClimax(const Event& a_event)
	{
		const auto& settings = Settings::Get();
		if (!SemenEnabled()) {
			if (settings.debugLog) {
				logger::info("semen is off; set [Semen] Enabled = true, or install OCum while it is auto");
			}
			return;
		}
		const auto actor = a_event.actor.get();
		auto*      root = actor ? actor->Get3D(false) : nullptr;
		if (!root) {
			return;
		}
		if (!HasSchlong(root)) {
			if (settings.debugLog) {
				logger::info("{} climaxed without a schlong; no semen", actor->GetDisplayFullName());
			}
			return;
		}
		if (IsStrapWearer(actor.get(), WornModels(actor.get()))) {
			if (settings.debugLog) {
				logger::info("{} climaxed wearing a strap-on; no semen", actor->GetDisplayFullName());
			}
			return;
		}
		if (std::ranges::any_of(_emitters, [&](const Emitter& e) { return e.actor == a_event.actor; })) {
			return;
		}
		Emitter emitter;
		emitter.id = ++_nextEmitter;
		emitter.thread = a_event.thread;
		emitter.actor = a_event.actor;
		emitter.hosts = a_event.actors;
		emitter.amount = settings.semenAmount;
		for (const auto& pair : a_event.pairs) {
			if (pair.giver == a_event.actor && IsPenis(pair.act)) {
				emitter.pair = pair;
				break;
			}
		}
		if (auto it = _loads.find(a_event.actor.native_handle()); it != _loads.end()) {
			const float fraction = it->second.first;
			if (fraction < kDryLoad) {
				if (settings.debugLog) {
					logger::info("{} climaxed dry (OCum)", actor->GetDisplayFullName());
				}
				return;
			}
			emitter.amount = std::clamp(fraction / kTypicalLoad, 0.2f, 2.0f) * settings.semenAmount;
			emitter.loaded = true;
		} else if (_ocum && a_event.thread == 0) {
			emitter.wait = settings.ocumWait;
		}
		if (settings.debugLog) {
			logger::info("{} climaxes in thread {}: {}, amount x{:.2f}{}", actor->GetDisplayFullName(), a_event.thread,
				emitter.pair ? std::format("{} partner", ActName(emitter.pair->act)) : std::string("no penetration"), emitter.amount,
				emitter.wait > 0.0f ? std::format(", waiting up to {}s for OCum", emitter.wait) : std::string{});
		}
		_emitters.push_back(std::move(emitter));
	}

	void FluidManager::Plan(Emitter& a_emitter)
	{
		const auto&                           settings = Settings::Get();
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		auto&                                 pulses = a_emitter.pulses;
		pulses.clear();
		const float amount = a_emitter.amount;
		const int count = settings.semenPulses;
		float     at = 0.0f;
		for (int k = 0; k < count; ++k) {
			const float progress = count > 1 ? static_cast<float>(k) / static_cast<float>(count - 1) : 0.0f;
			const float strength = std::max(0.15f, 1.0f - 0.85f * progress) * (0.85f + 0.3f * unit(_rng));
			const bool wavy = unit(_rng) < 0.6f;
			pulses.push_back({ at, (0.2f + 2.0f * strength) * settings.semenStrength, kSlotSpacing * kSpacing,
				std::max(1, static_cast<int>(std::lround(40.0f * strength * amount))), 0.03f + 0.06f * unit(_rng),
				unit(_rng) * 2.0f * std::numbers::pi_v<float>, wavy ? 0.03f + 0.09f * unit(_rng) : 0.0f, 6.0f + 8.0f * unit(_rng) });
			at += 0.5f + 0.35f * progress + 0.15f * unit(_rng);
		}
		pulses.push_back({ at, 0.08f, kDribbleSpacing * kSpacing, std::max(1, static_cast<int>(std::lround(8.0f * amount))), 0.1f, 0.0f });
	}

	bool FluidManager::Step(Emitter& a_emitter, float a_delta)
	{
		const auto& settings = Settings::Get();
		if (a_emitter.wait > 0.0f) {
			a_emitter.wait -= a_delta;
			if (a_emitter.wait > 0.0f) {
				return true;
			}
		}
		if (a_emitter.pulses.empty()) {
			if (a_emitter.amount <= 0.0f) {
				return false;
			}
			Plan(a_emitter);
		}
		const auto actor = a_emitter.actor.get();

		std::vector<Point> rim;
		RE::NiPoint3       center;
		RE::NiPoint3       inward;
		RE::NiPoint3       axis;
		RE::NiPoint3       nozzle;
		bool               inside = false;
		std::vector<RE::ActorHandle> hosts = a_emitter.hosts;
		if (std::ranges::find(hosts, a_emitter.actor) == hosts.end()) {
			hosts.push_back(a_emitter.actor);
		}
		const auto tip = Body::TipOf(actor.get());
		if (!tip) {
			return false;
		}
		axis = tip->axis;
		const RE::NiPoint3 end = tip->point;
		nozzle = end + axis * kNozzleOffset;
		if (a_emitter.pair) {
			if (std::ranges::find(hosts, a_emitter.pair->receiver) == hosts.end()) {
				hosts.push_back(a_emitter.pair->receiver);
			}
			const auto receiver = a_emitter.pair->receiver.get();
			auto*      receiver3D = receiver ? receiver->Get3D(false) : nullptr;
			if (receiver3D && !FindOpening(receiver3D, OpeningOf(a_emitter.pair->act), rim, center, inward)) {
				inside = (end - center).Dot(inward) > 0.0f;
			}
		}
		if (a_emitter.hasNozzle && a_delta > 0.0f) {
			RE::NiPoint3 measured = (nozzle - a_emitter.lastNozzle) / (a_delta * kUnitsPerMeter);
			if (const float speed = measured.Length(); speed > kMaxNozzleSpeed) {
				measured = measured * (kMaxNozzleSpeed / speed);
			}
			const float blend = 1.0f - std::exp(-a_delta / kCarrySmoothing);
			a_emitter.carried = a_emitter.carried + (measured - a_emitter.carried) * blend;
		}
		const RE::NiPoint3 carried = a_emitter.carried * kCarry;
		a_emitter.lastNozzle = nozzle;
		a_emitter.hasNozzle = true;
		if (inside != a_emitter.inside && settings.debugLog) {
			logger::info("{} is now {} the opening", actor->GetDisplayFullName(), inside ? "inside" : "outside");
		}
		a_emitter.inside = inside;
		a_emitter.clock += a_delta;

		Domain* domain = FindDomain(a_emitter.domain);
		if (!domain) {
			domain = DomainFor(actor.get(), nozzle, hosts);
			if (!domain) {
				return false;
			}
			a_emitter.domain = domain->id;
		}
		if (a_emitter.pair) {
			const std::pair opening{ a_emitter.pair->receiver, OpeningOf(a_emitter.pair->act) };
			if (std::ranges::find(domain->openings, opening) == domain->openings.end()) {
				domain->openings.push_back(opening);
			}
		}

		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const RE::NiPoint3                    side = Normalized(axis.Cross({ 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
		const RE::NiPoint3                    lift = side.Cross(axis);
		auto&                                 particles = domain->particles;
		const std::size_t                     capacity = static_cast<std::size_t>(settings.fluidMaxParticles);
		const auto                            source = [&]() {
			if (!a_emitter.oozing || rim.empty()) {
				return std::pair{ nozzle, axis };
			}
			return std::pair{ center - inward * kExitDepth, -inward };
		};

		const float                now = a_emitter.clock - domain->accumulator;
		std::optional<std::size_t> newest;
		while (a_emitter.pulse < a_emitter.pulses.size()) {
			const Pulse& pulse = a_emitter.pulses[a_emitter.pulse];
			if (now < pulse.at) {
				break;
			}
			if (a_emitter.emitted == 0 && a_emitter.slot <= 0.0f) {
				a_emitter.run = ++_nextRun;
				a_emitter.oozing = inside && !rim.empty();
				a_emitter.count = a_emitter.oozing ? std::max(1, static_cast<int>(std::lround(pulse.particles * settings.semenOverflow))) :
				                                     pulse.particles;
				if (a_emitter.oozing) {
					Deposit(a_emitter, static_cast<float>(pulse.particles - a_emitter.count));
				}
			}
			const auto [origin, outlet] = source();
			const bool         oozing = a_emitter.oozing && !rim.empty();
			const float        spacing = oozing ? kDribbleSpacing * kSpacing : pulse.spacing;
			const float        elapsed = now - pulse.at;
			const float        tilt = oozing ? 0.0f : std::tan(pulse.spread);
			const RE::NiPoint3 aim = Normalized(outlet + (side * std::cos(pulse.angle) + lift * std::sin(pulse.angle)) * tilt, outlet);
			while (a_emitter.emitted < a_emitter.count && a_emitter.slot <= elapsed) {
				const float speed = oozing ? kOozeSpeed : pulse.speed * Taper(static_cast<float>(a_emitter.emitted) / static_cast<float>(a_emitter.count));
				const float age = elapsed - a_emitter.slot;
				a_emitter.slot += spacing / std::max(speed, 0.02f);
				++a_emitter.emitted;
				if (particles.size() >= capacity) {
					continue;
				}
				const float        sway = oozing ? 0.0f : pulse.wave * std::sin(2.0f * std::numbers::pi_v<float> * pulse.frequency * (a_emitter.slot - age));
				const RE::NiPoint3 bend = (lift * std::cos(pulse.angle) - side * std::sin(pulse.angle)) * sway;
				const RE::NiPoint3 wobble = (side * (unit(_rng) - 0.5f) + lift * (unit(_rng) - 0.5f)) * (oozing ? 0.3f : 0.03f);
				const RE::NiPoint3 direction = Normalized(aim + bend + wobble, aim);
				const RE::NiPoint3 velocity = direction * speed + carried * (oozing ? 0.5f : 1.0f);
				const float        turn = unit(_rng) * 2.0f * std::numbers::pi_v<float>;
				const RE::NiPoint3 disc = oozing ? (side * std::cos(turn) + lift * std::sin(turn)) * (kGlobDisc * kSpacing * kUnitsPerMeter * std::sqrt(unit(_rng))) :
				                                   RE::NiPoint3{};
				Fluid::Particle    particle;
				particle.life = settings.fluidLifetime * (0.85f + 0.3f * unit(_rng));
				particle.position = ToLocal(*domain, origin + disc + velocity * (age * kUnitsPerMeter));
				particle.velocity = ToVec(velocity);
				particle.run = a_emitter.run;
				if (!particles.empty() && particles.back().run == a_emitter.run) {
					const float gap = Fluid::Length(particle.position - particles.back().position);
					if (gap < 2.0f * kSpacing) {
						particles.back().link = std::max(spacing, gap);
					}
				}
				newest = particles.size();
				particles.push_back(particle);
			}
			if (a_emitter.emitted < a_emitter.count) {
				break;
			}
			++a_emitter.pulse;
			a_emitter.emitted = 0;
			a_emitter.slot = 0.0f;
		}
		const Fluid::Vec3 hold = ToLocal(*domain, source().first);
		const auto        held = std::ranges::find_if(domain->anchors, [&](const Fluid::Anchor& a) { return a.owner == a_emitter.id; });
		if (newest) {
			const float rest = std::max(Fluid::Length(particles[*newest].position - hold), 0.5f * kSpacing);
			if (held != domain->anchors.end()) {
				*held = { *newest, hold, rest, a_emitter.id };
			} else {
				domain->anchors.push_back({ *newest, hold, rest, a_emitter.id });
			}
		} else if (held != domain->anchors.end()) {
			held->point = hold;
		}
		if (a_emitter.pulse < a_emitter.pulses.size()) {
			return true;
		}
		a_emitter.hang += a_delta;
		const bool holding = std::ranges::any_of(domain->anchors, [&](const Fluid::Anchor& a) { return a.owner == a_emitter.id; });
		if (holding && a_emitter.hang < kHangTime) {
			return true;
		}
		std::erase_if(domain->anchors, [&](const Fluid::Anchor& a) { return a.owner == a_emitter.id; });
		return false;
	}

	void FluidManager::Deposit(const Emitter& a_emitter, float a_particles)
	{
		if (!a_emitter.pair || a_particles <= 0.0f) {
			return;
		}
		const Opening opening = OpeningOf(a_emitter.pair->act);
		auto          it = std::ranges::find_if(_reservoirs, [&](const Reservoir& r) {
			return r.actor == a_emitter.pair->receiver && r.opening == opening;
		});
		if (it == _reservoirs.end()) {
			Reservoir reservoir;
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			reservoir.id = ++_nextEmitter;
			reservoir.actor = a_emitter.pair->receiver;
			reservoir.phase = unit(_rng) * 2.0f * std::numbers::pi_v<float>;
			reservoir.period = 1.4f + unit(_rng);
			reservoir.opening = opening;
			_reservoirs.push_back(std::move(reservoir));
			it = std::prev(_reservoirs.end());
		}
		it->plug = a_emitter.actor;
		it->hosts = a_emitter.hosts;
		it->volume += a_particles;
		it->plugged = true;
		it->age = 0.0f;
	}

	bool FluidManager::Leak(Reservoir& a_reservoir, float a_delta)
	{
		const auto& settings = Settings::Get();
		a_reservoir.age += a_delta;
		const auto actor = a_reservoir.actor.get();
		auto*      root = actor ? actor->Get3D(false) : nullptr;
		const bool holding = std::ranges::any_of(_domains, [&](const auto& d) {
			return std::ranges::any_of(d->anchors, [&](const Fluid::Anchor& a) { return a.owner == a_reservoir.id; });
		});
		const bool empty = a_reservoir.volume < 0.5f && a_reservoir.gush < 0.5f;
		const bool dressed = root && actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
		if (dressed) {
			if (a_reservoir.dressed <= 0.0f && settings.debugLog) {
				logger::info("{} is dressed; their {} spills for {}s more", actor->GetDisplayFullName(), OpeningName(a_reservoir.opening), kDressedGrace);
			}
			a_reservoir.dressed += a_delta;
		}
		if (!root || a_reservoir.dressed > kDressedGrace || a_reservoir.age > kReservoirLife || (empty && !holding)) {
			if (settings.debugLog && actor) {
				logger::info("{}'s {} stops leaking with {:.0f} left", actor->GetDisplayFullName(), OpeningName(a_reservoir.opening), a_reservoir.volume);
			}
			return false;
		}
		std::vector<Point> rim;
		RE::NiPoint3       center;
		RE::NiPoint3       inward;
		if (FindOpening(root, a_reservoir.opening, rim, center, inward)) {
			return false;
		}
		const RE::NiPoint3 outward = -inward;
		const RE::NiPoint3 exit = center + outward * kExitDepth;
		const auto         plug = a_reservoir.plug.get();
		const bool         plugged = !dressed && TipInside(plug.get(), center, inward);
		if (a_reservoir.plugged && !plugged && a_reservoir.volume >= 1.0f) {
			const float gush = a_reservoir.volume * kGushShare;
			a_reservoir.gush += gush;
			a_reservoir.volume -= gush;
			if (settings.debugLog) {
				logger::info("{} pulls out of {}'s {}: {:.0f} gush out, {:.0f} still inside", plug ? plug->GetDisplayFullName() : "?",
					actor->GetDisplayFullName(), OpeningName(a_reservoir.opening), gush, a_reservoir.volume);
			}
		}
		a_reservoir.plugged = plugged;

		std::vector<RE::ActorHandle> hosts = a_reservoir.hosts;
		if (std::ranges::find(hosts, a_reservoir.actor) == hosts.end()) {
			hosts.push_back(a_reservoir.actor);
		}
		Domain* domain = FindDomain(a_reservoir.domain);
		if (!domain) {
			domain = DomainFor(actor.get(), exit, hosts);
			if (!domain) {
				return false;
			}
			a_reservoir.domain = domain->id;
		}
		const std::pair opening{ a_reservoir.actor, a_reservoir.opening };
		if (std::ranges::find(domain->openings, opening) == domain->openings.end()) {
			domain->openings.push_back(opening);
		}

		const float facing = std::clamp(0.5f - outward.z, 0.05f, 1.0f);
		const float wave = std::max(0.0f, std::sin(2.0f * std::numbers::pi_v<float> * a_reservoir.age / a_reservoir.period + a_reservoir.phase));
		const float pulse = 0.25f + 1.5f * wave * wave;
		float       rate = (plugged ? a_reservoir.volume * kSeep : a_reservoir.volume * kLeak * facing) * pulse;
		if (!plugged && a_reservoir.gush > 0.0f) {
			rate += kGushRate * std::max(facing, 0.3f);
		}
		RE::NiPoint3 carried;
		if (a_reservoir.hasExit && a_delta > 0.0f) {
			carried = (exit - a_reservoir.lastExit) / (a_delta * kUnitsPerMeter);
			if (const float speed = carried.Length(); speed > kMaxNozzleSpeed) {
				carried = carried * (kMaxNozzleSpeed / speed);
			}
		}
		a_reservoir.lastExit = exit;
		a_reservoir.hasExit = true;
		a_reservoir.release += rate * a_delta;

		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const RE::NiPoint3                    side = Normalized(outward.Cross({ 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
		const RE::NiPoint3                    lift = side.Cross(outward);
		auto&                                 particles = domain->particles;
		const std::size_t                     capacity = static_cast<std::size_t>(settings.fluidMaxParticles);
		const Fluid::Vec3                     hold = ToLocal(*domain, exit);
		std::optional<std::size_t>            newest;
		while (a_reservoir.release >= 1.0f && (a_reservoir.volume >= 1.0f || a_reservoir.gush >= 1.0f)) {
			a_reservoir.release -= 1.0f;
			if (a_reservoir.gush >= 1.0f) {
				a_reservoir.gush -= 1.0f;
			} else {
				a_reservoir.volume -= 1.0f;
			}
			if (particles.size() >= capacity) {
				continue;
			}
			if (a_reservoir.glob <= 0) {
				a_reservoir.run = ++_nextRun;
				a_reservoir.glob = std::uniform_int_distribution<int>(kGlobMin, kGlobMax)(_rng);
				a_reservoir.globSpeed = 0.5f + 0.8f * unit(_rng);
			}
			--a_reservoir.glob;
			const RE::NiPoint3 wobble = (side * (unit(_rng) - 0.5f) + lift * (unit(_rng) - 0.5f)) * 0.4f;
			const RE::NiPoint3 velocity = Normalized(outward + wobble, outward) * (kLeakSpeed * a_reservoir.globSpeed) + carried * 0.5f;
			const float        turn = unit(_rng) * 2.0f * std::numbers::pi_v<float>;
			const RE::NiPoint3 disc = (side * std::cos(turn) + lift * std::sin(turn)) * (kGlobDisc * kSpacing * kUnitsPerMeter * std::sqrt(unit(_rng)));
			Fluid::Particle    particle;
			particle.life = settings.fluidLifetime * (0.85f + 0.3f * unit(_rng));
			particle.position = ToLocal(*domain, exit + disc + velocity * (unit(_rng) * a_delta * kUnitsPerMeter));
			particle.velocity = ToVec(velocity);
			particle.run = a_reservoir.run;
			if (!particles.empty() && particles.back().run == a_reservoir.run) {
				const float gap = Fluid::Length(particle.position - particles.back().position);
				if (gap < 2.0f * kSpacing) {
					particles.back().link = std::max(kDribbleSpacing * kSpacing, gap);
				}
			}
			newest = particles.size();
			particles.push_back(particle);
		}
		if (a_reservoir.volume < 1.0f && a_reservoir.gush < 1.0f) {
			a_reservoir.release = 0.0f;
		}
		const auto held = std::ranges::find_if(domain->anchors, [&](const Fluid::Anchor& a) { return a.owner == a_reservoir.id; });
		if (newest) {
			const float rest = std::max(Fluid::Length(particles[*newest].position - hold), 0.5f * kSpacing);
			if (held != domain->anchors.end()) {
				*held = { *newest, hold, rest, a_reservoir.id };
			} else {
				domain->anchors.push_back({ *newest, hold, rest, a_reservoir.id });
			}
		} else if (held != domain->anchors.end()) {
			held->point = hold;
		}
		return true;
	}

	FluidManager::Domain* FluidManager::FindDomain(std::uint32_t a_id)
	{
		if (a_id == 0) {
			return nullptr;
		}
		auto it = std::ranges::find_if(_domains, [&](const auto& d) { return d->id == a_id; });
		return it != _domains.end() ? it->get() : nullptr;
	}

	FluidManager::Domain* FluidManager::DomainFor(RE::Actor* a_actor, const RE::NiPoint3& a_point, const std::vector<RE::ActorHandle>& a_hosts)
	{
		const auto& settings = Settings::Get();
		auto*       cell = a_actor->GetParentCell();
		if (!cell) {
			return nullptr;
		}
		float ground = a_actor->GetPosition().z;
		for (const auto& handle : a_hosts) {
			if (const auto host = handle.get()) {
				ground = std::min(ground, host->GetPosition().z);
			}
		}
		if (const auto hit = GroundBelow(cell, a_point)) {
			ground = *hit;
		}
		for (auto& domain : _domains) {
			const RE::NiPoint3 offset = a_point - domain->origin;
			if (domain->cell == cell && std::abs(domain->origin.z - ground) < kDomainStep &&
				std::hypot(offset.x, offset.y) < kDomainReach) {
				for (const auto& handle : a_hosts) {
					if (std::ranges::find(domain->hosts, handle) == domain->hosts.end()) {
						domain->hosts.push_back(handle);
					}
				}
				return domain.get();
			}
		}
		if (_domains.size() >= kMaxDomains) {
			Release(*_domains.front());
			_domains.erase(_domains.begin());
		}
		auto domain = std::make_unique<Domain>();
		domain->id = ++_nextDomain;
		domain->origin = { a_point.x, a_point.y, ground };
		domain->cell = cell;
		domain->hosts = a_hosts;
		domain->blobs.reserve(kMaxBlobs);
		domain->ropes.reserve(kMaxRopes);
		domain->solver = std::make_unique<Fluid::Solver>(kSpacing, SemenParameters());
		if (settings.debugLog) {
			logger::info("semen pool {} opened, floor {:.1f} units below the emitter", domain->id,
				a_point.z - ground);
		}
		_domains.push_back(std::move(domain));
		return _domains.back().get();
	}

	bool FluidManager::Attach(Domain& a_domain)
	{
		RE::NiNode* parent = nullptr;
		RE::NiNode* player = nullptr;
		for (const auto& handle : a_domain.hosts) {
			const auto actor = handle.get();
			auto*      model = actor ? actor->Get3D(false) : nullptr;
			auto*      node = model ? model->AsNode() : nullptr;
			if (!node) {
				continue;
			}
			if (a_domain.root && a_domain.root->parent == node) {
				parent = node;
				break;
			}
			if (actor->IsPlayerRef()) {
				player = player ? player : node;
			} else if (!parent) {
				parent = node;
			}
		}
		if (!parent) {
			parent = player;
		}
		if (!parent) {
			return false;
		}
		if (!a_domain.root) {
			a_domain.root.reset(RE::NiNode::Create(0));
			if (!a_domain.root) {
				return false;
			}
			a_domain.root->name = "OStrandsFluid";
			a_domain.root->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		if (a_domain.root->parent != parent) {
			if (a_domain.root->parent) {
				a_domain.root->parent->DetachChild2(a_domain.root.get());
			}
			parent->AttachChild(a_domain.root.get(), true);
		}
		a_domain.root->local = parent->world.Invert();
		return true;
	}

	void FluidManager::Simulate(Domain& a_domain, float a_delta)
	{
		a_domain.age += a_delta;
		if (a_domain.particles.empty()) {
			a_domain.accumulator = 0.0f;
			return;
		}
		a_domain.accumulator += a_delta;
		auto& capsules = a_domain.capsules;
		auto& holes = a_domain.holes;
		auto& patches = a_domain.skin.Patches();
		capsules.clear();
		holes.clear();
		patches.clear();
		const auto add = [&](std::span<const Body::Capsule> a_shapes) {
			for (const auto& capsule : a_shapes) {
				capsules.push_back({ ToLocal(a_domain, capsule.a), ToLocal(a_domain, capsule.b), capsule.radius / kUnitsPerMeter,
					ToVec(capsule.velocityA / kUnitsPerMeter), ToVec(capsule.velocityB / kUnitsPerMeter) });
			}
		};
		Vec3 low = a_domain.particles.front().position;
		Vec3 high = low;
		for (const auto& particle : a_domain.particles) {
			low = { std::min(low.x, particle.position.x), std::min(low.y, particle.position.y), std::min(low.z, particle.position.z) };
			high = { std::max(high.x, particle.position.x), std::max(high.y, particle.position.y), std::max(high.z, particle.position.z) };
		}
		low = low - Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
		high = high + Vec3{ kSkinMargin, kSkinMargin, kSkinMargin };
		for (const auto& handle : a_domain.hosts) {
			const auto actor = handle.get();
			const auto surface = Body::SurfaceOf(actor.get());
			if (surface.patches.empty()) {
				add(Body::Of(actor.get()));
				add(Body::ShaftOf(actor.get()));
				continue;
			}
			for (const auto& patch : surface.patches) {
				const Vec3 position = ToLocal(a_domain, patch.position);
				if (position.x < low.x || position.y < low.y || position.z < low.z || position.x > high.x || position.y > high.y ||
					position.z > high.z) {
					continue;
				}
				patches.push_back({ position, ToVec(patch.normal), ToVec(patch.velocity / kUnitsPerMeter) });
			}
			add(surface.extras);
		}
		a_domain.skin.Build(kSkinCell);
		for (const auto& emitter : _emitters) {
			if (emitter.domain == a_domain.id && emitter.hasNozzle) {
				holes.push_back({ ToLocal(a_domain, emitter.lastNozzle), kNozzleHole / kUnitsPerMeter });
			}
		}
		std::vector<Point> rim;
		RE::NiPoint3       center;
		RE::NiPoint3       inward;
		for (const auto& [handle, opening] : a_domain.openings) {
			const auto actor = handle.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			if (root && !FindOpening(root, opening, rim, center, inward)) {
				holes.push_back({ ToLocal(a_domain, center), kHoleRadius * root->world.scale / kUnitsPerMeter });
			}
		}
		SampleGround(a_domain);
		a_domain.solver->Configure(SemenParameters());
		const Fluid::Colliders colliders{ capsules, holes, &a_domain.skin };
		int                    steps = 0;
		while (a_domain.accumulator >= kStep && steps < kMaxSteps) {
			a_domain.solver->Step(a_domain.particles, a_domain.anchors, colliders, a_domain.ground, kStep);
			a_domain.accumulator -= kStep;
			++steps;
		}
		if (steps == kMaxSteps) {
			a_domain.accumulator = 0.0f;
		}
		std::erase_if(a_domain.anchors, [](const Fluid::Anchor& a_anchor) { return a_anchor.rest <= 0.0f; });
	}

	void FluidManager::SampleGround(Domain& a_domain)
	{
		auto* world = a_domain.cell ? a_domain.cell->GetbhkWorld() : nullptr;
		if (!world) {
			return;
		}
		auto&                             ground = a_domain.ground;
		std::unordered_set<std::uint64_t> seen;
		int                               budget = kGroundBudget;
		for (const auto& particle : a_domain.particles) {
			if (particle.asleep || budget <= 0) {
				continue;
			}
			const int x = ground.Node(particle.position.x);
			const int y = ground.Node(particle.position.y);
			for (int dx = 0; dx <= 1 && budget > 0; ++dx) {
				for (int dy = 0; dy <= 1 && budget > 0; ++dy) {
					const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x + dx)) << 32) | static_cast<std::uint32_t>(y + dy);
					if (ground.Known(x + dx, y + dy) || !seen.insert(key).second) {
						continue;
					}
					--budget;
					const RE::NiPoint3 point = ToWorld(a_domain, { static_cast<float>(x + dx) * ground.Cell(), static_cast<float>(y + dy) * ground.Cell(), 0.0f });
					const auto         hit = CastDown(world, point + RE::NiPoint3{ 0.0f, 0.0f, kGroundAbove }, point - RE::NiPoint3{ 0.0f, 0.0f, kGroundBelow });
					ground.Set(x + dx, y + dy, hit ? (*hit - a_domain.origin.z) / kUnitsPerMeter : 0.0f);
				}
			}
		}
	}

	void FluidManager::Expire(Domain& a_domain, float a_delta)
	{
		auto& particles = a_domain.particles;
		bool  expired = false;
		for (auto& particle : particles) {
			particle.life -= a_delta;
			expired = expired || particle.life <= 0.0f;
		}
		if (!expired) {
			return;
		}
		std::vector<std::size_t> remap(particles.size(), Fluid::kNone);
		std::size_t              write = 0;
		bool                     previousKept = false;
		for (std::size_t read = 0; read < particles.size(); ++read) {
			if (particles[read].life <= 0.0f) {
				if (previousKept) {
					particles[write - 1].link = 0.0f;
				}
				previousKept = false;
				continue;
			}
			remap[read] = write;
			particles[write++] = particles[read];
			previousKept = true;
		}
		particles.resize(write);
		for (auto& blob : a_domain.blobs) {
			if (blob.live) {
				blob.seed = blob.seed < remap.size() ? remap[blob.seed] : Fluid::kNone;
				blob.live = blob.seed != Fluid::kNone;
			}
		}
		std::erase_if(a_domain.anchors, [&](Fluid::Anchor& a_anchor) {
			a_anchor.particle = a_anchor.particle < remap.size() ? remap[a_anchor.particle] : Fluid::kNone;
			return a_anchor.particle == Fluid::kNone;
		});
	}

	FluidManager::Meshes& FluidManager::MeshesFor()
	{
		auto& meshes = _meshes;
		if (meshes.failed || (meshes.blob && meshes.rope)) {
			return meshes;
		}
		RE::BSModelDB::DBTraits::ArgsType args{};
		for (auto [path, target] : { std::pair{ kBlobPath, &meshes.blob }, std::pair{ kRopePath, &meshes.rope } }) {
			const auto error = RE::BSModelDB::Demand(path, *target, args);
			if (error != RE::BSResource::ErrorCode::kNone || !*target) {
				logger::error("could not load meshes\\{} (error {}); semen stays invisible", path, std::to_underlying(error));
				meshes.failed = true;
				return meshes;
			}
		}
		if (_directions.empty()) {
			for (std::size_t i = 0;; ++i) {
				auto* bone = meshes.blob->GetObjectByName(std::format("Blob{:02}", i));
				if (!bone) {
					break;
				}
				_directions.push_back(Fluid::Normalize(ToVec(bone->local.translate), { 0.0f, 0.0f, 1.0f }));
			}
			_scratch.resize(_directions.size());
		}
		if (_directions.size() < 12 || !meshes.rope->GetObjectByName(std::format("Rope{:02}", kRopeBones - 1))) {
			logger::error("semen meshes are from another version; reinstall OStrands");
			meshes.failed = true;
		}
		return meshes;
	}

	RE::NiNode* FluidManager::CollisionMesh()
	{
		if (!_collisionMesh && !_collisionMeshFailed) {
			RE::BSModelDB::DBTraits::ArgsType args{};
			const auto error = RE::BSModelDB::Demand(kCollisionPath, _collisionMesh, args);
			if (error != RE::BSResource::ErrorCode::kNone || !_collisionMesh ||
				!_collisionMesh->GetObjectByName(MeshNames::Get().ropeShape)) {
				_collisionMeshFailed = true;
			}
			for (std::size_t b = 0; !_collisionMeshFailed && b < kRopeBones; ++b) {
				_collisionMeshFailed = !_collisionMesh->GetObjectByName(std::format("Rope{:02}", b));
			}
			if (_collisionMeshFailed) {
				logger::error("collision display: missing or incompatible meshes\\{}; reinstall the debug assets", kCollisionPath);
				_collisionMesh.reset();
			}
		}
		return _collisionMesh.get();
	}

	void FluidManager::Shade(Domain& a_domain, RE::NiAVObject* a_root)
	{
		if (a_domain.diagnostic) {
			return;
		}
		const auto& settings = Settings::Get();
		const auto& names = MeshNames::Get();
		for (const auto& name : { names.blobShape, names.ropeShape }) {
			auto* shape = a_root->GetObjectByName(name);
			auto* geometry = shape ? shape->AsGeometry() : nullptr;
			auto* shader = geometry ? geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			if (!shader) {
				continue;
			}
			shader->SetMaterialAlpha(settings.semenOpacity);
			if (shader->GetMaterialType() == RE::BSShaderMaterial::Type::kLighting) {
				if (auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(shader->GetBaseMaterial())) {
					material->specularColorScale = settings.shine;
					material->specularPower = settings.gloss;
				}
			}
		}
	}

	FluidManager::Blob* FluidManager::AcquireBlob(Domain& a_domain)
	{
		for (auto& blob : a_domain.blobs) {
			if (!blob.used) {
				blob.used = true;
				return &blob;
			}
		}
		auto& meshes = MeshesFor();
		if (meshes.failed || a_domain.blobs.size() >= kMaxBlobs) {
			return nullptr;
		}
		auto* clone = meshes.blob->Clone();
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			return nullptr;
		}
		Blob blob;
		blob.root.reset(node);
		for (std::size_t i = 0; i < _directions.size(); ++i) {
			auto* bone = node->GetObjectByName(std::format("Blob{:02}", i));
			if (!bone) {
				return nullptr;
			}
			blob.bones.push_back(bone);
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		if (auto* shape = node->GetObjectByName(MeshNames::Get().blobShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		node->local = RE::NiTransform{};
		a_domain.root->AttachChild(node, true);
		Shade(a_domain, node);
		blob.used = true;
		a_domain.blobs.push_back(std::move(blob));
		return &a_domain.blobs.back();
	}

	FluidManager::Rope* FluidManager::AcquireRope(Domain& a_domain)
	{
		for (auto& rope : a_domain.ropes) {
			if (!rope.used) {
				rope.used = true;
				return &rope;
			}
		}
		const auto limit = a_domain.diagnostic ? kMaxDebugShapes : kMaxRopes;
		if (a_domain.ropes.size() >= limit) {
			return nullptr;
		}
		auto* mesh = a_domain.diagnostic ? CollisionMesh() : (MeshesFor().failed ? nullptr : _meshes.rope.get());
		if (!mesh) {
			return nullptr;
		}
		auto* clone = mesh->Clone();
		auto* node = clone ? clone->AsNode() : nullptr;
		if (!node) {
			return nullptr;
		}
		Rope rope;
		rope.root.reset(node);
		for (std::size_t i = 0; i < kRopeBones; ++i) {
			rope.bones[i] = node->GetObjectByName(std::format("Rope{:02}", i));
			if (!rope.bones[i]) {
				return nullptr;
			}
		}
		node->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		if (auto* shape = node->GetObjectByName(MeshNames::Get().ropeShape)) {
			shape->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
		}
		node->local = RE::NiTransform{};
		a_domain.root->AttachChild(node, true);
		Shade(a_domain, node);
		rope.used = true;
		a_domain.ropes.push_back(std::move(rope));
		return &a_domain.ropes.back();
	}

	void FluidManager::PoseBlob(Domain& a_domain, Blob& a_blob)
	{
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			RE::NiTransform world;
			world.translate = ToWorld(a_domain, a_blob.surface[i].position);
			world.rotate = ArcRotation(_directions[i], a_blob.surface[i].normal);
			a_blob.bones[i]->local = world;
		}
		a_blob.root->SetAppCulled(false);
	}

	void FluidManager::PoseDrop(Domain& a_domain, Blob& a_blob, Vec3 a_center, float a_radius, Vec3 a_velocity, std::optional<Vec3> a_contact)
	{
		const float flying = a_contact ? 0.0f : std::max(Fluid::Length(a_velocity) - kStreakStart, 0.0f);
		const float stretch = 1.0f + std::min(flying * kStreak, kMaxStretch - 1.0f);
		const Vec3  along = Fluid::Normalize(a_velocity, { 0.0f, 0.0f, 1.0f });
		const float squash = a_contact ? kSquash : 1.0f;
		const Vec3  normal = a_contact ? *a_contact : Vec3{ 0.0f, 0.0f, 1.0f };
		const Vec3  center = a_contact ? a_center - normal * (a_radius * (1.0f - squash) * 0.7f) : a_center;
		for (std::size_t i = 0; i < a_blob.bones.size(); ++i) {
			const Vec3      direction = _directions[i];
			const Vec3      offset = Stretch(Stretch(direction, along, stretch), normal, squash);
			const Vec3      facing = Fluid::Normalize(Stretch(Stretch(direction, along, 1.0f / stretch), normal, 1.0f / squash), direction);
			RE::NiTransform world;
			world.translate = ToWorld(a_domain, center + offset * a_radius);
			world.rotate = ArcRotation(direction, facing);
			a_blob.bones[i]->local = world;
		}
		a_blob.live = false;
		a_blob.root->SetAppCulled(false);
	}

	void FluidManager::PoseRope(Domain& a_domain, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor)
	{
		const float                  fade = std::max(Settings::Get().fluidFade, 1e-3f);
		std::vector<Fluid::Particle> points(a_domain.particles.begin() + a_begin, a_domain.particles.begin() + a_end);
		if (a_anchor) {
			Fluid::Particle tip = points.back();
			tip.position = a_anchor->point;
			points.push_back(tip);
		}
		const std::size_t  count = points.size();
		std::vector<float> along(count, 0.0f);
		for (std::size_t k = 1; k < count; ++k) {
			along[k] = along[k - 1] + Fluid::Length(points[k].position - points[k - 1].position);
		}
		const float nominal = std::sqrt(kVolume / (std::numbers::pi_v<float> * kSlotSpacing * kSpacing));
		const float total = along.back();
		const float cap = nominal;
		const auto  gapAt = [&](std::size_t k) {
			const std::size_t lo = k > 0 ? k - 1 : 0;
			const std::size_t hi = std::min(k + 2, count - 1);
			return (along[hi] - along[lo]) / static_cast<float>(std::max<std::size_t>(hi - lo, 1));
		};

		std::array<RE::NiPoint3, kRopeBones> positions{};
		std::array<float, kRopeBones>        radii{};
		std::size_t                          segment = 0;
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			const float t = static_cast<float>(b) / static_cast<float>(kRopeBones - 1);
			const float s = -cap + (total + 2.0f * cap) * t;
			const float clamped = std::clamp(s, 0.0f, total);
			while (segment + 2 < count && along[segment + 1] < clamped) {
				++segment;
			}
			const auto& a = points[segment];
			const auto& c = points[std::min(segment + 1, count - 1)];
			const float length = along[std::min(segment + 1, count - 1)] - along[segment];
			const float u = length > 1e-6f ? std::clamp((clamped - along[segment]) / length, 0.0f, 1.0f) : 0.0f;
			Vec3        point = a.position + (c.position - a.position) * u;
			const Vec3  tangent = Fluid::Normalize(c.position - a.position, { 0.0f, 0.0f, 1.0f });
			if (s < 0.0f) {
				point = point + tangent * s;
			} else if (s > total) {
				point = point + tangent * (s - total);
			}
			const float gap = std::max(gapAt(segment), 0.3f * kSpacing);
			float       radius = std::clamp(std::sqrt(kVolume / (std::numbers::pi_v<float> * gap)), 0.2f * nominal, 1.6f * nominal);
			const float weight = std::clamp((a.life + (c.life - a.life) * u) / fade, 0.0f, 1.0f);
			radius *= std::sqrt(weight);
			if (b == 0 || (b + 1 == kRopeBones && !a_anchor)) {
				radius *= 0.55f;
			} else if (b == 1 || (b + 2 == kRopeBones && !a_anchor)) {
				radius *= 0.85f;
			}
			positions[b] = ToWorld(a_domain, point);
			radii[b] = radius;
		}
		for (auto& radius : radii) {
			radius *= kUnitsPerMeter;
		}
		Bend(a_rope, positions, radii);
	}

	void FluidManager::PoseCapsule(Rope& a_rope, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radius)
	{
		static_assert(kRopeBones >= 4 && kRopeBones % 2 == 0);
		constexpr std::size_t half = kRopeBones / 2;
		const auto axis = Normalized(a_to - a_from, { 0.0f, 1.0f, 0.0f });
		std::array<RE::NiPoint3, kRopeBones> positions{};
		std::array<float, kRopeBones>        radii{};
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			const bool first = b < half;
			const float t = static_cast<float>(b % half) / static_cast<float>(half - 1);
			const float angle = (t - (first ? 1.0f : 0.0f)) * std::numbers::pi_v<float> * 0.5f;
			positions[b] = (first ? a_from : a_to) + axis * (std::sin(angle) * a_radius);
			radii[b] = std::max(std::cos(angle), 0.0f) * a_radius;
		}
		Bend(a_rope, positions, radii);
	}

	void FluidManager::Bend(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii)
	{
		GeometryMath::UpdateFrames(a_rope.frames, a_positions, -1);
		for (std::size_t b = 0; b < kRopeBones; ++b) {
			RE::NiTransform world;
			world.rotate = Rotation(a_rope.frames[b]);
			world.translate = a_positions[b];
			world.scale = std::max(a_radii[b], 0.0005f);
			a_rope.bones[b]->local = world;
		}
		a_rope.root->SetAppCulled(false);
	}

	void FluidManager::Draw(Domain& a_domain)
	{
		const auto& settings = Settings::Get();
		auto&       meshes = MeshesFor();
		if (meshes.failed) {
			return;
		}
		auto&                     particles = a_domain.particles;
		const Fluid::DensityField field(particles, kKernel, settings.fluidFade);
		std::vector<std::size_t>  pooled;
		for (std::size_t i = 0; i < particles.size(); ++i) {
			if (!particles[i].asleep) {
				const float count = field.Count(particles[i].position);
				particles[i].bulk = particles[i].bulk ? count >= kBulkLeave : count >= kBulkEnter;
			}
			if (particles[i].bulk) {
				pooled.push_back(i);
			}
		}
		const auto clusters = Fluid::FindClusters(particles, pooled, 2.0f * kSpacing);
		std::vector<std::size_t> owner(particles.size(), Fluid::kNone);
		for (std::size_t c = 0; c < clusters.size(); ++c) {
			for (const auto member : clusters[c].members) {
				owner[member] = c;
			}
		}
		for (auto& blob : a_domain.blobs) {
			blob.used = false;
		}
		for (auto& rope : a_domain.ropes) {
			rope.used = false;
		}
		std::vector<std::vector<std::size_t>> hints(clusters.size());
		for (std::size_t b = 0; b < a_domain.blobs.size(); ++b) {
			auto& blob = a_domain.blobs[b];
			if (blob.live && blob.seed < particles.size() && owner[blob.seed] != Fluid::kNone) {
				hints[owner[blob.seed]].push_back(b);
				blob.used = true;
			} else {
				blob.live = false;
			}
		}

		const float blend = 1.0f - std::exp(-RE::GetSecondsSinceLastFrame() / kSmoothing);
		std::vector<std::size_t> claimed;
		struct Droplet
		{
			Vec3                center;
			float               radius;
			Vec3                velocity;
			std::optional<Vec3> contact;
		};
		std::vector<Droplet> droplets;
		const auto           contactOf = [&](std::span<const std::size_t> a_members, Vec3 a_center) -> std::optional<Vec3> {
			for (const auto m : a_members) {
				const auto& particle = particles[m];
				if (!particle.touching || particle.surface < 0) {
					continue;
				}
				const auto index = static_cast<std::size_t>(particle.surface);
				if (index < a_domain.capsules.size()) {
					const auto& capsule = a_domain.capsules[index];
					return Fluid::Normalize(particle.position - (capsule.a + (capsule.b - capsule.a) * particle.along), { 0.0f, 0.0f, 1.0f });
				}
				const auto& skin = a_domain.skin.Patches();
				if (index - a_domain.capsules.size() < skin.size()) {
					return skin[index - a_domain.capsules.size()].normal;
				}
			}
			if (a_center.z - a_domain.ground.Height(a_center.x, a_center.y) < kSpacing) {
				return Vec3{ 0.0f, 0.0f, 1.0f };
			}
			return std::nullopt;
		};
		const auto velocityOf = [&](std::span<const std::size_t> a_members) {
			Vec3 sum{};
			for (const auto m : a_members) {
				sum += particles[m].velocity;
			}
			return a_members.empty() ? sum : sum * (1.0f / static_cast<float>(a_members.size()));
		};
		const auto           weightOf = [&](std::span<const std::size_t> a_members) {
			float sum = 0.0f;
			for (const auto m : a_members) {
				sum += field.Weight(particles[m]);
			}
			return a_members.empty() ? 0.0f : sum / static_cast<float>(a_members.size());
		};
		for (std::size_t c = 0; c < clusters.size(); ++c) {
			const auto& cluster = clusters[c];
			if (cluster.members.size() < kBlobMinimum) {
				droplets.push_back({ cluster.center, Fluid::VolumeRadius(cluster.members.size(), kVolume) * std::cbrt(weightOf(cluster.members)),
					velocityOf(cluster.members), contactOf(cluster.members, cluster.center) });
				continue;
			}
			std::vector<std::size_t> seeds;
			for (const auto b : hints[c]) {
				seeds.push_back(a_domain.blobs[b].seed);
			}
			const bool settled = std::ranges::all_of(cluster.members, [&](std::size_t a_member) {
				return particles[a_member].asleep && particles[a_member].life > settings.fluidFade;
			});
			for (const auto& piece : Fluid::SplitCluster(particles, cluster, kPieceRadius, seeds)) {
				Blob* blob = piece.hint != Fluid::kNone ? &a_domain.blobs[hints[c][piece.hint]] : AcquireBlob(a_domain);
				if (!blob) {
					continue;
				}
				if (piece.hint != Fluid::kNone) {
					claimed.push_back(hints[c][piece.hint]);
				}
				const bool warm = blob->live && blob->surface.size() == _directions.size();
				if (settled && warm && piece.hint != Fluid::kNone) {
					blob->seed = piece.seed;
					continue;
				}
				const auto previous = warm ? std::span<const Fluid::SurfacePoint>(blob->surface) : std::span<const Fluid::SurfacePoint>{};
				if (!Fluid::WrapSurface(field, particles[piece.seed].position, piece.extent + kKernel, kThreshold, _directions, previous, _scratch)) {
					PoseDrop(a_domain, *blob, piece.center, Fluid::VolumeRadius(piece.count, kVolume));
					continue;
				}
				if (warm) {
					for (std::size_t i = 0; i < _scratch.size(); ++i) {
						auto&       current = blob->surface[i];
						const auto& target = _scratch[i];
						current.position = current.position + (target.position - current.position) * blend;
						current.normal = Fluid::Normalize(current.normal + (target.normal - current.normal) * blend, target.normal);
						current.distance = target.distance;
					}
				} else {
					blob->surface = _scratch;
				}
				blob->seed = piece.seed;
				blob->live = true;
				PoseBlob(a_domain, *blob);
			}
		}
		for (auto& hinted : hints) {
			for (const auto b : hinted) {
				if (std::ranges::find(claimed, b) == claimed.end()) {
					a_domain.blobs[b].used = false;
					a_domain.blobs[b].live = false;
				}
			}
		}

		std::size_t                                   ropes = 0;
		std::unordered_map<std::size_t, Fluid::Vec3> axes;
		for (const auto& run : Fluid::FindRuns(particles)) {
			const auto held = std::ranges::find_if(a_domain.anchors, [&](const Fluid::Anchor& a) { return a.particle + 1 == run.end; });
			const auto* anchor = held != a_domain.anchors.end() ? &*held : nullptr;
			if (settings.debugLog && !anchor && run.Size() >= 2 && run.Size() <= 4) {
				Watch(a_domain, run, axes);
			}
			if (run.Size() >= 2 || anchor) {
				if (Rope* rope = AcquireRope(a_domain)) {
					PoseRope(a_domain, *rope, run.begin, run.end, anchor);
					++ropes;
				}
				continue;
			}
			const auto& single = particles[run.begin];
			if (field.BulkCount(single.position) > kCovered) {
				continue;
			}
			const std::size_t index = run.begin;
			droplets.push_back({ single.position, Fluid::VolumeRadius(1, kVolume) * std::cbrt(field.Weight(single)), single.velocity,
				contactOf({ &index, 1 }, single.position) });
		}
		for (const auto& anchor : a_domain.anchors) {
			if (anchor.particle < particles.size() && particles[anchor.particle].bulk) {
				if (Rope* rope = AcquireRope(a_domain)) {
					PoseRope(a_domain, *rope, anchor.particle, anchor.particle + 1, &anchor);
					++ropes;
				}
			}
		}
		for (const auto& droplet : droplets) {
			if (droplet.radius <= 0.0f) {
				continue;
			}
			if (Blob* blob = AcquireBlob(a_domain)) {
				PoseDrop(a_domain, *blob, droplet.center, droplet.radius, droplet.velocity, droplet.contact);
			}
		}

		std::size_t blobs = 0;
		for (auto& blob : a_domain.blobs) {
			if (blob.used) {
				++blobs;
			} else {
				blob.live = false;
				blob.root->SetAppCulled(true);
			}
		}
		for (auto& rope : a_domain.ropes) {
			if (!rope.used) {
				rope.root->SetAppCulled(true);
			}
		}
		a_domain.axes = std::move(axes);
		Reshade(a_domain);
		RE::NiUpdateData update{};
		a_domain.root->Update(update);
		a_domain.shownBlobs = blobs;
		a_domain.shownRopes = ropes;
		a_domain.pooled = pooled.size();
	}

	void FluidManager::Watch(Domain& a_domain, const Fluid::Run& a_run, std::unordered_map<std::size_t, Fluid::Vec3>& a_axes)
	{
		const auto&       particles = a_domain.particles;
		const Vec3        axis = Fluid::Normalize(particles[a_run.end - 1].position - particles[a_run.begin].position, { 1.0f, 0.0f, 0.0f });
		const std::size_t key = a_run.begin * 8 + a_run.Size();
		a_axes[key] = axis;
		const auto last = a_domain.axes.find(key);
		if (last == a_domain.axes.end()) {
			return;
		}
		const bool resting = std::all_of(particles.begin() + static_cast<std::ptrdiff_t>(a_run.begin), particles.begin() + static_cast<std::ptrdiff_t>(a_run.end),
			[&](const Fluid::Particle& a_particle) {
				return a_particle.touching || a_particle.position.z - a_domain.ground.Height(a_particle.position.x, a_particle.position.y) < kSpacing;
			});
		if (!resting) {
			return;
		}
		const float turn = std::acos(std::clamp(Fluid::Dot(axis, last->second), -1.0f, 1.0f)) / std::max(RE::GetSecondsSinceLastFrame(), 1e-3f);
		if (turn < kSpinning) {
			return;
		}
		++a_domain.spinning;
		if (!a_domain.spinSample.empty()) {
			return;
		}
		a_domain.spinSample = std::format("{} linked turning {:.0f} deg/s:", a_run.Size(), turn * 180.0f / std::numbers::pi_v<float>);
		for (std::size_t i = a_run.begin; i < a_run.end; ++i) {
			const auto&       particle = particles[i];
			const auto&       ground = a_domain.ground;
			const bool        known = ground.Known(ground.Node(particle.position.x), ground.Node(particle.position.y));
			const std::size_t surface = particle.surface < 0 ? 0 : static_cast<std::size_t>(particle.surface);
			const char*       on = !particle.touching ? "free" : surface < a_domain.capsules.size() ? "capsule" : "skin";
			a_domain.spinSample += std::format(" [{}{}, {:.1f} cm above {} floor, speed {:.2f} m/s, link {:.1f}]", on, particle.asleep ? " asleep" : "",
				(particle.position.z - ground.Height(particle.position.x, particle.position.y)) * 100.0f, known ? "measured" : "unmeasured",
				Fluid::Length(particle.velocity), particle.link / kSpacing);
		}
	}

	void FluidManager::Reshade(Domain& a_domain)
	{
		const auto& settings = Settings::Get();
		if (a_domain.opacity == settings.semenOpacity && a_domain.shine == settings.shine && a_domain.gloss == settings.gloss) {
			return;
		}
		for (auto& blob : a_domain.blobs) {
			Shade(a_domain, blob.root.get());
		}
		for (auto& rope : a_domain.ropes) {
			Shade(a_domain, rope.root.get());
		}
		a_domain.opacity = settings.semenOpacity;
		a_domain.shine = settings.shine;
		a_domain.gloss = settings.gloss;
	}

	void FluidManager::ShowShapes()
	{
		std::vector<RE::ActorHandle> actors;
		if (Settings::Get().showShapes) {
			const auto selected = RE::Console::GetSelectedRef();
			auto* actor = selected ? selected->As<RE::Actor>() : nullptr;
			if (actor && actor->Get3D(false)) {
				actors.push_back(actor->GetHandle());
			} else {
				for (const auto& [thread, members] : _scenes) {
					for (const auto& handle : members) {
						const auto member = handle.get();
						if (member && member->Get3D(false) && std::ranges::find(actors, handle) == actors.end()) {
							actors.push_back(handle);
						}
					}
				}
				if (actors.empty()) {
					if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->Get3D(false)) {
						actors.push_back(player->GetHandle());
					}
				}
			}
		}
		if (actors.empty() || !CollisionMesh()) {
			HideShapes();
			return;
		}
		if (!_shapes) {
			_shapes = std::make_unique<Domain>();
			_shapes->diagnostic = true;
			_shapes->ropes.reserve(kMaxDebugShapes);
		}
		auto& domain = *_shapes;
		domain.hosts = actors;
		if (!Attach(domain)) {
			HideShapes();
			return;
		}
		domain.root->name = "OStrandsCollisionDebug";
		for (auto& rope : domain.ropes) {
			rope.used = false;
		}
		std::size_t missing = 0;
		const auto tube = [&](const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radius) {
			if (Rope* rope = AcquireRope(domain)) {
				PoseCapsule(*rope, a_from, a_to, a_radius);
			} else {
				++missing;
			}
		};
		const auto sphere = [&](const RE::NiPoint3& a_center, float a_radius) { tube(a_center, a_center, a_radius); };
		std::vector<Point> rim;
		RE::NiPoint3       center;
		RE::NiPoint3       inward;
		for (const auto& handle : actors) {
			const auto actor = handle.get();
			auto*      root = actor ? actor->Get3D(false) : nullptr;
			if (!root) {
				continue;
			}
			if (const auto surface = Body::SurfaceOf(actor.get()); !surface.patches.empty()) {
				const std::size_t stride = std::max<std::size_t>(1, surface.patches.size() / kWrapDots);
				for (std::size_t k = 0; k < surface.patches.size(); k += stride) {
					sphere(surface.patches[k].position, kMarkerRadius);
				}
				for (const auto& capsule : surface.extras) {
					tube(capsule.a, capsule.b, capsule.radius);
				}
			} else {
				for (const auto& capsule : Body::Of(actor.get())) {
					tube(capsule.a, capsule.b, capsule.radius);
				}
				for (const auto& capsule : Body::ShaftOf(actor.get())) {
					tube(capsule.a, capsule.b, capsule.radius);
				}
			}
			if (HasSchlong(root)) {
				if (const auto tip = Body::TipOf(actor.get())) {
					sphere(tip->point, kMarkerRadius);
					tube(tip->point, tip->point + tip->axis * kPointerLength, kMarkerRadius * 0.3f);
				}
			}
			for (const auto opening : { Opening::kVagina, Opening::kAnus, Opening::kMouth }) {
				if (!FindOpening(root, opening, rim, center, inward)) {
					sphere(center - inward * kExitDepth, kMarkerRadius);
				}
			}
		}
		for (auto& rope : domain.ropes) {
			if (!rope.used) {
				rope.root->SetAppCulled(true);
			}
		}
		if (missing && !domain.incompleteReported) {
			logger::warn("collision display incomplete: {} shapes could not be drawn (limit {}); select one actor in the console", missing, kMaxDebugShapes);
		}
		domain.incompleteReported = missing != 0;
		RE::NiUpdateData update{};
		domain.root->Update(update);
	}

	void FluidManager::HideShapes()
	{
		if (_shapes) {
			Release(*_shapes);
			_shapes.reset();
		}
	}

	void FluidManager::Release(Domain& a_domain)
	{
		if (a_domain.root && a_domain.root->parent) {
			a_domain.root->parent->DetachChild2(a_domain.root.get());
		}
		a_domain.blobs.clear();
		a_domain.ropes.clear();
		a_domain.root.reset();
		a_domain.particles.clear();
	}

	Vec3 FluidManager::ToLocal(const Domain& a_domain, const RE::NiPoint3& a_point) const
	{
		return ToVec((a_point - a_domain.origin) / kUnitsPerMeter);
	}

	RE::NiPoint3 FluidManager::ToWorld(const Domain& a_domain, Vec3 a_point) const
	{
		return a_domain.origin + ToPoint(a_point) * kUnitsPerMeter;
	}

	void FluidManager::Update(float a_delta)
	{
		auto        settingsGuard = Settings::Lock();
		const auto& settings = Settings::Get();
		std::vector<Event> events;
		{
			std::scoped_lock guard(_lock);
			events.swap(_pending);
		}
		if (!settings.enabled) {
			if (!_domains.empty() || !_emitters.empty()) {
				Reset();
			}
			HideShapes();
			return;
		}
		for (auto& event : events) {
			Apply(event);
		}
		if (a_delta <= 0.0f) {
			return;
		}
		a_delta = std::min(a_delta, 0.05f);
		ShowShapes();
		for (auto it = _loads.begin(); it != _loads.end();) {
			it->second.second += a_delta;
			it = it->second.second > kLoadMemory ? _loads.erase(it) : std::next(it);
		}
		if (_emitters.empty() && _domains.empty()) {
			return;
		}
		const auto start = std::chrono::steady_clock::now();
		for (auto& domain : _domains) {
			try {
				Simulate(*domain, a_delta);
				Expire(*domain, a_delta);
			} catch (const std::exception& error) {
				logger::error("semen pool {} reset: {}", domain->id, error.what());
				domain->particles.clear();
				domain->anchors.clear();
			}
		}
		std::erase_if(_emitters, [&](Emitter& a_emitter) { return !Step(a_emitter, a_delta); });
		std::erase_if(_reservoirs, [&](Reservoir& a_reservoir) { return !Leak(a_reservoir, a_delta); });
		for (auto& domain : _domains) {
			std::erase_if(domain->anchors, [&](const Fluid::Anchor& a_anchor) {
				return std::ranges::none_of(_emitters, [&](const Emitter& e) { return e.id == a_anchor.owner; }) &&
				       std::ranges::none_of(_reservoirs, [&](const Reservoir& r) { return r.id == a_anchor.owner; });
			});
		}
		const auto middle = std::chrono::steady_clock::now();
		std::erase_if(_domains, [&](const std::unique_ptr<Domain>& a_domain) {
			const bool fed = std::ranges::any_of(_emitters, [&](const Emitter& e) { return e.domain == a_domain->id; }) ||
			                 std::ranges::any_of(_reservoirs, [&](const Reservoir& r) { return r.domain == a_domain->id; });
			if ((a_domain->particles.empty() && !fed) || !Attach(*a_domain)) {
				if (settings.debugLog) {
					logger::info("semen pool {} closed after {:.0f}s", a_domain->id, a_domain->age);
				}
				Release(*a_domain);
				return true;
			}
			return false;
		});
		for (auto& domain : _domains) {
			Draw(*domain);
		}

		if (settings.debugLog) {
			const auto end = std::chrono::steady_clock::now();
			_statSimulate += std::chrono::duration<double, std::micro>(middle - start).count();
			_statDraw += std::chrono::duration<double, std::micro>(end - middle).count();
			++_statFrames;
			_statTime += a_delta;
			if (_statTime >= 10.0f) {
				for (const auto& domain : _domains) {
					logger::info("semen pool {}: {} particles, {} pooled, {} blobs, {} ropes, {} spinning resting pills", domain->id,
						domain->particles.size(), domain->pooled, domain->shownBlobs, domain->shownRopes, domain->spinning);
					if (!domain->spinSample.empty()) {
						logger::info("semen pool {} spin sample: {}", domain->id, domain->spinSample);
					}
					domain->spinning = 0;
					domain->spinSample.clear();
				}
				logger::info("fluid cost: simulate {:.0f} us, draw {:.0f} us per frame; {} emitter(s)", _statSimulate / std::max(_statFrames, 1),
					_statDraw / std::max(_statFrames, 1), _emitters.size());
				_statTime = 0.0f;
				_statSimulate = 0.0;
				_statDraw = 0.0;
				_statFrames = 0;
			}
		}
	}
}
