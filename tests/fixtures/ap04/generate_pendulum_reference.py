#!/usr/bin/env python3
"""F-PENDULUM — the analytic small-angle reference for PendulumLab (App Platform / AP-04).

    theta'' = -(g / L) theta - c theta'          theta(0) = theta0, theta'(0) = 0
    gamma = c / 2,  w0^2 = g / L,  wd = sqrt(w0^2 - gamma^2)
    theta(t) = theta0 e^{-gamma t} (cos(wd t) + (gamma / wd) sin(wd t))
    omega(t) = -theta0 e^{-gamma t} (w0^2 / wd) sin(wd t)

L = 1 m, g = 9.80665 m/s^2, theta0 = 5 deg, c in {0, 0.05} 1/s, t = i / 240 for i = 0..2400,
double precision, %.17g. Written to pendulum_reference.csv next to this script (git add -f: *.csv
is gitignored). Run with `py -3 generate_pendulum_reference.py`; Generate-PendulumReference.ps1 is
the PowerShell 5.1 fallback that prefers this script when `py` exists.
"""
import math, os, sys

L, G, THETA0_DEG, HZ, N = 1.0, 9.80665, 5.0, 240, 2400
DAMPINGS = (0.0, 0.05)


def series(c, i):
    t = i / HZ
    theta0 = THETA0_DEG * math.pi / 180.0
    w0sq = G / L
    gamma = c / 2.0
    wd = math.sqrt(w0sq - gamma * gamma)
    env = theta0 * math.exp(-gamma * t)
    theta = env * (math.cos(wd * t) + (gamma / wd) * math.sin(wd * t))
    omega = -env * (w0sq / wd) * math.sin(wd * t)
    return t, theta, omega


def main(out):
    lines = ["# F-PENDULUM analytic small-angle reference: L=1 g=9.80665 theta0=5deg dt=1/240 (AP-04)",
             "t,theta_c0,omega_c0,theta_c005,omega_c005"]
    for i in range(N + 1):
        t, th0, om0 = series(DAMPINGS[0], i)
        _, th1, om1 = series(DAMPINGS[1], i)
        lines.append("%.17g,%.17g,%.17g,%.17g,%.17g" % (t, th0, om0, th1, om1))
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", out, N + 1, "rows")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "pendulum_reference.csv"))
