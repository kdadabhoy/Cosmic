#pragma once

// WeaponModel.h
// SF_Telem_Weapon
//
// ============================================================================
// Predictive weapon spin-up model  (port of the "Weapon Speed Analysis"
// spreadsheet by Kaden Dadabhoy / Shear Force)
// ============================================================================
//
// Given the motor, battery, reduction, weapon inertia, tip radius and an
// aerodynamic-drag coefficient, this predicts the full-throttle spin-up curve
// and the steady-state max tip speed / weapon RPM. It runs alongside the live
// telemetry so measured performance can be compared against theory.
//
// PHYSICS (identical to the spreadsheet)
// --------------------------------------
//   Kt              = 60 / (2*pi*Kv)                       [N*m/A]
//   MotorNoLoadRPM  = BatteryVoltage * Kv                  [rpm]   (full throttle)
//   MotorStall      = (MaxCurrent - NoLoadCurrent) * Kt    [N*m]
//   WeaponNoLoadRPM = MotorNoLoadRPM / Reduction           [rpm]
//   WeaponStall     = MotorStall     * Reduction           [N*m]
//   TWSlope         = WeaponStall / WeaponNoLoadRPM        [N*m per weapon-rpm]
//
//   per step (forward Euler, state = weapon omega in rad/s):
//     weaponRPM  = omega * 60 / (2*pi)
//     motorT     = WeaponStall - TWSlope * weaponRPM       (linear t-w curve)
//     dragT      = DragCoeff * weaponRPM^2                 (CFD quadratic fit)
//     alpha      = (motorT - dragT) / Inertia
//     omega     += alpha * dt
//     tipSpeed   = (omega * tipRadius_m)  -> mph
//
// The reduction and tip radius are passed in from the live decode constants
// (GearRatio and WeaponDiameterIn) so the geometry lives in exactly one place.
//
// NOTE: InternalResistance is carried for reference but, like the spreadsheet,
// is NOT used by the simplified linear torque-speed line.
// ============================================================================

#include <vector>
#include <cmath>

namespace Workspace
{
    struct WeaponModelConfig
    {
        // --- Motor / battery ---
        float BatteryVoltage = 50.4f;   // V at full throttle (12S * 4.2 = 50.4)
        float MotorKv        = 950.0f;  // rpm/V
        float NoLoadCurrent  = 5.1f;    // A
        float InternalRes    = 0.0049f; // ohm (reference only; not in t-w line)
        float MaxCurrent     = 212.0f;  // A (limited by motor/ESC)

        // --- Mechanical ---
        float Inertia        = 0.0091865073f; // kg*m^2 (weapon + pulley, from CAD)

        // --- Aerodynamics ---
        // Drag torque [N*m] = DragCoeff * weaponRPM^2  (2nd-deg fit of CFD data)
        float DragCoeff      = 9.717054733e-09f;

        // --- Sim controls ---
        float SimDuration    = 20.0f;   // s
        int   SimSteps       = 500;
    };

    struct WeaponModelResult
    {
        // Derived motor / weapon parameters.
        float Kt               = 0.0f;
        float MotorNoLoadRPM   = 0.0f;
        float MotorStallTorque = 0.0f;
        float WeaponNoLoadRPM  = 0.0f;
        float WeaponStallTorque= 0.0f;
        float TWSlope          = 0.0f;

        // Headline outputs.
        float MaxTipSpeedMph   = 0.0f;
        float MaxWeaponRPM     = 0.0f;
        float TimeTo90Pct      = -1.0f; // s to reach 90% of max tip speed (-1 = n/a)
        float SteadyStateRPM   = 0.0f;  // analytic torque-balance solution

        // Spin-up series (size SimSteps+1) for plotting.
        std::vector<float> t;        // s
        std::vector<float> tipMph;   // mph
        std::vector<float> weaponRpm;// rpm
    };

    // -------------------------------------------------------------------------
    // SimulateWeaponModel — reduction + tipRadiusM come from the live decode
    // constants (GearRatio, WeaponDiameterIn/2). Integration is done in double
    // to match the spreadsheet; the stored series is float for plotting.
    // -------------------------------------------------------------------------
    inline WeaponModelResult SimulateWeaponModel(const WeaponModelConfig& c,
                                                 float reduction, float tipRadiusM)
    {
        constexpr double PI         = 3.14159265358979323846;
        constexpr double MPS_TO_MPH = 2.236936292;

        WeaponModelResult r;

        const double N  = (reduction != 0.0f) ? reduction : 1.0;
        const double Kv = (c.MotorKv > 0.0f) ? c.MotorKv : 1.0;
        const double Kt = 60.0 / (2.0 * PI * Kv);
        const double I  = (c.Inertia > 1e-9f) ? c.Inertia : 1e-9;
        const double rTip = tipRadiusM;

        const double motorNoLoad = c.BatteryVoltage * Kv;
        const double motorStall  = (c.MaxCurrent - c.NoLoadCurrent) * Kt;
        const double wpnNoLoad   = motorNoLoad / N;
        const double wpnStall    = motorStall * N;
        const double slope       = (wpnNoLoad > 0.0) ? (wpnStall / wpnNoLoad) : 0.0;
        const double k           = c.DragCoeff;

        r.Kt                = (float)Kt;
        r.MotorNoLoadRPM    = (float)motorNoLoad;
        r.MotorStallTorque  = (float)motorStall;
        r.WeaponNoLoadRPM   = (float)wpnNoLoad;
        r.WeaponStallTorque = (float)wpnStall;
        r.TWSlope           = (float)slope;

        const int   steps = (c.SimSteps > 0) ? c.SimSteps : 1;
        const double dt    = (c.SimDuration > 0.0f) ? (double)c.SimDuration / steps : 0.001;

        r.t.reserve(steps + 1);
        r.tipMph.reserve(steps + 1);
        r.weaponRpm.reserve(steps + 1);

        double w = 0.0; // weapon angular velocity (rad/s)
        double maxTip = 0.0, maxRpm = 0.0;

        for (int i = 0; i <= steps; ++i)
        {
            const double time = i * dt;
            const double rpm  = w * 60.0 / (2.0 * PI);
            const double tip  = (w * rTip) * MPS_TO_MPH;

            r.t.push_back((float)time);
            r.tipMph.push_back((float)tip);
            r.weaponRpm.push_back((float)rpm);

            if (tip > maxTip) maxTip = tip;
            if (rpm > maxRpm) maxRpm = rpm;

            const double motorT = wpnStall - slope * rpm;
            const double dragT  = k * rpm * rpm;
            const double alpha  = (motorT - dragT) / I;
            w += alpha * dt;
        }

        r.MaxTipSpeedMph = (float)maxTip;
        r.MaxWeaponRPM   = (float)maxRpm;

        // Time to reach 90% of the max tip speed.
        const double target = 0.9 * maxTip;
        for (size_t i = 0; i < r.t.size(); ++i)
            if (r.tipMph[i] >= target) { r.TimeTo90Pct = r.t[i]; break; }

        // Analytic steady state: k*rpm^2 + slope*rpm - wpnStall = 0.
        const double disc = slope * slope + 4.0 * k * wpnStall;
        r.SteadyStateRPM = (k > 0.0 && disc >= 0.0)
                         ? (float)((-slope + std::sqrt(disc)) / (2.0 * k))
                         : (float)maxRpm;

        return r;
    }

} // namespace Workspace
