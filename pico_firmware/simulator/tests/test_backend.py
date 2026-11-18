#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SIMULATOR_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SIMULATOR_DIR))

from build_backend import build  # noqa: E402
from pico_sim import format_duration, pixel_is_on  # noqa: E402


class Harness:
    def __init__(self, executable: Path, state_path: Path):
        self.process = subprocess.Popen(
            [str(executable), "--state", str(state_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.initial = self._read()

    def _read(self) -> dict:
        assert self.process.stdout is not None
        line = self.process.stdout.readline()
        if not line:
            assert self.process.stderr is not None
            raise AssertionError(self.process.stderr.read())
        return json.loads(line)

    def send(self, command: str) -> dict:
        assert self.process.stdin is not None
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        return self._read()

    def close(self) -> None:
        if self.process.poll() is None:
            assert self.process.stdin is not None
            self.process.stdin.write("quit\n")
            self.process.stdin.flush()
            self.process.wait(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if stream is not None:
                stream.close()


class BackendTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.executable = build(force=True)

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.harness = Harness(
            self.executable, Path(self.temporary.name) / "pico-state.bin"
        )

    def tearDown(self) -> None:
        self.harness.close()
        self.temporary.cleanup()

    def test_boot_and_interaction_use_real_core_and_renderer(self) -> None:
        boot = self.harness.initial
        self.assertEqual(boot["state"]["mode_name"], "companion")
        self.assertEqual(boot["effect_names"], ["Render", "Notify"])
        self.assertEqual(len(bytes.fromhex(boot["frame_hex"])), 1024)

        interaction = self.harness.send("event PrimaryPressed")
        self.assertEqual(interaction["state"]["mood_name"], "happy")
        self.assertEqual(interaction["state"]["interaction_count"], 1)
        self.assertEqual(
            interaction["effect_names"], ["Render", "Notify", "Persist", "Chirp"]
        )
        self.assertNotEqual(interaction["frame_hex"], boot["frame_hex"])

        reset = self.harness.send("advance 4")
        self.assertEqual(reset["state"]["mood_name"], "neutral")

    def test_car_dock_owns_driving_and_restores_previous_mode(self) -> None:
        self.harness.send("event SetMode 2")
        docked = self.harness.send("event DockChanged 2")
        self.assertEqual(docked["state"]["mode_name"], "driving")

        refused = self.harness.send("event SetMode 1")
        self.assertEqual(refused["state"]["mode_name"], "driving")
        self.assertEqual(refused["effects"], 0)

        undocked = self.harness.send("event DockChanged 0")
        self.assertEqual(undocked["state"]["mode_name"], "music")

    def test_raw_ble_command_uses_production_decoder(self) -> None:
        timer = self.harness.send("ble 04 05 00 00 00")
        self.assertTrue(timer["ok"])
        self.assertTrue(timer["state"]["timer_running"])
        self.assertEqual(timer["state"]["timer_remaining_seconds"], 5)

        finished = self.harness.send("advance 5")
        self.assertFalse(finished["state"]["timer_running"])
        self.assertEqual(finished["state"]["mood_name"], "alert")
        self.assertTrue(finished["chirped"])

        rejected = self.harness.send("ble 04 00 00 00 00")
        self.assertFalse(rejected["ok"])
        self.assertIn("invalid-value", rejected["message"])

    def test_delayed_flash_save_survives_power_cycle(self) -> None:
        interaction = self.harness.send("event PrimaryPressed")
        self.assertTrue(interaction["storage_pending"])

        before_flush = self.harness.send("power_cycle")
        self.assertEqual(before_flush["state"]["interaction_count"], 0)

        self.harness.send("event PrimaryPressed")
        flushed = self.harness.send("advance 5")
        self.assertTrue(flushed["storage_saved"])
        self.assertFalse(flushed["storage_pending"])

        restored = self.harness.send("power_cycle")
        self.assertEqual(restored["state"]["interaction_count"], 1)
        self.assertEqual(restored["state"]["mood_name"], "neutral")

    def test_status_packet_and_generic_event_injection_remain_available(self) -> None:
        connected = self.harness.send("event Connected")
        status = bytes.fromhex(connected["status_hex"])
        self.assertEqual(len(status), 16)
        self.assertEqual(status[0], 1)
        self.assertEqual(status[4] & 0x01, 1)

        future_event = self.harness.send("event 200 99")
        self.assertTrue(future_event["ok"])
        self.assertEqual(future_event["effects"], 0)

    def test_sprint_themes_sleep_blink_and_wake(self):
        frames = set()
        for theme in range(8):
            snap = self.harness.send(f"ble 07 {theme:02x}")
            self.assertEqual(snap["state"]["theme"], theme)
            frames.add(snap["frame_hex"])
        self.assertEqual(len(frames), 8)
        saw_blink = False
        for _ in range(100):
            snap = self.harness.send("advance_ms 100")
            saw_blink |= snap["state"]["eyes_closed"]
            self.assertFalse(snap["persist_queued"])
        self.assertTrue(saw_blink)
        sleepy = self.harness.send("advance 300")
        self.assertEqual(sleepy["state"]["mood_name"], "sleepy")
        wake = self.harness.send("ble 01")
        self.assertEqual(wake["state"]["mood_name"], "happy")

    def test_sprint_pause_resume_completion_and_menu(self):
        self.harness.send("ble 10")
        selected = self.harness.send("event FocusButtonTapped")
        self.assertEqual(selected["state"]["screen"], 2)
        self.harness.send("ble 0a 0a 00 00 00")
        self.harness.send("ble 13")
        self.harness.send("advance 3")
        paused = self.harness.send("ble 08")
        self.assertTrue(paused["state"]["timer_paused"])
        self.assertEqual(bytes.fromhex(paused["status_hex"])[4] & 4, 4)
        still = self.harness.send("advance 50")
        self.assertEqual(still["state"]["timer_remaining_seconds"], 7)
        self.harness.send("ble 09")
        finished = self.harness.send("advance 7")
        self.assertEqual(finished["state"]["mood_name"], "alert")
        self.assertTrue(finished["chirped"])
        home = self.harness.send("advance 4")
        self.assertEqual(home["state"]["screen"], 0)
        self.harness.send("ble 04 05 00 00 00")
        cancelled = self.harness.send("ble 05")
        self.assertFalse(cancelled["state"]["timer_running"])

    def test_sprint_durable_settings_and_companion_boot(self):
        self.harness.send("ble 07 04")
        self.harness.send("ble 0a 2c 01 00 00")
        self.harness.send("event FocusButtonTapped")
        self.harness.send("advance 5")
        reboot = self.harness.send("power_cycle")
        self.assertEqual(reboot["state"]["theme"], 4)
        self.assertEqual(reboot["state"]["configured_duration_seconds"], 300)
        self.assertEqual(reboot["state"]["mode_name"], "companion")
        self.assertFalse(reboot["state"]["timer_running"])



class UiHelperTests(unittest.TestCase):
    def test_duration_format(self) -> None:
        self.assertEqual(format_duration(65), "01:05")
        self.assertEqual(format_duration(3661), "1:01:01")

    def test_framebuffer_pixel_decode(self) -> None:
        frame = bytearray(1024)
        frame[0] = 0x80
        self.assertTrue(pixel_is_on(frame, 0, 0))
        self.assertFalse(pixel_is_on(frame, 1, 0))


if __name__ == "__main__":
    unittest.main(verbosity=2)
