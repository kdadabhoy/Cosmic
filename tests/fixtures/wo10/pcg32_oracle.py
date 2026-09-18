# Independent PCG32 (XSH-RR, O'Neill) reference — NOT the engine's Random.h.
MASK64 = (1 << 64) - 1
def pcg32(seed, stream):
    inc = ((stream << 1) | 1) & MASK64
    state = 0
    def step():
        nonlocal state
        old = state
        state = (old * 6364136223846793005 + inc) & MASK64
        xorshifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rot = old >> 59
        return ((xorshifted >> rot) | (xorshifted << ((32 - rot) & 31))) & 0xFFFFFFFF
    step(); state = (state + seed) & MASK64; step()
    return step
g = pcg32(42, 54)
first = [g() for _ in range(6)]
print("first6:", [hex(v) for v in first])
# FNV-1a 64 over the little-endian bytes of 1,000,000 outputs (same seed/stream)
g = pcg32(42, 54)
h = 0xcbf29ce484222325
import struct
vals = [g() for _ in range(1000000)]
for v in vals:
    for b in struct.pack('<I', v):
        h ^= b; h = (h * 0x100000001b3) & MASK64
print("fnv1a64 of 1,000,000 outputs (seed 42, stream 54): 0x%016x" % h)
print("last: 0x%08x" % vals[-1], "sum mod 2^32: 0x%08x" % (sum(vals) & 0xFFFFFFFF))
