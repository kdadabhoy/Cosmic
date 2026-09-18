# gen_trajectory.py — writes the F-TRAJECTORY reference fixture (2D stability
# catalog, WO-10 / X01) INDEPENDENTLY of the engine: x = 30 t, y = 50 t - 0.5 g t^2,
# vx = 30, vy = 50 - g t, g = 9.80665, t = i/120, i = 0..1200 (1,201 rows), every
# value a shortest-round-trip double. The same expressions, in the same
# association, are what AnalysisFixtures.h evaluates in C++ — so the test can
# require bit-equality between this file and the generator.
import sys
g = 9.80665
out = sys.argv[1] if len(sys.argv) > 1 else "trajectory.csv"
with open(out, "w", newline="\n") as f:
    f.write("t,x,y,vx,vy\n")
    for i in range(1201):
        t = i / 120.0
        x = 30.0 * t
        y = 50.0 * t - 0.5 * g * t * t
        vx = 30.0
        vy = 50.0 - g * t
        f.write("%s,%s,%s,%s,%s\n" % (repr(t), repr(x), repr(y), repr(vx), repr(vy)))
print("wrote", out)
