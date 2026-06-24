#pragma once

// DrivetrainModel.h
//
// ============================================================================
// Shear Force drivetrain simulation — pure, UI-free physics core.
// ============================================================================
//
// This header is a faithful re-implementation of the original command-line
// drivetrain calculator (main.cpp / DrivetrainInputeTemplate.txt). The exact
// same equations are used so results match number-for-number; the app layer
// only adds an ImGui front-end, live ImPlot curves and CSV export on top.
//
// The model is a 1-D point-mass spin-up:
//   - A KISS-style brushed/brushless motor torque-speed line gives the motor
//     torque available at the current shaft speed.
//   - That torque is geared to the contact patch through the gearbox + pulley
//     reduction, and capped by the available traction (mu * weight).
//   - The reflected inertia is integrated forward in time to produce the
//     speed / acceleration / force / torque curves.
//
// A "drivetrain" here has a FRONT and a REAR axle (possibly different wheel
// sizes / pulley counts). For a belt-coupled robot both axles must produce the
// same linear speed or they fight each other — Simulate() reports that sync
// check. The FRONT axle is the primary simulation (matching the original);
// the REAR axle is simulated too so the two can be overlaid for tuning.
// ============================================================================

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace Workspace
{
    // -----------------------------------------------------------------------
    // Physical constants (shared with the original tool).
    // -----------------------------------------------------------------------
    namespace DT
    {
        constexpr double INCHES_TO_METERS = 0.0254;
        constexpr double LB_TO_KG         = 0.45359237;
        constexpr double PI               = 3.14159265358979323846;
        constexpr double G                = 9.80665;
        constexpr double MPS_TO_MPH       = 2.237;
        constexpr double M_TO_FT          = 3.28084;
    }

    // -----------------------------------------------------------------------
    // All user-editable inputs (mirrors DrivetrainInputeTemplate.txt).
    // -----------------------------------------------------------------------
    struct DrivetrainConfig
    {
        // Simulation
        double simTime = 5.0;     // s
        double dt      = 0.02;    // s

        // Reduction / geometry
        double gearboxReduction = 19.0;

        double frontWheelInches = 2.5;
        double frontPulleyTeeth = 20.0;   // driven (at the wheel)
        double motorPulleyFront = 30.0;   // driving (at the motor)

        double rearWheelInches  = 3.5;
        double rearPulleyTeeth  = 28.0;
        double motorPulleyRear  = 30.0;

        // Chassis geometry (does NOT affect the sim) — the rear axle mount sits
        // this much higher than the front mount on the chassis. The robot is
        // level when this offset equals (rearRadius - frontRadius); the default
        // 0.5 in makes the stock 2.5 in / 3.5 in combo sit level.
        double mountOffsetIn = 0.5;

        // Efficiency
        double speedFactor           = 0.85;
        double drivetrainEfficiency  = 0.80;

        // Battery / motor
        double kv           = 1400.0;  // rpm / Volt
        double currentLimit = 80.0;    // A
        double vBatt        = 22.2;    // V

        // Constants
        double mu           = 1.0;     // coefficient of friction
        double totalWeightLb = 30.0;   // lb (whole robot; split per side)
    };

    enum DriveStatus : int { STATUS_MOTOR = 0, STATUS_LIMIT = 1 };

    // -----------------------------------------------------------------------
    // Result of simulating a single axle. Parallel column vectors hold the
    // time series; the scalars are derived headline numbers.
    // -----------------------------------------------------------------------
    struct AxleResult
    {
        // Time series (one entry per simulated step)
        std::vector<double> time;       // s
        std::vector<double> speedMph;   // mph
        std::vector<double> accelG;     // g
        std::vector<double> forceN;     // N (tractive force at contact patch)
        std::vector<double> torqueNm;   // Nm (motor torque)
        std::vector<double> distFt;     // ft (integrated distance travelled)
        std::vector<int>    status;     // DriveStatus per step

        // Derived constants
        double K_sys          = 0.0;  // m travelled per motor-radian
        double omegaNoLoad    = 0.0;  // rad/s
        double torqueStall    = 0.0;  // Nm
        double tractionLimitN = 0.0;  // N
        double inertiaEq      = 0.0;  // kg·m² reflected to motor

        // Headline metrics
        double noLoadTopSpeedMph = 0.0; // theoretical (omega_no_load geared out)
        double topSpeedMph       = 0.0; // reached within simTime
        double peakAccelG        = 0.0;
        double launchForceN      = 0.0;
        double distanceFt        = 0.0; // total over the run
        bool   launchTractionLimited = false;
        double timeToReleaseTraction = -1.0; // s when status LIMIT->MOTOR (-1 = n/a)
    };

    struct SimOutput
    {
        AxleResult front;
        AxleResult rear;

        // Velocity sync check (belt-coupled axles must match).
        double kFront      = 0.0;
        double kRear       = 0.0;
        bool   velMismatch = false;

        // Tangential (ground) wheel-surface speed feasibility. For a rigid
        // chassis both driven wheels must roll at the same ground speed, so
        // the two axles' tangential velocities must match or one wheel has to
        // slip — i.e. the build is mechanically impossible.
        double vTangFrontMph  = 0.0; // front wheel surface speed (no-load ref)
        double vTangRearMph   = 0.0; // rear  wheel surface speed (no-load ref)
        double tangMismatchPct = 0.0; // |vF - vR| / max(vF,vR) * 100

        bool        valid = true;
        std::string error;
    };

    // -----------------------------------------------------------------------
    // Derived per-axle constants (single source of truth shared by the full
    // time-series sim and the lightweight metrics-only sweep sim).
    // -----------------------------------------------------------------------
    struct AxleConstants
    {
        double massSide        = 0.0;
        double weightSide      = 0.0;
        double r               = 0.0;  // wheel radius (m)
        double R               = 0.0;  // pulley ratio (driven / driving)
        double K_sys           = 0.0;  // m travelled per motor-radian
        double kt              = 0.0;
        double omegaNoLoad     = 0.0;
        double torqueStall     = 0.0;
        double tractionTorque  = 0.0;  // mu*W*K_sys (torque cap, original)
        double tractionForceN  = 0.0;  // mu*W       (force at the patch)
        double inertiaEq       = 0.0;
        double noLoadTopSpeedMph = 0.0;
    };

    inline AxleConstants ComputeAxleConstants(const DrivetrainConfig& cfg,
                                              double wheelInches,
                                              double pulleyTeeth,
                                              double motorPulley)
    {
        AxleConstants a;
        a.massSide       = (cfg.totalWeightLb / 2.0) * DT::LB_TO_KG;
        a.weightSide     = a.massSide * DT::G;
        a.r              = (wheelInches / 2.0) * DT::INCHES_TO_METERS;
        a.R              = (motorPulley != 0.0) ? pulleyTeeth / motorPulley : 0.0;
        a.K_sys          = (cfg.gearboxReduction != 0.0 && a.R != 0.0)
                           ? a.r / (cfg.gearboxReduction * a.R) : 0.0;
        a.kt             = (cfg.kv != 0.0) ? 60.0 / (2.0 * DT::PI * cfg.kv) : 0.0;
        a.omegaNoLoad    = (cfg.kv * cfg.vBatt * cfg.speedFactor) * (2.0 * DT::PI / 60.0);
        a.torqueStall    = cfg.currentLimit * a.kt * cfg.drivetrainEfficiency;
        a.tractionTorque = cfg.mu * a.weightSide * a.K_sys;
        a.tractionForceN = cfg.mu * a.weightSide;
        a.inertiaEq      = a.massSide * std::pow(a.K_sys, 2);
        a.noLoadTopSpeedMph = a.omegaNoLoad * a.K_sys * DT::MPS_TO_MPH;
        return a;
    }

    // -----------------------------------------------------------------------
    // Simulate one axle. wheelInches/pulleyTeeth/motorPulley select the axle;
    // every other parameter is shared (motor + battery + mass).
    // -----------------------------------------------------------------------
    inline AxleResult SimulateAxle(const DrivetrainConfig& cfg,
                                   double wheelInches,
                                   double pulleyTeeth,
                                   double motorPulley)
    {
        AxleResult out;

        // --- Derived physical constants (identical to the original tool) ---
        const AxleConstants ac = ComputeAxleConstants(cfg, wheelInches, pulleyTeeth, motorPulley);
        const double K_sys          = ac.K_sys;
        const double omega_no_load  = ac.omegaNoLoad;
        const double torque_stall   = ac.torqueStall;
        const double Traction_Limit = ac.tractionTorque;
        const double Inertia_eq     = ac.inertiaEq;

        out.K_sys          = ac.K_sys;
        out.omegaNoLoad    = ac.omegaNoLoad;
        out.torqueStall    = ac.torqueStall;
        out.inertiaEq      = ac.inertiaEq;
        out.noLoadTopSpeedMph = ac.noLoadTopSpeedMph;
        out.tractionLimitN = ac.tractionForceN;

        // --- Integrate the spin-up ---
        const double dt   = (cfg.dt > 0.0) ? cfg.dt : 0.02;
        const double tEnd = std::max(cfg.simTime, 0.0);

        // Pre-reserve to avoid reallocation churn (cap to keep huge dt-small runs sane).
        const size_t approxSteps = static_cast<size_t>(tEnd / dt) + 2;
        const size_t reserveN    = std::min<size_t>(approxSteps, 2'000'000);
        out.time.reserve(reserveN);   out.speedMph.reserve(reserveN);
        out.accelG.reserve(reserveN); out.forceN.reserve(reserveN);
        out.torqueNm.reserve(reserveN); out.distFt.reserve(reserveN);
        out.status.reserve(reserveN);

        double time = 0.0, omega_m = 0.0, vel_m_s = 0.0, dist_m = 0.0;
        bool wasLimited = false;
        int guard = 0;

        while (time <= tEnd + 1e-9 && guard < 2'000'001)
        {
            const double torque_pot = torque_stall * (1.0 - (omega_m / omega_no_load));
            const double torque_m   = std::min(std::max(torque_pot, 0.0), Traction_Limit);

            const double accel_mps2 = (torque_m * K_sys) / std::fmax(Inertia_eq, 0.00001);
            const double alpha_m    = torque_m / std::fmax(Inertia_eq, 0.00001);

            const bool limited = (torque_m >= Traction_Limit - 0.01);
            const int  status  = limited ? STATUS_LIMIT : STATUS_MOTOR;

            const double speedMph = vel_m_s * DT::MPS_TO_MPH;
            const double accelG   = accel_mps2 / DT::G;
            const double forceN   = (K_sys > 1e-12) ? (torque_m / K_sys) : 0.0;

            out.time.push_back(time);
            out.speedMph.push_back(speedMph);
            out.accelG.push_back(accelG);
            out.forceN.push_back(forceN);
            out.torqueNm.push_back(torque_m);
            out.distFt.push_back(dist_m * DT::M_TO_FT);
            out.status.push_back(status);

            // Track the LIMIT -> MOTOR transition (when traction is "released").
            if (wasLimited && !limited && out.timeToReleaseTraction < 0.0)
                out.timeToReleaseTraction = time;
            wasLimited = limited;

            // Integrate forward (matches original ordering).
            omega_m += alpha_m * dt;
            vel_m_s  = omega_m * K_sys;
            dist_m  += vel_m_s * dt;
            time    += dt;
            ++guard;
        }

        // --- Headline metrics ---
        out.launchTractionLimited = !out.status.empty() && out.status.front() == STATUS_LIMIT;
        out.launchForceN          = out.forceN.empty() ? 0.0 : out.forceN.front();
        out.topSpeedMph           = out.speedMph.empty() ? 0.0 : out.speedMph.back();
        out.distanceFt            = out.distFt.empty() ? 0.0 : out.distFt.back();
        out.peakAccelG            = 0.0;
        for (double a : out.accelG) out.peakAccelG = std::max(out.peakAccelG, a);

        return out;
    }

    // -----------------------------------------------------------------------
    // Lightweight, allocation-free headline metrics for one axle. Same step
    // physics as SimulateAxle but stores no time series — cheap enough to call
    // dozens of times per frame for the sweep / explorer tables.
    // -----------------------------------------------------------------------
    struct AxleMetrics
    {
        double K_sys             = 0.0;
        double R                 = 0.0;
        double noLoadTopSpeedMph = 0.0;
        double topSpeedMph       = 0.0;
        double peakAccelG        = 0.0;
        double launchForceN      = 0.0;
        double tractionLimitN    = 0.0;
        double distanceFt        = 0.0;
        bool   launchTractionLimited = false;
        int    finalStatus       = STATUS_MOTOR;
        bool   valid             = false;
    };

    inline AxleMetrics SimulateAxleMetrics(const DrivetrainConfig& cfg,
                                           double wheelInches,
                                           double pulleyTeeth,
                                           double motorPulley)
    {
        const AxleConstants ac = ComputeAxleConstants(cfg, wheelInches, pulleyTeeth, motorPulley);

        AxleMetrics m;
        m.K_sys             = ac.K_sys;
        m.R                 = ac.R;
        m.noLoadTopSpeedMph = ac.noLoadTopSpeedMph;
        m.tractionLimitN    = ac.tractionForceN;

        if (ac.omegaNoLoad <= 0.0 || ac.K_sys <= 0.0 || cfg.dt <= 0.0 || cfg.simTime <= 0.0)
            return m; // valid stays false

        const double dt   = cfg.dt;
        const double tEnd = std::max(cfg.simTime, 0.0);

        double time = 0.0, omega_m = 0.0, vel_m_s = 0.0, dist_m = 0.0;
        bool wasLimited = false, first = true;
        int guard = 0;

        while (time <= tEnd + 1e-9 && guard < 2'000'001)
        {
            const double torque_pot = ac.torqueStall * (1.0 - (omega_m / ac.omegaNoLoad));
            const double torque_m   = std::min(std::max(torque_pot, 0.0), ac.tractionTorque);
            const double accel_mps2 = (torque_m * ac.K_sys) / std::fmax(ac.inertiaEq, 0.00001);
            const double alpha_m    = torque_m / std::fmax(ac.inertiaEq, 0.00001);
            const bool   limited    = (torque_m >= ac.tractionTorque - 0.01);

            // Display values mirror SimulateAxle (speed/dist lag one step).
            m.topSpeedMph = vel_m_s * DT::MPS_TO_MPH;
            m.distanceFt  = dist_m * DT::M_TO_FT;
            m.peakAccelG  = std::max(m.peakAccelG, accel_mps2 / DT::G);
            m.finalStatus = limited ? STATUS_LIMIT : STATUS_MOTOR;
            if (first)
            {
                m.launchForceN = (ac.K_sys > 1e-12) ? (torque_m / ac.K_sys) : 0.0;
                m.launchTractionLimited = limited;
                first = false;
            }
            wasLimited = limited; (void)wasLimited;

            omega_m += alpha_m * dt;
            vel_m_s  = omega_m * ac.K_sys;
            dist_m  += vel_m_s * dt;
            time    += dt;
            ++guard;
        }

        m.valid = true;
        return m;
    }

    // -----------------------------------------------------------------------
    // Solver helpers for the explorer tabs (no integration needed).
    // -----------------------------------------------------------------------

    // Wheel diameter (in) for `front`(true)/rear(false) that makes its K_sys
    // equal the OTHER axle's K_sys, with all pulleys + the other wheel fixed.
    inline double SyncWheelDiameterIn(const DrivetrainConfig& cfg, bool forFront)
    {
        const double G = cfg.gearboxReduction;
        double kOther, Rthis;
        if (forFront)
        {
            const double rRear = (cfg.rearWheelInches / 2.0) * DT::INCHES_TO_METERS;
            const double Rrear = cfg.rearPulleyTeeth / cfg.motorPulleyRear;
            kOther = rRear / (G * Rrear);
            Rthis  = cfg.frontPulleyTeeth / cfg.motorPulleyFront;
        }
        else
        {
            const double rFront = (cfg.frontWheelInches / 2.0) * DT::INCHES_TO_METERS;
            const double Rfront = cfg.frontPulleyTeeth / cfg.motorPulleyFront;
            kOther = rFront / (G * Rfront);
            Rthis  = cfg.rearPulleyTeeth / cfg.motorPulleyRear;
        }
        const double rThis = kOther * G * Rthis;
        return 2.0 * rThis / DT::INCHES_TO_METERS;
    }

    // Pulley ratio R (= driven/driving) for `front`(true)/rear(false) that makes
    // its K_sys equal the OTHER axle's, with both wheels + the other axle fixed.
    inline double SyncPulleyRatio(const DrivetrainConfig& cfg, bool forFront)
    {
        const double G = cfg.gearboxReduction;
        double kOther, rThis;
        if (forFront)
        {
            const double rRear = (cfg.rearWheelInches / 2.0) * DT::INCHES_TO_METERS;
            const double Rrear = cfg.rearPulleyTeeth / cfg.motorPulleyRear;
            kOther = rRear / (G * Rrear);
            rThis  = (cfg.frontWheelInches / 2.0) * DT::INCHES_TO_METERS;
        }
        else
        {
            const double rFront = (cfg.frontWheelInches / 2.0) * DT::INCHES_TO_METERS;
            const double Rfront = cfg.frontPulleyTeeth / cfg.motorPulleyFront;
            kOther = rFront / (G * Rfront);
            rThis  = (cfg.rearWheelInches / 2.0) * DT::INCHES_TO_METERS;
        }
        return (kOther > 1e-12) ? rThis / (G * kOther) : 0.0;
    }

    // -----------------------------------------------------------------------
    // Validate inputs, then simulate both axles. Returns valid=false with a
    // human-readable error if a parameter would produce NaN/Inf curves.
    // -----------------------------------------------------------------------
    inline SimOutput Simulate(const DrivetrainConfig& cfg)
    {
        SimOutput sim;

        auto fail = [&](const std::string& msg) { sim.valid = false; sim.error = msg; return sim; };

        if (cfg.dt <= 0.0)                 return fail("Time step must be > 0.");
        if (cfg.simTime <= 0.0)            return fail("Total simulation time must be > 0.");
        if (cfg.gearboxReduction <= 0.0)   return fail("Gearbox reduction must be > 0.");
        if (cfg.kv <= 0.0)                 return fail("Motor Kv must be > 0.");
        if (cfg.vBatt <= 0.0)              return fail("Battery voltage must be > 0.");
        if (cfg.speedFactor <= 0.0)        return fail("Speed factor must be > 0.");
        if (cfg.totalWeightLb <= 0.0)      return fail("Total weight must be > 0.");
        if (cfg.frontWheelInches <= 0.0 || cfg.rearWheelInches <= 0.0)
            return fail("Wheel diameters must be > 0.");
        if (cfg.motorPulleyFront <= 0.0 || cfg.motorPulleyRear <= 0.0)
            return fail("Motor pulley teeth must be > 0.");
        if (cfg.frontPulleyTeeth <= 0.0 || cfg.rearPulleyTeeth <= 0.0)
            return fail("Wheel pulley teeth must be > 0.");

        sim.front = SimulateAxle(cfg, cfg.frontWheelInches, cfg.frontPulleyTeeth, cfg.motorPulleyFront);
        sim.rear  = SimulateAxle(cfg, cfg.rearWheelInches,  cfg.rearPulleyTeeth,  cfg.motorPulleyRear);

        sim.kFront = sim.front.K_sys;
        sim.kRear  = sim.rear.K_sys;
        sim.velMismatch = std::abs(sim.kFront - sim.kRear) > 0.0001;

        // Tangential (ground) wheel-surface speeds. noLoadTopSpeedMph already
        // equals omegaNoLoad * K_sys, i.e. the wheel's no-slip surface speed,
        // so the relative gap is speed-independent (= |K_f - K_r| / max(K)).
        sim.vTangFrontMph = sim.front.noLoadTopSpeedMph;
        sim.vTangRearMph  = sim.rear.noLoadTopSpeedMph;
        const double denom = std::max({ std::abs(sim.vTangFrontMph),
                                        std::abs(sim.vTangRearMph), 1e-9 });
        sim.tangMismatchPct = std::abs(sim.vTangFrontMph - sim.vTangRearMph) / denom * 100.0;

        return sim;
    }

} // namespace Workspace
