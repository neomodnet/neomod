import Module from './bin/neomod.js';

const neomod = await Module();

async function test_pp_version() {
    console.log("pp version:", neomod.PP_ALGORITHM_VERSION);
}

// https://osu.ppy.sh/scores/1641562531 (636pp)
async function test_pp_simple() {
    const res = await fetch('https://osu.ppy.sh/osu/3337690');
    const map_bytes = await res.bytes();

    const beatmap = new neomod.Beatmap(map_bytes);
    try {
        const pp = beatmap.calculate({
            num300s: 3027,
            num100s: 115,
            num50s: 4,
            numMisses: 11,
            score: 300461780,
            comboMax: 3483,
        });
        console.log("pp:", pp);
    } catch(err) {
        console.error("failed to calc pp:", err);
    } finally {
        beatmap.delete();
    }
}

test_pp_version();
test_pp_simple();
