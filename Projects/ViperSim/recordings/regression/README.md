# Regression recordings

Every gate/phase demo is a recorded session (doc 04 §5): the scenario runner
writes to `user://recordings/regression/<name>/session` (dev tree:
`build/Runtime/<CONFIG>/recordings/regression/`). Copy the sessions that
represent an accepted gate run into this folder and commit them, so "replay
after changes" has a pinned baseline:

- `g1_hover/` — G1 hover vs noise + 5 m/s gusts
- `g2_transition/` — G2 scripted VTOL→cruise→VTOL (one per CG/airspeed sweep row)
- `g3_orbit_failsafe/` — G3 orbit-in-gusts + link-kill RTL

Replay them in the Replay screen, or run the Tuning screen's
replay-through-FC on them after controller changes.
