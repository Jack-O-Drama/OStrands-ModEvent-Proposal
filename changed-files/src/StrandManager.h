#pragma once

#include "Anatomy.h"
#include "AmbientLease.h"
#include "Body.h"
#include "GeometryMath.h"

namespace OStrands
{
	class StrandManager
	{
	public:
		static StrandManager& Get();

		void SetPairs(std::int32_t a_thread, std::vector<Pair> a_pairs);
		void ClearThread(std::int32_t a_thread);
		void Recalibrate();

		void RequestAmbient(float interval);
		void Reset();
		void Update(float a_delta, float a_realDelta);

		static constexpr std::size_t kBones = 10;

		using Point = Anatomy::Point;

	private:
		enum class Kind
		{
			kBridge,
			kContact,
			kDrip
		};

		enum class Source
		{
			kReceiver,
			kGiver
		};

		struct Anchors
		{
			RE::NiAVObject*              receiverRoot{ nullptr };
			RE::NiAVObject*              giverRoot{ nullptr };
			std::vector<Point>           opening;
			std::vector<Point>           vulva;
			RE::NiPoint3                 center;
			RE::NiPoint3                 inward;
			std::vector<RE::NiAVObject*> chain;
			RE::NiAVObject*              toy{ nullptr };
			float                        toyRadius{ 0.0f };
			RE::NiPoint3                 tip;
			float                        chainScale{ 1.0f };
			std::vector<RE::NiAVObject*> crotch;
			std::vector<Point>           giverSite;
			RE::NiPoint3                 giverCenter;

			float Depth(const RE::NiPoint3& a_point) const { return (a_point - center).Dot(inward); }
		};

		enum class Crossing
		{
			kNone,
			kArrived,
			kLeft
		};

		struct Bands
		{
			float nearFrac;
			float nearMax;
			float gapFrac;
			float gapMax;
		};

		struct Swing
		{
			float low{ 0.0f };
			float high{ 0.0f };
			bool  primed{ false };
			bool  engaged{ false };

			float    Range() const { return high - low; }
			Crossing Step(float a_value, float a_delta, const Bands& a_bands, bool a_armed);
		};

		struct PairState
		{
			Pair    pair;
			Anchors anchors;
			bool    resolved{ false };
			bool    reported{ false };
			bool    checked{ false };
			bool    strapWearer{ false };
			float   shaftRadius{ 0.0f };
			Swing   stroke;
			Swing   contact;
			bool    bridgePending{ false };
			bool    contactPending{ false };
			float   settle{ 0.0f };
			float   bridgeBudget{ 0.0f };
			float   contactBudget{ 0.0f };
			float   cooldown{ 0.0f };
			float   receiverDrip{ -1.0f };
			float   giverDrip{ -1.0f };
		};

		struct End
		{
			RE::NiPointer<RE::NiAVObject> node;
			RE::NiPoint3                  local;

			RE::NiPoint3 World() const { return node->world * local; }
		};

		struct Strand
		{
			Kind                                kind{ Kind::kBridge };
			Source                              source{ Source::kReceiver };
			RE::NiPointer<RE::NiNode>           root;
			std::array<RE::NiAVObject*, kBones> bones{};
			RE::ActorHandle                     owner;
			RE::NiAVObject*                     attachedTo{ nullptr };
			Pair                                pair;
			End                                 top;
			End                                 bottom;
			std::array<RE::NiPoint3, kBones>    pos{};
			std::array<RE::NiPoint3, kBones>    prev{};
			std::array<GeometryMath::Frame, kBones> frames{};
			float                               previousStep{ 0.0f };
			std::array<float, kBones - 1>       rest{};
			float                               volume{ 0.0f };
			float                               startLength{ 0.0f };
			RE::NiPointer<RE::BSShaderProperty> shader;
			float                               opacity{ -1.0f };
			float                               shine{ -1.0f };
			float                               gloss{ -1.0f };
			float                               radius{ 0.0f };
			float                               age{ 0.0f };
			float                               fade{ 1.0f };
			float                               fadeRate{ 0.0f };
			int                                 breakAt{ -1 };
			std::uint32_t                       serial{ 0 };
			bool                                active{ false };
			bool                                external{ false };
		};

