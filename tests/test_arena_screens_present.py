"""Every screen the arena draws has to be put on the screen.

Selecting VIEW CONTROLS on the briefing softlocked the game, and the cause was
not in the controls page: its drawing was fine and a test already checked the
pixels. What it did not do was open a renderer frame. `game_render_end_frame`
is what presents the buffer, so the page was drawn into memory and never shown
-- the briefing stayed up, the mode ran happily behind it with its input still
working, and from the player's side the button did nothing forever.

The fix is structural rather than local: one begin/end pair around the whole of
`mecha_mode_draw`, so a screen added later cannot be the branch that forgets.
What this file pins is that shape -- one frame opened, one closed, nothing
drawn outside them, and no early return in between to leave one hanging.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODE = ROOT / "PROJECTS" / "ROLLER" / "mecha_mode.c"

BEGIN = "game_render_begin_frame"
END = "game_render_end_frame"


def function_body(source: str, signature: str) -> str:
    """The braced body of one function, by brace matching from its signature."""
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for i in range(open_brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace : i + 1]
    raise AssertionError(f"unbalanced braces after {signature!r}")


def strip_comments(source: str) -> str:
    """Comments talk about the bug; they must not count as code doing it."""
    return re.sub(r"/\*.*?\*/", " ", source, flags=re.S)


class TheArenaDrawPresentsExactlyOneFrame(unittest.TestCase):
    def setUp(self) -> None:
        self.body = strip_comments(
            function_body(MODE.read_text(encoding="utf-8"), "void mecha_mode_draw(void)")
        )

    def test_one_frame_opened_and_one_closed(self) -> None:
        self.assertEqual(self.body.count(BEGIN), 1, "draw must open exactly one frame")
        self.assertEqual(self.body.count(END), 1, "draw must close exactly one frame")

    def test_the_frame_is_opened_before_it_is_closed(self) -> None:
        self.assertLess(self.body.index(BEGIN), self.body.index(END))

    def test_nothing_is_drawn_outside_the_frame(self) -> None:
        """The bug itself: a screen drawn where nothing will present it."""
        opened = self.body.index(BEGIN)
        closed = self.body.index(END)
        drawn = [m.start() for m in re.finditer(r"\bmecha_render_\w+\s*\(", self.body)]
        self.assertTrue(drawn, "the draw has to draw something")
        for at in drawn:
            call = self.body[at : self.body.index("(", at)].strip()
            self.assertGreater(at, opened, f"{call} draws before the frame is opened")
            self.assertLess(at, closed, f"{call} draws after the frame is closed")

    def test_no_early_return_leaves_a_frame_open(self) -> None:
        """A return between the pair presents nothing and strands the frame."""
        opened = self.body.index(BEGIN)
        closed = self.body.index(END)
        between = self.body[opened:closed]
        self.assertNotIn("return", between)

    def test_every_screen_the_mode_has_is_drawn(self) -> None:
        """A screen with no branch here is a screen that shows the last one."""
        source = strip_comments(MODE.read_text(encoding="utf-8"))
        screens = re.findall(r"\bMECHA_SCREEN_(\w+)\s*=", source)
        self.assertGreaterEqual(len(screens), 3)
        # The match is the fall-through, so it needs no test of its own.
        for screen in screens:
            if screen == "MATCH":
                continue
            self.assertIn(
                f"MECHA_SCREEN_{screen}",
                self.body,
                f"MECHA_SCREEN_{screen} has no branch in mecha_mode_draw",
            )


if __name__ == "__main__":
    unittest.main()
