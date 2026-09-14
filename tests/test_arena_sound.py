"""The arena's sound layer, checked where it can be checked without ears.

The mixer needs a device and the samples need FATDATA, so what is asserted
here is the wiring and the conventions: that the mode drives the sound layer
at the right moments, that the sim is still told nothing about sound, and
that the pan and volume constants are the ones Whiplash's own mixer expects.
See docs/arena-notes.md [SND-01].
"""

import re
import unittest
from pathlib import Path

ROLLER = Path(__file__).resolve().parents[1] / "PROJECTS" / "ROLLER"
SOUND = ROLLER / "mecha_sound.c"
SOUND_H = ROLLER / "mecha_sound.h"
MODE = ROLLER / "mecha_mode.c"
SIM = ROLLER / "mecha_sim.c"
RENDER_H = ROLLER / "mecha_render.h"


def read(path):
    return path.read_text(encoding="utf-8")


def squeal_block(sound):
    """The block that decides how loud the skid loop is, both branches of
    it: the car's slide and the walker's ground dash."""
    start = sound.index("fLevel = 0.0f;\n  if (pDef->bWheeled)")
    return sound[start : sound.index("loopsample(iMechIdx, MECHA_SFX_SKID",
                                     start)]


class ArenaSoundTests(unittest.TestCase):
    def test_mode_drives_the_sound_layer_at_the_right_moments(self):
        mode = read(MODE)
        self.assertIn('#include "mecha_sound.h"', mode)
        for call in (
            "mecha_sound_enter();",
            "mecha_sound_exit();",
            "mecha_sound_briefing();",
            "mecha_sound_match();",
            "mecha_sound_update(&s_World, &s_Camera, s_iPlayerIdx);",
        ):
            self.assertIn(call, mode, call)

    def test_the_listener_is_placed_after_the_camera_moves(self):
        """A frame's sound has to be mixed from where that frame is drawn."""
        mode = read(MODE)
        camera = mode.index("mecha_mode_free_camera_update();")
        listener = mode.index("mecha_sound_update(&s_World, &s_Camera,")
        self.assertLess(camera, listener)

    def test_the_simulation_still_knows_nothing_about_sound(self):
        """The sim stays headless: the sound layer reads the world instead."""
        sim = read(SIM)
        self.assertNotIn("mecha_sound", sim)
        self.assertNotIn("sound.h", sim)

    def test_it_borrows_whiplash_samples_rather_than_shipping_its_own(self):
        sound = read(SOUND)
        for sample in (
            "SOUND_SAMPLE_ENGINE",
            "SOUND_SAMPLE_SKID1",
            "SOUND_SAMPLE_LANDSKID",
            "SOUND_SAMPLE_EXPLO",
            "SOUND_SAMPLE_BIGCRASH",
            "SOUND_SAMPLE_FENDER",
            "SOUND_SAMPLE_GRSHIFT",
            "SOUND_SAMPLE_BLOP",
            "SOUND_SAMPLE_LIGHTLAN",
            "SOUND_SAMPLE_BUTTON",
            "SOUND_SAMPLE_BRP",
        ):
            self.assertIn(sample, sound, sample)

    def test_every_sample_it_plays_is_one_it_asked_to_be_loaded(self):
        """A sample nobody loaded is silence, and silence is not a crash --
        so nothing would ever report it. [SND-04]"""
        sound = read(SOUND)
        enter = sound[sound.index("void mecha_sound_enter(void)"):]
        loaded = set(re.findall(r"MECHA_SFX_\w+", enter[: enter.index("\n}")]))
        played = set(re.findall(r"MECHA_SFX_\w+", sound))
        self.assertTrue(played <= loaded, sorted(played - loaded))

    def test_the_squeal_belongs_to_the_boost_and_not_to_strafing(self):
        """A mecha strafes for a living: keying the skid loop off the angle
        between facing and travel made walking sideways squeal. [SND-06]"""
        skid = squeal_block(read(SOUND))
        walker = skid[skid.index("} else if"):]
        self.assertIn("MECHA_MOVE_DASH", walker)
        self.assertIn("mecha_mech_is_airborne", walker)
        # A walker never asks which way it is travelling relative to its nose.
        self.assertNotIn("MECHA_SND_SLIP_ANGLE", walker)

    def test_a_car_squeals_when_it_slides_and_never_on_a_boost(self):
        """The one machine on the roster that is a car has no thrusters to
        scrub the floor with. A tyre squeals when it is going one way and
        the car is pointed another, which is how Whiplash decides it.
        [SND-09]"""
        skid = squeal_block(read(SOUND))
        car = skid[: skid.index("} else if")]
        self.assertIn("pDef->bWheeled", car)
        self.assertIn("MECHA_SND_SLIP_ANGLE", car)
        self.assertIn("mecha_angle_delta", car)
        # And the boost is not what starts it.
        self.assertNotIn("MECHA_MOVE_DASH", car)

    def test_only_a_machine_with_legs_is_modulated_by_its_gait(self):
        """Wheels and tracks roll; their noise follows road speed and did
        already. [SND-07]"""
        sound = read(SOUND)
        self.assertIn("MECHA_CHASSIS_BIPED", sound)
        self.assertIn("MECHA_CHASSIS_ARACHNID", sound)
        self.assertNotIn("MECHA_CHASSIS_CAR", sound)
        self.assertNotIn("MECHA_CHASSIS_TREAD", sound)
        self.assertIn("fStepPhase", sound)

    def test_the_cockpit_warnings_are_the_view_machine_only(self):
        """They are the player's own machine talking to the player, so they
        are neither placed nor attenuated -- and nobody else's. [SND-05]"""
        sound = read(SOUND)
        self.assertIn("if (i == iViewMech) {", sound)
        warn = sound[sound.index("if (i == iViewMech) {"):]
        warn = warn[: warn.index("\n    }")]
        self.assertIn("iHitTakenTick", warn)
        self.assertIn("iDryFireTick", warn)

    def test_the_two_warnings_are_not_the_same_noise(self):
        """They started as one sample at two pitches, and the high one was a
        car horn -- which is what a bright broadband sample pitched up sounds
        like. One is a buzz and the other a clunk now. [SND-05]"""
        sound = read(SOUND)
        buzz = re.search(r"#define MECHA_SFX_BUZZ\s+(\S+)", sound)[1]
        clunk = re.search(r"#define MECHA_SFX_CLUNK\s+(\S+)", sound)[1]
        self.assertNotEqual(buzz, clunk)

    def test_each_warning_carries_its_own_sample_and_rate(self):
        """Which event gets which noise has changed twice, so the mapping is
        one pair of defines rather than something spelt out at the call. A
        rate belongs to the sample it was tuned against, not to the event:
        taking the buzz up instead of down is how it became a horn. [SND-05]"""
        sound = read(SOUND)
        hurt = re.search(r"#define MECHA_SND_HURT_SFX\s+(\S+)", sound)[1]
        dry = re.search(r"#define MECHA_SND_DRY_SFX\s+(\S+)", sound)[1]
        self.assertIn(hurt, ("MECHA_SFX_BUZZ", "MECHA_SFX_CLUNK"))
        self.assertIn(dry, ("MECHA_SFX_BUZZ", "MECHA_SFX_CLUNK"))
        self.assertNotEqual(hurt, dry)
        # The buzz is only ever taken down. Up, it is a car horn.
        rates = {
            hurt: float(
                re.search(r"#define MECHA_SND_HURT_RATE\s+([\d.]+)f", sound)[1]
            ),
            dry: float(
                re.search(r"#define MECHA_SND_DRY_RATE\s+([\d.]+)f", sound)[1]
            ),
        }
        self.assertLess(rates["MECHA_SFX_BUZZ"], 1.0)

    def test_the_squeal_is_not_taken_above_the_rate_it_was_cut_at(self):
        """It ran to 2.7x at an ordinary dash and 4.6x at the quickest,
        which is a whistle rather than a tyre. [SND-06]"""
        sound = read(SOUND)
        rest = float(re.search(r"MECHA_SND_SKID_REST\s+([\d.]+)f", sound)[1])
        rise = float(re.search(r"MECHA_SND_SKID_RISE\s+([\d.]+)f", sound)[1])
        self.assertLess(rest, 1.0)
        self.assertLessEqual(rest + rise, 1.75)

    def test_a_kill_is_heard_once_and_blasts_are_held_apart(self):
        """A burning wreck throws an explosion every few ticks [SIM-27], and
        pannedsample keys its handle on the sample -- so without a gap the
        channel is one sample being restarted forever. [SND-08]"""
        sound = read(SOUND)
        self.assertIn("MECHA_SND_BLAST_GAP", sound)
        self.assertIn("s_abWreckedWas", sound)
        self.assertIn("MECHA_MOVE_DESTROYED", sound)
        # The size heuristic it replaced could never fire for a machine.
        self.assertNotRegex(sound, r"\bbWreck\b")

    def test_the_events_it_reads_are_ticks_the_sim_leaves_behind(self):
        """A frame can run several ticks, so a flag set inside one would be
        gone before the sound layer looked. [TYPE-09]"""
        sim = read(SIM)
        for field in ("iFireTick", "iDryFireTick", "iHitTakenTick",
                      "iHitDealtTick"):
            self.assertRegex(sim, r"->%s = " % field, field)

    def test_pan_runs_left_to_right(self):
        """DIGISetPanLocation reads iPan / 0x8000 - 1, so zero is hard left.

        The sign of the sine term is the whole convention: get it backwards
        and every machine is on the wrong side of the player.
        """
        sound = read(SOUND)
        self.assertRegex(
            sound, r"dPan\s*=\s*\(1\.0\s*\+\s*\(double\)mecha_sin\(",
        )
        self.assertIn("0x8000", sound)

    def test_loops_are_stopped_by_asking_for_no_volume(self):
        """loopsample() takes volume zero as stop; leaving them running would
        carry the arena's engines back into the race."""
        sound = read(SOUND)
        exit_body = sound[sound.index("void mecha_sound_exit(void)"):]
        exit_body = exit_body[: exit_body.index("\n}")]
        self.assertIn("loopsample(i, MECHA_SFX_ENGINE, 0, 0,", exit_body)
        self.assertIn("loopsample(i, MECHA_SFX_SKID, 0, 0,", exit_body)
        self.assertIn("stopmusic();", exit_body)

    def test_the_camera_basis_is_shared_rather_than_copied(self):
        """The sound layer needs the same axes the renderer uses; a second
        copy of that basis is a second thing to get wrong."""
        self.assertIn("void mecha_camera_basis(", read(RENDER_H))
        self.assertIn("mecha_camera_basis(pCamera,", read(SOUND))

    def test_every_entry_point_is_declared(self):
        header = read(SOUND_H)
        body = read(SOUND)
        for name in (
            "mecha_sound_enter",
            "mecha_sound_exit",
            "mecha_sound_briefing",
            "mecha_sound_match",
            "mecha_sound_update",
        ):
            self.assertIn(name, header, name)
            self.assertRegex(body, r"void\s+%s\s*\(" % re.escape(name))


if __name__ == "__main__":
    unittest.main()
