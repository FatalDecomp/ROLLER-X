"""A non-magnetic surface stops launching the car below a crawl.

The launch test already scales with speed -- a car leaves a non-magnetic
surface when the ground has dropped further than ``fFinalSpeed * 0.015``
below it, which is a gradient gate and the right one. What it had no floor
for was the bottom of that scale: at a crawl the tolerance goes to nearly
nothing, so the smallest lip reads as a gradient the car cannot follow and a
car edging over a non-magnetic crest at walking pace is thrown into the air
by it. The arena answered the same problem with a speed floor [SIM-12]; this
is that floor in the race game's units [SIM-32].

Car physics has no runtime harness in this tree -- nothing in tests/ links
control.c -- so what is pinned here is the shape of the fix rather than its
effect, in the way this repo already pins structure it cannot execute.

The shape is the point. The floor clears ``uiJumpFlag`` once, in the block
where the two existing guards clear it, and every launch site downstream
reads that one flag. Written per-site instead it would be eight copies of
the same test and a ninth site added later that forgets.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTROL = ROOT / "PROJECTS" / "ROLLER" / "control.c"
CONSTANT = "WHIP_RAMP_LAUNCH_MIN_SPEED"


class RampLaunchSpeedFloorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = CONTROL.read_text(encoding="utf-8", errors="replace")
        self.lines = self.source.splitlines()

    def test_the_floor_is_a_named_constant_at_a_crawl(self) -> None:
        match = re.search(
            r"#define\s+%s\s+([0-9.]+)" % CONSTANT, self.source
        )
        self.assertIsNotNone(match, "the speed floor must be a named constant")
        value = float(match.group(1))
        # Above a standstill, or it would never hold anything down.
        self.assertGreater(value, 0.0)
        # And well under the speeds this file already treats as slow, so it
        # only ever catches a car that is barely moving. The speedometer
        # divides internal speed by three, so this is about seven mph.
        self.assertLess(value, 36.0)

    def test_the_floor_clears_the_launch_flag(self) -> None:
        self.assertRegex(
            self.source,
            r"if\s*\(\s*pCar->fFinalSpeed\s*<\s*%s\s*\)\s*\n\s*uiJumpFlag\s*=\s*0;"
            % CONSTANT,
            "below the floor the surface must stop launching the car",
        )

    def test_the_floor_is_set_before_any_launch_decision(self) -> None:
        """One flag, set once, read everywhere -- not a test per site."""
        assigned = None
        floored = None
        first_use = None
        for index, line in enumerate(self.lines):
            if assigned is None and re.search(
                r"uiJumpFlag\s*=\s*abs\(.*SURFACE_FLAG_NON_MAGNETIC", line
            ):
                assigned = index
            if floored is None and CONSTANT in line and "#define" not in line:
                floored = index
            if (
                first_use is None
                and assigned is not None
                and re.search(r"if\s*\(\s*!?uiJumpFlag", line)
            ):
                first_use = index

        self.assertIsNotNone(assigned, "the launch flag must be assigned")
        self.assertIsNotNone(floored, "the floor must be applied")
        self.assertIsNotNone(first_use, "the launch flag must be read")
        self.assertLess(
            assigned, floored, "the floor applies after the flag is set"
        )
        self.assertLess(
            floored,
            first_use,
            "the floor must apply before anything acts on the flag, or the "
            "sites before it launch a car the floor was meant to hold",
        )

    def test_the_existing_guards_are_untouched(self) -> None:
        """The floor is added beside the original two, not instead of them."""
        self.assertRegex(
            self.source,
            r"if\s*\(\s*pCar->fFinalSpeed\s*<\s*0\.0\s*\)\s*\n\s*uiJumpFlag\s*=\s*0;",
            "reversing must still not launch",
        )
        self.assertIn("SURFACE_FLAG_PREVENT_JUMP", self.source)


if __name__ == "__main__":
    unittest.main()
