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

    def test_root_icon_menu_cycles_and_selects_music(self) -> None:
        first = self.harness.send("event OpenMenu")
        frames = [first["frame_hex"]]
        for index in range(1, 9):
            current = self.harness.send("event NavigateNext")
            self.assertEqual(current["state"]["menu_index"], index)
            frames.append(current["frame_hex"])
        self.assertEqual(len(set(frames)), 9)
        wrapped = self.harness.send("event NavigateNext")
        self.assertEqual(wrapped["frame_hex"], first["frame_hex"])
        for _ in range(3):
            self.harness.send("event NavigateNext")
        selected = self.harness.send("event Select")
        self.assertEqual(selected["state"]["screen"], 5)
        returned = self.harness.send("event Back")
        self.assertEqual(returned["state"]["menu_index"], 3)
        self.assertEqual(returned["frame_hex"], frames[3])

    def test_core_deadlines_follow_expression_and_metronome(self) -> None:
        self.assertEqual(self.harness.initial["next_core_wake_ms"], 1000)
        self.harness.send("event PrimaryPressed")
        before = self.harness.send("advance_ms 3999")
        self.assertEqual(before["state"]["mood_name"], "happy")
        self.assertEqual(before["next_core_wake_ms"], 1)
        after = self.harness.send("advance_ms 1")
        self.assertEqual(after["state"]["mood_name"], "neutral")
        music = self.harness.send("event StartMetronome")
        self.assertEqual(music["next_core_wake_ms"], 80)
        pulse_end = self.harness.send("advance_ms 80")
        self.assertEqual(pulse_end["next_core_wake_ms"], 420)
        beat = self.harness.send("advance_ms 420")
        self.assertEqual(beat["next_core_wake_ms"], 80)

    def test_oled_preview_sleep_alert_and_restart(self) -> None:
        self.harness.send("event OpenSettings")
        for _ in range(7):
            self.harness.send("event NavigateNext")
        self.harness.send("event Select")
        preview = self.harness.send("event Select")
        self.assertEqual(preview["state"]["display_preview_percent"], 55)
        self.assertEqual(preview["state"]["display_brightness_percent"], 50)
        self.harness.send("event Back")
        cancelled = self.harness.send("status")
        self.assertEqual(cancelled["state"]["display_preview_percent"], 50)
        self.harness.send("event SetBrightness 75")
        self.harness.send("event SetDisplayTimeout 1")
        asleep = self.harness.send("advance 60")
        self.assertTrue(asleep["state"]["display_asleep"])
        self.assertFalse(any(bytes.fromhex(asleep["frame_hex"])))
        wake = self.harness.send("event Select")
        self.assertFalse(wake["state"]["display_asleep"])
        self.assertEqual(wake["state"]["screen"], 8)  # wake does not open editor
        self.harness.send("event StartTimer 1")
        self.harness.send("event SleepDisplay")
        alert = self.harness.send("advance 1")
        self.assertEqual(alert["state"]["mood_name"], "alert")
        self.assertFalse(alert["state"]["display_asleep"])
        self.assertTrue(any(bytes.fromhex(alert["frame_hex"])))
        self.harness.send("advance 5")
        reboot = self.harness.send("power_cycle")
        self.assertEqual(reboot["state"]["display_brightness_percent"], 75)
        self.assertEqual(reboot["state"]["display_timeout_minutes"], 1)
        self.assertFalse(reboot["state"]["display_asleep"])

    def test_v4_oled_defaults_migrate_without_losing_preferences(self) -> None:
        self.harness.send("event SetTheme 4")
        self.harness.send("event SetVolume 70")
        self.harness.send("event SetBrightness 80")
        self.harness.send("advance 5")
        self.harness.close()
        path = Path(self.temporary.name) / "pico-state.bin"
        legacy = bytearray(path.read_bytes()[:23]) + bytearray([0])
        legacy[4] = 4
        checksum = 2166136261
        for byte in legacy:
            checksum = ((checksum ^ byte) * 16777619) & 0xFFFFFFFF
        legacy += checksum.to_bytes(4, "little")
        path.write_bytes(legacy)
        self.harness = Harness(self.executable, path)
        restored = self.harness.initial["state"]
        self.assertEqual(restored["theme"], 4)
        self.assertEqual(restored["sound_volume_percent"], 70)
        self.assertEqual(restored["display_brightness_percent"], 50)
        self.assertEqual(restored["display_timeout_minutes"], 0)
        self.harness.send("event SetBrightness 75")
        self.harness.send("advance 5")
        self.assertEqual(len(path.read_bytes()), 32)

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

    def test_work_break_cycle_ble_and_button_confirmation(self):
        self.harness.send("ble 0a 02 00 00 00")
        self.harness.send("ble 22 01 00 00 00")
        work = self.harness.send("ble 20")
        self.assertEqual(work["state"]["focus_phase"], 1)
        done = self.harness.send("advance 2")
        self.assertTrue(done["state"]["focus_waiting"])
        self.assertEqual(done["state"]["sound_cue"], 2)
        self.assertIn("Chirp", done["effect_names"])
        self.assertEqual(done["state"]["completed_work_sessions"], 1)
        self.assertTrue(bytes.fromhex(done["status_hex"])[4] & 8)
        waiting = self.harness.send("advance 60")
        self.assertFalse(waiting["state"]["timer_running"])
        self.assertTrue(waiting["state"]["focus_waiting"])
        rest = self.harness.send("event FocusButtonTapped")
        self.assertEqual(rest["state"]["focus_phase"], 2)
        self.assertEqual(rest["state"]["timer_remaining_seconds"], 1)
        self.assertNotEqual(rest["frame_hex"], waiting["frame_hex"])
        self.harness.send("advance 1")
        again = self.harness.send("ble 21")
        self.assertEqual(again["state"]["focus_phase"], 1)
        self.assertEqual(again["state"]["timer_remaining_seconds"], 2)
        cancelled = self.harness.send("ble 05")
        self.assertEqual(cancelled["state"]["focus_phase"], 0)
        self.assertFalse(cancelled["state"]["timer_running"])

    def test_mute_and_sound_command_validation(self):
        self.harness.send("ble 24 01")
        muted = self.harness.send("ble 23 01")
        self.assertTrue(muted["state"]["sound_muted"])
        self.assertNotIn("Chirp", muted["effect_names"])
        self.assertTrue(bytes.fromhex(muted["status_hex"])[4] & 16)
        self.harness.send("ble 24 00")
        sound = self.harness.send("ble 23 03")
        self.assertIn("Chirp", sound["effect_names"])
        self.assertEqual(sound["state"]["sound_cue"], 3)
        for command in ("ble 23 04", "ble 24 02", "ble 22 00 00 00 00", "ble 20 00"):
            rejected = self.harness.send(command)
            self.assertFalse(rejected["ok"])

    def test_music_menu_visual_beats_and_stop(self):
        self.harness.send("ble 10")
        for _ in range(3):
            self.harness.send("ble 11")
        music = self.harness.send("event FocusButtonTapped")
        self.assertEqual(music["state"]["screen"], 5)
        self.assertEqual(music["state"]["metronome_bpm"], 120)
        first = self.harness.send("event FocusButtonTapped")
        self.assertTrue(first["state"]["metronome_running"])
        self.assertTrue(bytes.fromhex(first["status_hex"])[4] & 32)
        self.assertEqual(first["state"]["sound_cue"], 5)
        off = self.harness.send("advance_ms 80")
        self.assertFalse(off["state"]["beat_pulse"])
        self.assertNotEqual(first["frame_hex"], off["frame_hex"])
        beat = self.harness.send("advance_ms 420")
        self.assertEqual(beat["state"]["current_beat"], 2)
        self.assertEqual(beat["state"]["sound_cue"], 4)
        self.assertNotIn("Notify", beat["effect_names"])
        stopped = self.harness.send("event FocusButtonHeld")
        self.assertFalse(stopped["state"]["metronome_running"])
        self.assertEqual(stopped["state"]["screen"], 1)

    def test_music_tap_tempo_ble_validation_and_restart(self):
        self.harness.send("ble 34")
        self.harness.send("advance_ms 600")
        tap = self.harness.send("ble 34")
        self.assertEqual(tap["state"]["metronome_bpm"], 100)
        tempo = self.harness.send("ble 32 89 00")
        self.assertEqual(tempo["state"]["metronome_bpm"], 137)
        self.harness.send("ble 33 03")
        self.harness.send("ble 30")
        skipped = self.harness.send("advance 60")
        self.assertEqual(skipped["state"]["current_beat"], 3)  # 137 elapsed beats in 3/beat bars
        self.assertIn("Chirp", skipped["effect_names"])
        self.harness.send("ble 31")
        for command in ("ble 32 1d 00", "ble 32 f1 00", "ble 32 78", "ble 33 09", "ble 30 00"):
            self.assertFalse(self.harness.send(command)["ok"])
        reboot = self.harness.send("power_cycle")
        self.assertFalse(reboot["state"]["metronome_running"])
        self.assertEqual(reboot["state"]["metronome_bpm"], 120)
        self.assertEqual(reboot["state"]["beats_per_bar"], 4)

    def test_tuner_uses_pcm_detector_and_rejects_silence(self):
        start = self.harness.send("ble 35")
        self.assertTrue(start["state"]["tuner_running"])
        self.assertFalse(start["state"]["metronome_running"])
        self.assertEqual(start["state"]["screen"], 6)
        self.assertTrue(bytes.fromhex(start["status_hex"])[4] & 64)
        concert_a = self.harness.send("tone 440000")
        self.assertEqual(concert_a["state"]["tuner_signal"], 4)
        self.assertEqual(concert_a["state"]["tuner_note"], 69)
        self.assertLessEqual(abs(concert_a["state"]["tuner_cents"]), 2)
        self.assertNotEqual(start["frame_hex"], concert_a["frame_hex"])
        self.assertNotIn("Notify", concert_a["effect_names"])
        drop_c = self.harness.send("tone 65406")
        self.assertEqual(drop_c["state"]["tuner_note"], 36)
        silence = self.harness.send("tone 0")
        self.assertEqual(silence["state"]["tuner_signal"], 1)
        self.assertNotEqual(drop_c["frame_hex"], silence["frame_hex"])
        self.harness.send("ble 36")
        inactive = self.harness.send("tone 440000")
        self.assertFalse(inactive["state"]["tuner_running"])
        self.assertNotEqual(inactive["state"]["tuner_signal"], 4)

    def test_tuner_buttons_optional_mic_and_restart(self):
        self.harness.send("ble 10")
        for _ in range(3): self.harness.send("ble 11")
        self.harness.send("ble 13")  # Music, Start index 2
        for _ in range(3): self.harness.send("ble 11") # Tuner index 5
        tuner = self.harness.send("event FocusButtonTapped")
        self.assertTrue(tuner["state"]["tuner_running"])
        stop = self.harness.send("event FocusButtonTapped")
        self.assertFalse(stop["state"]["tuner_running"])
        self.harness.send("event FocusButtonTapped")
        back = self.harness.send("event FocusButtonHeld")
        self.assertFalse(back["state"]["tuner_running"])
        self.assertEqual(back["state"]["screen"], 1)
        self.harness.send("event MicrophoneReady 0")
        missing = self.harness.send("ble 35")
        self.assertFalse(missing["state"]["tuner_running"])
        self.assertEqual(missing["state"]["tuner_signal"], 5)
        reboot = self.harness.send("power_cycle")
        self.assertFalse(reboot["state"]["tuner_running"])
        self.assertEqual(reboot["state"]["mode_name"], "companion")
        self.assertTrue(reboot["state"]["microphone_ready"])
        self.assertFalse(self.harness.send("ble 35 00")["ok"])

    def test_motion_raw_traces_wake_react_and_rest(self):
        resting = self.harness.send("motion_test rest")
        self.assertEqual(resting["state"]["mood_name"], "sleepy")
        self.assertTrue(resting["state"]["motion_settled"])
        pickup = self.harness.send("motion_test pickup")
        self.assertEqual(pickup["state"]["mood_name"], "curious")
        self.assertEqual(pickup["state"]["interaction_count"], 0)
        self.assertFalse(pickup["state"]["timer_running"])
        self.assertFalse(pickup["storage_pending"])
        self.harness.send("advance 3")
        rock = self.harness.send("motion_test rock")
        self.assertEqual(rock["state"]["mood_name"], "happy")
        self.assertNotIn("Chirp",rock["effect_names"])
        self.assertEqual(self.harness.send("advance 3")["state"]["mood_name"],"neutral")

    def test_motion_disabled_menu_timer_and_unavailable(self):
        self.harness.send("ble 40 00")
        disabled = self.harness.send("motion_test rock")
        self.assertEqual(disabled["state"]["motion_config"],0)
        self.assertNotEqual(disabled["state"]["mood_name"],"happy")
        self.assertFalse(bytes.fromhex(disabled["status_hex"])[4] & 128)
        self.assertFalse(self.harness.send("ble 40 08")["ok"])
        self.harness.send("ble 40 07")
        self.harness.send("ble 10")
        menu = self.harness.send("motion_test rock")
        self.assertEqual(menu["state"]["screen"],1)
        self.assertNotEqual(menu["state"]["mood_name"],"happy")
        self.harness.send("ble 14")
        self.harness.send("ble 04 1e 00 00 00")
        timer = self.harness.send("motion_test rock")
        self.assertTrue(timer["state"]["timer_running"])
        self.assertNotEqual(timer["state"]["mood_name"],"happy")
        self.harness.send("ble 05")
        self.harness.send("event MotionReady 0")
        missing = self.harness.send("motion_test rock")
        self.assertFalse(missing["state"]["motion_ready"])
        self.assertFalse(bytes.fromhex(missing["status_hex"])[14] & 128)
        self.assertNotEqual(missing["state"]["mood_name"],"happy")
        reboot = self.harness.send("power_cycle")
        self.assertTrue(reboot["state"]["motion_ready"])
        self.assertTrue(bytes.fromhex(reboot["status_hex"])[14] & 128)
        self.assertEqual(reboot["state"]["motion_config"],7)
        self.assertEqual(reboot["state"]["mode_name"],"companion")

    def test_motion_impacts_and_gaps_do_not_create_gesture(self):
        impact = self.harness.send("motion_test bump")
        self.assertNotEqual(impact["state"]["mood_name"],"happy")
        self.harness.send("motion_test still")
        self.harness.send("motion 500 0 866 1000")  # invalid sample gap clears arming
        for _ in range(6): self.harness.send("motion -500 0 866 40")
        end = self.harness.send("motion 0 0 1000 40")
        self.assertNotEqual(end["state"]["mood_name"],"happy")
        self.assertFalse(self.harness.send("motion 9000 0 0 40")["ok"])

    def test_minute_duration_editor_confirm_cancel_and_break(self):
        self.harness.send("ble 10"); self.harness.send("ble 13")
        self.harness.send("ble 12")  # Duration
        opened = self.harness.send("event FocusButtonTapped")
        self.assertEqual(opened["state"]["screen"],7)
        self.assertEqual(opened["state"]["draft_duration_seconds"],1500)
        preview = self.harness.send("event FocusButtonTapped")
        self.assertEqual(preview["state"]["draft_duration_seconds"],1560)
        self.assertEqual(preview["state"]["configured_duration_seconds"],1500)
        self.assertNotIn("Persist",preview["effect_names"])
        self.assertNotEqual(opened["frame_hex"],preview["frame_hex"])
        cancelled = self.harness.send("event FocusButtonHeld")
        self.assertEqual(cancelled["state"]["screen"],2)
        self.assertEqual(cancelled["state"]["configured_duration_seconds"],1500)
        self.harness.send("ble 13"); self.harness.send("ble 13")
        self.harness.send("ble 11")  # Save
        saved = self.harness.send("ble 13")
        self.assertEqual(saved["state"]["configured_duration_seconds"],1560)
        self.harness.send("ble 12")  # Break, utility index 5
        self.harness.send("ble 13"); self.harness.send("ble 13")
        self.harness.send("ble 11")
        rest = self.harness.send("ble 13")
        self.assertEqual(rest["state"]["break_duration_seconds"],360)
        self.harness.send("advance 5")
        reboot = self.harness.send("power_cycle")
        self.assertEqual(reboot["state"]["configured_duration_seconds"],1560)
        self.assertEqual(reboot["state"]["break_duration_seconds"],360)  # durable v4 settings

    def test_settings_preview_cancel_and_durable_preferences(self):
        opened = self.harness.send("ble 15")
        self.assertEqual(opened["state"]["screen"],8)
        self.harness.send("ble 13") # Theme editor
        preview = self.harness.send("event FocusButtonTapped")
        self.assertEqual(preview["state"]["screen"],9)
        self.assertEqual(preview["state"]["setting_draft"],1)
        self.assertEqual(preview["state"]["theme"],0)
        self.assertNotIn("Persist",preview["effect_names"])
        self.harness.send("event Back")
        self.assertEqual(self.harness.send("ble 06")["state"]["theme"],0)
        self.harness.send("ble 13"); self.harness.send("ble 13") # preview again
        self.harness.send("ble 11"); self.harness.send("ble 13") # Save
        self.harness.send("ble 24 01")
        self.harness.send("ble 25 46")
        self.harness.send("ble 22 a4 01 00 00") # 420 s
        self.harness.send("ble 40 01")
        self.harness.send("advance 5")
        reboot = self.harness.send("power_cycle")
        self.assertEqual(reboot["state"]["theme"],1)
        self.assertTrue(reboot["state"]["sound_muted"])
        self.assertEqual(reboot["state"]["sound_volume_percent"],70)
        self.assertEqual(reboot["state"]["break_duration_seconds"],420)
        self.assertEqual(reboot["state"]["motion_config"],1)
        self.assertEqual(reboot["state"]["screen"],0)
        self.assertFalse(reboot["state"]["timer_running"])
        self.assertFalse(self.harness.send("ble 25 65")["ok"])

    def test_settings_duration_returns_to_settings_and_completion_wins(self):
        self.harness.send("ble 15")
        for _ in range(4): self.harness.send("ble 11") # Break row
        self.harness.send("ble 13")
        self.harness.send("ble 13") # +1 min
        self.harness.send("ble 11")
        saved = self.harness.send("ble 13")
        self.assertEqual(saved["state"]["screen"],8)
        self.assertEqual(saved["state"]["break_duration_seconds"],360)
        self.assertIn("Persist",saved["effect_names"])
        self.harness.send("ble 13") # open break again
        self.harness.send("ble 13") # preview 7 min
        self.harness.send("ble 04 01 00 00 00")
        done = self.harness.send("advance 1")
        self.assertEqual(done["state"]["screen"],0)
        self.assertEqual(done["state"]["break_duration_seconds"],360)
        self.assertEqual(done["state"]["mood_name"],"alert")

    def test_legacy_v3_record_migrates_with_settings_defaults(self):
        record = bytearray(b"PICO" + bytes([3,0,0,4]) + (42).to_bytes(4,"little") + (600).to_bytes(4,"little"))
        checksum=2166136261
        for value in record: checksum=((checksum ^ value)*16777619)&0xffffffff
        record.extend(checksum.to_bytes(4,"little"))
        (Path(self.temporary.name)/"pico-state.bin").write_bytes(record)
        restored = self.harness.send("power_cycle")
        self.assertEqual(restored["state"]["theme"],4)
        self.assertEqual(restored["state"]["interaction_count"],42)
        self.assertEqual(restored["state"]["configured_duration_seconds"],600)
        self.assertEqual(restored["state"]["break_duration_seconds"],300)
        self.assertEqual(restored["state"]["sound_volume_percent"],25)
        self.assertFalse(restored["state"]["sound_muted"])
        self.harness.send("ble 25 1e")
        self.harness.send("advance 5")
        self.assertEqual(len((Path(self.temporary.name)/"pico-state.bin").read_bytes()),32)

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
