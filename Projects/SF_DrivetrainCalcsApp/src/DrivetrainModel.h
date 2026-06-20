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

        bool        valid = true;
        std::string error;
    };

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
        const double mass_kg_side  = (cfg.totalWeightLb / 2.0) * DT::LB_TO_KG;
        const double weight_n_side = mass_kg_side * DT::G;
        const double r             = (wheelInches / 2.0) * DT::INCHES_TO_METERS;
        const double R             = pulleyTeeth / motorPulley;

        const double K_sys         = r / (cfg.gearboxReduction * R);
        const double kt            = 60.0 / (2.0 * DT::PI * cfg.kv);
        const double omega_no_load = (cfg.kv * cfg.vBatt * cfg.speedFactor) * (2.0 * DT::PI / 60.0);
        const double torque_stall  = cfg.currentLimit * kt * cfg.drivetrainEfficiency;
        const double Traction_Limit = cfg.mu * weight_n_side * K_sys;
        const double Inertia_eq    = mass_kg_side * std::pow(K_sys, 2);

        out.K_sys          = K_sys;
        out.omegaNoLoad    = omega_no_load;
        out.torqueStall    = torque_stall;
        out.inertiaEq      = Inertia_eq;
        out.noLoadTopSpeedMph = omega_no_load * K_sys * DT::MPS_TO_MPH;

        // In the original, Traction_Limit is a *torque* cap (mu*W*K_sys). The
        // contact-patch force at that cap is Traction_Limit / K_sys = mu*W.
        out.tractionLimitN = cfg.mu * weight_n_side;

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

        return sim;
    }

} // namespace Workspace
