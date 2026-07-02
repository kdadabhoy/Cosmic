// SimHub.cpp — see header.

#include "SimHub.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Viper
{
	SimHub::SimHub()
	{
		// Config load is deferred to the root layer's OnAttach: the active
		// project (FileSystem::SetActiveProject) isn't set at construction, and
		// "project://" must resolve against it. Built-in defaults hold until then.
	}

	bool SimHub::LoadConfig(const std::string& path)
	{
		// Resolve the VFS path HERE (client side): FileSystem is a header-only
		// class with per-DLL static state, so the engine's Config::Load would
		// otherwise resolve "project://" against the engine's (unset) active
		// project. Resolving in the project DLL uses the name we set in
		// ViperSim::OnAttach; the already-resolved path passes through the
		// engine's Resolve unchanged.
		const std::string resolved = Cosmic::FileSystem::Resolve(path);
		m_Config = Cosmic::Config::Load(resolved);

		// Pull airframe params from viper.toml; every getter has a design-point
		// default so a missing file still yields a flyable body (plan §2.2).
		BodyParams p;
		if (m_Config)
		{
			p.mass_kg      = m_Config->Get<float>("airframe.auw_kg", p.mass_kg);
			p.inertia      = m_Config->Get<glm::vec3>("airframe.inertia_diag", p.inertia);
			p.body_radius  = m_Config->Get<float>("airframe.body_radius_m", p.body_radius);
			p.ground_agl_m = m_Config->Get<float>("sim.ground_agl_m", p.ground_agl_m);
			p.ground_k     = m_Config->Get<float>("sim.ground_k", p.ground_k);
			p.ground_c     = m_Config->Get<float>("sim.ground_c", p.ground_c);
			p.substeps     = m_Config->Get<int>("sim.substeps", p.substeps);
		}
		m_Params = p;

		if (!m_Dynamics)
			m_Dynamics = std::make_unique<ComposableDynamics>(p);
		else
			m_Dynamics->SetParams(p);

		CS_INFO("ViperSim: config loaded (mass {:.2f} kg, substeps {}).", p.mass_kg, p.substeps);
		return true;
	}

	void SimHub::RegisterEntities()
	{
		if (m_EntitiesRegistered)
			return;

		// truth = ground-truth state; fc = flight-computer internals (reserved).
		m_TruthId = m_Recorder.Register("truth", "sim", TruthChannels());
		m_FcId    = m_Recorder.Register("fc",    "estimate", FcChannels());
		m_EntitiesRegistered = true;
	}

	void SimHub::StartDrop(float dropHeightM)
	{
		RegisterEntities();
		m_Recorder.Clear();

		m_DropHeight = std::max(dropHeightM, 0.1f);

		// Reset truth: dropHeight above ground, at rest, wings level.
		RigidState init;
		init.posNed = { 0.0f, 0.0f, -(m_Params.ground_agl_m + m_DropHeight) };
		init.velNed = { 0.0f, 0.0f, 0.0f };
		init.attNed = Cosmic::Math::QuatFromEulerZYX({ 0.0f, 0.0f, 0.0f });
		m_Dynamics->Reset(init);

		m_Running     = true;
		m_RunTime     = 0.0f;
		m_SettleTimer = 0.0f;
		m_Mode        = FlightMode::Idle;

		// ~15 s of headroom at the engine fixed rate; drop settles well before.
		m_Recorder.ReserveCapacity(static_cast<size_t>(15.0f * 60.0f));

		CS_INFO("ViperSim: drop started from {:.1f} m.", m_DropHeight);
	}

	void SimHub::ResetDrop()
	{
		StartDrop(m_DropHeight);
	}

	void SimHub::RecordTruth()
	{
		const RigidState& s = m_Dynamics->GetTruth();
		const glm::vec3 e = Cosmic::Math::EulerZYXFromQuat(s.attNed);

		m_Recorder.Record(m_TruthId, {
			s.posNed.x, s.posNed.y, s.posNed.z,
			s.velNed.x, s.velNed.y, s.velNed.z,
			e.x, e.y, e.z,
			s.airspeed, glm::degrees(s.alpha), AltitudeAgl(),
		});

		// fc entity: zeros until viper-fc exists (P2). Registered now so replay
		// tooling and column layout are stable from day one.
		m_Recorder.Record(m_FcId, {
			static_cast<float>(m_Mode), 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f,
		});
	}

	void SimHub::Step(float dt)
	{
		if (!m_Running || dt <= 0.0f)
			return;

		// Drop test: motors off (idle actuator command).
		ActuatorFrame idle;
		m_Dynamics->Step(idle, dt);

		m_RunTime += dt;
		m_Recorder.Tick(dt);
		RecordTruth();

		// Auto-stop once the body has settled (near ground, near rest) for 1.5 s.
		// AltitudeAgl() is the CG's height — at rest the CG sits ~body_radius
		// above the ground (on the contact spring), so gate on the LOWEST
		// point's clearance or the settle condition can never trigger.
		const float speed = glm::length(m_Dynamics->GetTruth().velNed);
		const float lowestAgl = AltitudeAgl() - m_Params.body_radius;
		const bool settled = lowestAgl < 0.05f && speed < 0.05f;
		m_SettleTimer = settled ? (m_SettleTimer + dt) : 0.0f;

		if (m_SettleTimer > 1.5f || m_RunTime > 14.0f)
		{
			m_Running = false;
			CS_INFO("ViperSim: drop settled after {:.2f} s.", m_RunTime);
		}
	}

	std::string SimHub::FlushRecording()
	{
		if (!m_EntitiesRegistered)
			return "";

		const std::string folder = Cosmic::FileSystem::Resolve("user://recordings/viper_drop");
		m_Recorder.Flush(folder, "session", 60.0f);
		m_Recorder.WaitForFlush();
		m_LastRecordingPath = folder + "/session";
		CS_INFO("ViperSim: recording flushed to '{}'.", m_LastRecordingPath);
		return m_LastRecordingPath;
	}
}
