import unittest

from bcm import BCM, BCMConfig, Ignition, Inputs, LightSwitch, Mode

DT = 0.02
NIGHT = 5.0
DAY = 1000.0


def run(bcm: BCM, inp: Inputs, seconds: float) -> None:
    for _ in range(round(seconds / DT)):
        bcm.step(DT, inp)


def driving(lux: float) -> tuple[BCM, Inputs]:
    """Carro em movimento: ignição ligada, comando em AUTO."""
    bcm = BCM()
    inp = Inputs(ignition=Ignition.ON, light_switch=LightSwitch.AUTO, ambient_lux=lux)
    run(bcm, inp, 1.0)
    return bcm, inp


class ComingHomeTests(unittest.TestCase):
    def test_activates_on_ignition_off_at_night(self):
        bcm, inp = driving(NIGHT)
        inp.ignition = Ignition.OFF
        run(bcm, inp, 1.0)
        self.assertEqual(bcm.mode, Mode.COMING_HOME)
        self.assertEqual(bcm.out.low_beam, 1.0)

    def test_not_activated_in_daylight(self):
        bcm, inp = driving(DAY)
        inp.ignition = Ignition.OFF
        run(bcm, inp, 1.0)
        self.assertEqual(bcm.mode, Mode.OFF)

    def test_times_out_and_fades(self):
        bcm, inp = driving(NIGHT)
        inp.ignition = Ignition.OFF
        run(bcm, inp, bcm.cfg.coming_home_time_s - 1)
        self.assertEqual(bcm.mode, Mode.COMING_HOME)
        run(bcm, inp, 1.1)
        self.assertEqual(bcm.mode, Mode.OFF)
        self.assertGreater(bcm.out.low_beam, 0.0)  # ainda em fade
        run(bcm, inp, bcm.cfg.exterior_fade_out_s)
        self.assertEqual(bcm.out.low_beam, 0.0)

    def test_open_door_holds_timer(self):
        bcm, inp = driving(NIGHT)
        inp.ignition = Ignition.OFF
        inp.doors[0] = True
        run(bcm, inp, 200)
        self.assertEqual(bcm.mode, Mode.COMING_HOME)
        self.assertAlmostEqual(bcm.mode_timer, bcm.cfg.coming_home_time_s)

    def test_lock_restarts_timer(self):
        bcm, inp = driving(NIGHT)
        inp.ignition = Ignition.OFF
        run(bcm, inp, 30)
        bcm.press_lock()
        run(bcm, inp, DT)
        self.assertTrue(bcm.locked)
        self.assertAlmostEqual(bcm.mode_timer, bcm.cfg.coming_home_time_s, delta=0.05)

    def test_high_beam_flash_triggers_even_in_daylight(self):
        bcm, inp = driving(DAY)
        inp.ignition = Ignition.OFF
        run(bcm, inp, 3)
        bcm.flash_high_beam()
        run(bcm, inp, DT)
        self.assertEqual(bcm.mode, Mode.COMING_HOME)

    def test_flash_outside_window_ignored(self):
        bcm, inp = driving(DAY)
        inp.ignition = Ignition.OFF
        run(bcm, inp, bcm.cfg.coming_home_flash_window_s + 1)
        bcm.flash_high_beam()
        run(bcm, inp, DT)
        self.assertEqual(bcm.mode, Mode.OFF)

    def test_low_battery_blocks(self):
        bcm, inp = driving(NIGHT)
        inp.battery_v = 11.0
        inp.ignition = Ignition.OFF
        run(bcm, inp, 1)
        self.assertEqual(bcm.mode, Mode.OFF)


