#!/usr/bin/env python3
# deterministic generator for maps/synthetic-5001-sliders.osu (McKay >5000-slider deferred
# curve alloc path). re-running always produces the same bytes.
import os

N = 5001
out = os.path.join(os.path.dirname(__file__), "maps", "synthetic-5001-sliders.osu")
with open(out, "w", newline="\n") as f:
    f.write("""osu file format v14

[General]
AudioFilename: audio.mp3
StackLeniency: 0.7
Mode: 0

[Metadata]
Title:synthetic 5001 sliders
Artist:diffcalc suite
Creator:neomod
Version:fixture

[Difficulty]
HPDrainRate:5
CircleSize:4
OverallDifficulty:8
ApproachRate:9
SliderMultiplier:1.4
SliderTickRate:1

[TimingPoints]
0,300,4,2,0,60,1,0

[HitObjects]
""")
    for i in range(N):
        x = 64 + (i * 37) % 384
        y = 64 + (i * 53) % 256
        t = 1000 + i * 300
        f.write(f"{x},{y},{t},2,0,L|{min(512, x + 40)}:{y},1,30\n")
print(f"wrote {out} ({N} sliders)")