		struct Glint
		{
			RE::NiPointer<RE::NiNode>           root;
			RE::NiAVObject*                     bone{ nullptr };
			RE::NiPointer<RE::BSShaderProperty> shader;
			RE::NiAVObject*                     attachedTo{ nullptr };
			std::size_t                         strand{ 0 };
			std::uint32_t                       serial{ 0 };
			int                                 segment{ 0 };
			float                               along{ 0.0f };
			float                               age{ 0.0f };
			float                               life{ 0.0f };
			float                               size{ 0.0f };
			float                               angle{ 0.0f };
			float                               spin{ 0.0f };
			float                               brightness{ -1.0f };
			bool                                active{ false };
		};

		struct Linger
		{
			Pair            pair;
			Source          source{ Source::kReceiver };
			RE::ActorHandle actor;
			float           remaining{ 0.0f };
			float           timer{ 0.0f };
		};

		struct SurfaceSettings
		{
			float rate;
			int   maxPerContact;
			int   maxPerPair;
			float maxGap;
			float spread;
		};

		bool        EnsureMesh();
		Strand*     AcquireStrand();
		void        ApplyPendingPairs();
		const char* Resolve(const Pair& a_pair, Anchors& a_out);
		void        Check(PairState& a_state);
		void        TrackStroke(PairState& a_state, float a_delta);
		bool        TrackContact(PairState& a_state, float a_delta, const RE::NiPoint3& a_point, float a_rate, float a_maxGap);
		void        TrackCrotch(PairState& a_state, float a_delta);
		void        TrackSurface(PairState& a_state, float a_delta, const SurfaceSettings& a_surface);
		void        TickDrips(PairState& a_state, float a_delta);
		bool        SpawnBridges(PairState& a_state);
		void        SpawnCrotch(PairState& a_state);
		void        SpawnSurface(PairState& a_state, const SurfaceSettings& a_surface);
		void        SpawnDrip(PairState& a_state, Source a_source);
		void        SpawnDripAt(const Pair& a_pair, Source a_source, RE::NiAVObject* a_root, const std::vector<Point>& a_points, bool a_external = false);
		void        BeginLinger(const PairState& a_state);
		void        TickLinger(float a_delta);
		void        TickAmbient(float a_delta);
		void        ClearAmbientStrands();
		void        StartBetween(Strand& a_strand, Kind a_kind, const PairState& a_state, End a_top, End a_bottom, float a_radius);
		void        Attach(Strand& a_strand, RE::NiAVObject* a_parent);
		void        Snap(Strand& a_strand, int a_segment, float a_fadeTime);
		void        Simulate(Strand& a_strand, float a_delta, std::span<const Body::Capsule> a_bodies, std::span<const Fluid::Skin* const> a_skins);
		void        Pose(Strand& a_strand);
		void        Retire(Strand& a_strand);
		int         Count(const Pair& a_pair, Kind a_kind, std::optional<Source> a_source = std::nullopt) const;
		int         CountActive() const;
		bool        EnsureGlintMesh();
		Glint*      AcquireGlint();
		void        SpawnGlints(float a_delta);
		void        UpdateGlints(float a_delta);
		void        RetireGlint(Glint& a_glint);

		std::mutex                                _lock;
		std::map<std::int32_t, std::vector<Pair>> _threads;
		bool                                      _dirty{ false };

		AmbientLease _ambient;
		bool _ambientEligible{ false };
		std::vector<PairState>    _pairs;
		std::vector<Linger>       _lingering;
		std::vector<Strand>       _strands;
		RE::NiPointer<RE::NiNode> _prototype;
		bool                      _meshFailed{ false };
		std::vector<Glint>        _glints;
		std::vector<Body::Capsule> _bodies;
		std::vector<const Fluid::Skin*> _skins;
		RE::NiPointer<RE::NiNode> _glintPrototype;
		bool                      _glintMeshFailed{ false };
		std::uint32_t             _serial{ 0 };
		std::mt19937              _rng{ std::random_device{}() };

		float  _statTime{ 0.0f };
		double _statMicros{ 0.0 };
		int    _statFrames{ 0 };
		int    _statBridges{ 0 };
		int    _statContacts{ 0 };
		int    _statDrips{ 0 };
		float  _statClosest{ std::numeric_limits<float>::max() };
		float  _statStroke{ 0.0f };
		float  _statSwing{ 0.0f };
	};
}