class LeavingHomeTests(unittest.TestCase):
    def parked(self, lux: float) -> tuple[BCM, Inputs]:
        bcm = BCM()
        inp = Inputs(ambient_lux=lux)
        run(bcm, inp, 1)
        return bcm, inp

    def test_unlock_at_night(self):
        bcm, inp = self.parked(NIGHT)
        bcm.press_unlock()
        run(bcm, inp, 1)
        self.assertEqual(bcm.mode, Mode.LEAVING_HOME)
        self.assertEqual(bcm.out.puddle, 1.0)
        self.assertEqual(bcm.out.interior, 1.0)

    def test_unlock_in_daylight_only_interior(self):
        bcm, inp = self.parked(DAY)
        bcm.press_unlock()
        run(bcm, inp, 1)
        self.assertEqual(bcm.mode, Mode.OFF)
        self.assertEqual(bcm.out.low_beam, 0.0)
        self.assertEqual(bcm.out.interior, 1.0)

    def test_lock_cancels(self):
        bcm, inp = self.parked(NIGHT)
        bcm.press_unlock()
        run(bcm, inp, 2)
        bcm.press_lock()
        run(bcm, inp, DT)
        self.assertEqual(bcm.mode, Mode.OFF)

    def test_ignition_on_goes_normal(self):
        bcm, inp = self.parked(NIGHT)
        bcm.press_unlock()
        run(bcm, inp, 2)
        inp.ignition = Ignition.ON
        run(bcm, inp, 1)
        self.assertEqual(bcm.mode, Mode.NORMAL)
        self.assertEqual(bcm.out.low_beam, 1.0)  # AUTO + escuro

    def test_auto_relock_when_no_door_opened(self):
        bcm, inp = self.parked(NIGHT)
        bcm.press_unlock()
        run(bcm, inp, bcm.cfg.auto_relock_time_s + 0.1)
        self.assertTrue(bcm.locked)
        self.assertEqual(bcm.mode, Mode.OFF)

    def test_door_open_cancels_relock(self):
        bcm, inp = self.parked(NIGHT)
        bcm.press_unlock()
        run(bcm, inp, 2)
        inp.doors[0] = True
        run(bcm, inp, 1)
        inp.doors[0] = False
        run(bcm, inp, bcm.cfg.auto_relock_time_s + 1)
        self.assertFalse(bcm.locked)


class MiscTests(unittest.TestCase):
    def test_unlock_flashes_twice(self):
        bcm = BCM()
        inp = Inputs()
        run(bcm, inp, 0.5)
        bcm.press_unlock()
        rising = 0
        prev = False
        for _ in range(200):
            bcm.step(DT, inp)
            if bcm.out.indicators and not prev:
                rising += 1
            prev = bcm.out.indicators
        self.assertEqual(rising, 2)

    def test_lock_refused_with_door_open(self):
        bcm = BCM()
        inp = Inputs()
        bcm.press_unlock()
        run(bcm, inp, 0.1)
        inp.doors[1] = True
        bcm.press_lock()
        run(bcm, inp, 0.1)
        self.assertFalse(bcm.locked)

    def test_darkness_hysteresis_and_debounce(self):
        bcm = BCM()
        inp = Inputs(ambient_lux=DAY)
        run(bcm, inp, 1)
        inp.ambient_lux = 100  # entre os limiares: não muda
        run(bcm, inp, 5)
        self.assertFalse(bcm.dark)
        inp.ambient_lux = 10
        run(bcm, inp, bcm.cfg.dark_debounce_s - 0.5)
        self.assertFalse(bcm.dark)
        run(bcm, inp, 1)
        self.assertTrue(bcm.dark)
        inp.ambient_lux = 100
        run(bcm, inp, 5)
        self.assertTrue(bcm.dark)

    def test_battery_saver_cuts_park_lamps(self):
        cfg = BCMConfig(battery_saver_time_s=20)
        bcm = BCM(cfg)
        inp = Inputs(light_switch=LightSwitch.PARK, ambient_lux=DAY)
        run(bcm, inp, 10)
        self.assertEqual(bcm.out.park_lamps, 1.0)
        run(bcm, inp, 15)
        self.assertTrue(bcm.saver_tripped)
        self.assertEqual(bcm.out.park_lamps, 0.0)

    def test_interior_delay_after_door_close(self):
        bcm = BCM()
        inp = Inputs(ambient_lux=DAY)
        inp.doors[0] = True
        run(bcm, inp, 2)
        inp.doors[0] = False
        run(bcm, inp, bcm.cfg.interior_delay_s - 1)
        self.assertEqual(bcm.out.interior, 1.0)
        run(bcm, inp, 1 + bcm.cfg.interior_fade_s + 0.1)
        self.assertEqual(bcm.out.interior, 0.0)


if __name__ == "__main__":
    unittest.main()
