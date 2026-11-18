#!/usr/bin/env python3
"""Pico desktop hardware simulator.

The GUI emulates peripherals. All product decisions, pixels, packet decoding,
and saved-state encoding come from the same C++ core used by board firmware.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import messagebox, ttk
from typing import Any

from build_backend import BACKEND_BINARY, BUILD_DIR, build


DISPLAY_WIDTH = 128
DISPLAY_HEIGHT = 64
DISPLAY_SCALE = 4
DEFAULT_STATE_PATH = BUILD_DIR / "pico-sim-state.bin"


class BackendError(RuntimeError):
    pass


class BackendProcess:
    def __init__(self, executable: Path, state_path: Path, seed: int = 0x5049434F):
        self.process = subprocess.Popen(
            [str(executable), "--state", str(state_path), "--seed", str(seed)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.snapshot = self._read_snapshot()

    def _read_snapshot(self) -> dict[str, Any]:
        if self.process.stdout is None:
            raise BackendError("Simulator backend has no output stream")
        line = self.process.stdout.readline()
        if not line:
            details = ""
            if self.process.stderr is not None:
                details = self.process.stderr.read().strip()
            raise BackendError(details or "Simulator backend stopped unexpectedly")
        try:
            return json.loads(line)
        except json.JSONDecodeError as error:
            raise BackendError(f"Invalid response from simulator backend: {error}") from error

    def send(self, command: str) -> dict[str, Any]:
        if self.process.poll() is not None or self.process.stdin is None:
            raise BackendError("Simulator backend is not running")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        self.snapshot = self._read_snapshot()
        return self.snapshot

    def close(self) -> None:
        if self.process.poll() is None and self.process.stdin is not None:
            try:
                self.process.stdin.write("quit\n")
                self.process.stdin.flush()
                self.process.wait(timeout=1)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.terminate()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if stream is not None:
                stream.close()


def pixel_is_on(frame: bytes, x: int, y: int) -> bool:
    """Read the portable compositor's horizontal-MSB framebuffer."""
    stride = (DISPLAY_WIDTH + 7) // 8
    index = y * stride + x // 8
    return bool(frame[index] & (1 << (7 - x % 8)))


def format_duration(seconds: int) -> str:
    hours, remainder = divmod(seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    if hours:
        return f"{hours:d}:{minutes:02d}:{seconds:02d}"
    return f"{minutes:02d}:{seconds:02d}"


class PicoSimulatorApp:
    def __init__(self, root: tk.Tk, backend: BackendProcess):
        self.root = root
        self.backend = backend
        self.snapshot = backend.snapshot
        self.closed = False
        self._last_frame_hex: str | None = None
        self.display_image: tk.PhotoImage | None = None
        self.display_scaled: tk.PhotoImage | None = None
        self.mode_lookup: dict[str, int] = {}
        self.mood_lookup: dict[str, int] = {}

        self.root.title("Pico Hardware Simulator")
        self.root.minsize(940, 650)
        self.root.geometry("1120x760")
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self._configure_style()
        self._build_ui()
        self._load_catalog(self.snapshot)
        self._apply_snapshot(self.snapshot, log_event=True)
        self.root.bind("<space>", lambda _event: self._send("event PrimaryPressed"))
        self.root.bind("<Key-m>", lambda _event: self._send("event SecondaryPressed"))
        self.root.bind("<Key-M>", lambda _event: self._send("event SecondaryPressed"))
        self.root.after(100, self._clock_tick)

    def _configure_style(self) -> None:
        style = ttk.Style(self.root)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure("Title.TLabel", font=("TkDefaultFont", 17, "bold"))
        style.configure("Readout.TLabel", font=("TkFixedFont", 12, "bold"))
        style.configure("Primary.TButton", padding=(14, 10))
        style.configure("Secondary.TButton", padding=(12, 8))

    def _build_ui(self) -> None:
        outer = ttk.Frame(self.root, padding=14)
        outer.grid(row=0, column=0, sticky="nsew")
        self.root.rowconfigure(0, weight=1)
        self.root.columnconfigure(0, weight=1)
        outer.rowconfigure(1, weight=1)
        outer.columnconfigure(0, weight=3)
        outer.columnconfigure(1, weight=2)

        title_row = ttk.Frame(outer)
        title_row.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 10))
        title_row.columnconfigure(0, weight=1)
        ttk.Label(title_row, text="Pico Hardware Simulator", style="Title.TLabel").grid(
            row=0, column=0, sticky="w"
        )
        self.backend_label = ttk.Label(title_row, text="Shared firmware core", foreground="#4e8b64")
        self.backend_label.grid(row=0, column=1, sticky="e")

        self._build_device_panel(outer)
        self._build_control_panel(outer)

        self.footer_var = tk.StringVar(value="Ready")
        ttk.Label(outer, textvariable=self.footer_var, anchor="w").grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0)
        )

    def _build_device_panel(self, parent: ttk.Frame) -> None:
        panel = ttk.Frame(parent, padding=(0, 0, 12, 0))
        panel.grid(row=1, column=0, sticky="nsew")
        panel.rowconfigure(0, weight=1)
        panel.columnconfigure(0, weight=1)

        shell = tk.Frame(panel, background="#6f5278", padx=30, pady=24)
        shell.grid(row=0, column=0, sticky="n", pady=8)

        top = tk.Frame(shell, background="#6f5278")
        top.pack(fill="x", pady=(0, 12))
        tk.Label(
            top,
            text="PICO // PROTOTYPE",
            background="#6f5278",
            foreground="#eee5f2",
            font=("TkFixedFont", 10, "bold"),
        ).pack(side="left")
        self.led_canvas = tk.Canvas(
            top, width=22, height=22, background="#6f5278", highlightthickness=0
        )
        self.led_canvas.pack(side="right")
        self.led_item = self.led_canvas.create_oval(4, 4, 18, 18, fill="#293329", outline="#c4d1c5")

        bezel = tk.Frame(shell, background="#29232d", padx=10, pady=10)
        bezel.pack()
        self.oled = tk.Canvas(
            bezel,
            width=DISPLAY_WIDTH * DISPLAY_SCALE,
            height=DISPLAY_HEIGHT * DISPLAY_SCALE,
            background="#0c1710",
            highlightthickness=0,
        )
        self.oled.pack()
        self.oled_item = self.oled.create_image(0, 0, anchor="nw")

        button_row = tk.Frame(shell, background="#6f5278")
        button_row.pack(fill="x", pady=(18, 4))
        ttk.Button(
            button_row,
            text="Interact  [Space]",
            command=lambda: self._send("event PrimaryPressed"),
            style="Primary.TButton",
        ).pack(side="left", expand=True, padx=(0, 8))
        ttk.Button(
            button_row,
            text="Menu / Next [M]",
            command=lambda: self._send("event SecondaryPressed"),
            style="Secondary.TButton",
        ).pack(side="left", expand=True)

        navigation = ttk.Frame(shell)
        navigation.pack(fill="x")
        for label, event in (("D0 tap", "FocusButtonTapped"), ("D0 hold", "FocusButtonHeld"),
                             ("Previous", "NavigatePrevious"), ("Select", "Select"), ("Back", "Back")):
            ttk.Button(navigation, text=label,
                       command=lambda e=event: self._send("event " + e)).pack(side="left", padx=2)

        readout = ttk.Frame(panel, padding=(8, 10))
        readout.grid(row=1, column=0, sticky="ew")
        for column in range(4):
            readout.columnconfigure(column, weight=1)
        self.mode_var = tk.StringVar()
        self.mood_var = tk.StringVar()
        self.timer_var = tk.StringVar()
        self.interactions_var = tk.StringVar()
        for column, (label, variable) in enumerate(
            (
                ("MODE", self.mode_var),
                ("MOOD", self.mood_var),
                ("TIMER", self.timer_var),
                ("INTERACTIONS", self.interactions_var),
            )
        ):
            ttk.Label(readout, text=label).grid(row=0, column=column)
            ttk.Label(readout, textvariable=variable, style="Readout.TLabel").grid(
                row=1, column=column, pady=(3, 0)
            )

    def _build_control_panel(self, parent: ttk.Frame) -> None:
        notebook = ttk.Notebook(parent)
        notebook.grid(row=1, column=1, sticky="nsew")

        controls = ttk.Frame(notebook, padding=10)
        radio = ttk.Frame(notebook, padding=10)
        inspector = ttk.Frame(notebook, padding=10)
        notebook.add(controls, text="Hardware")
        notebook.add(radio, text="BLE")
        notebook.add(inspector, text="Inspector")

        self._build_hardware_controls(controls)
        self._build_ble_controls(radio)
        self._build_inspector(inspector)

    def _build_hardware_controls(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(0, weight=1)

        dock_box = ttk.LabelFrame(parent, text="Dock contacts", padding=10)
        dock_box.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        self.dock_var = tk.IntVar(value=0)
        for column, (name, value) in enumerate((("None", 0), ("Home", 1), ("Car", 2))):
            ttk.Radiobutton(
                dock_box,
                text=name,
                value=value,
                variable=self.dock_var,
                command=lambda selected=value: self._send(f"event DockChanged {selected}"),
            ).grid(row=0, column=column, padx=7)

        battery_box = ttk.LabelFrame(parent, text="Battery input", padding=10)
        battery_box.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        battery_box.columnconfigure(0, weight=1)
        self.battery_var = tk.IntVar(value=75)
        ttk.Scale(
            battery_box,
            from_=0,
            to=100,
            variable=self.battery_var,
            orient="horizontal",
        ).grid(row=0, column=0, sticky="ew", padx=(0, 8))
        ttk.Button(
            battery_box,
            text="Apply",
            command=lambda: self._send(f"event SetBattery {self.battery_var.get()}"),
        ).grid(row=0, column=1)
        ttk.Button(
            battery_box,
            text="Unknown",
            command=lambda: self._send("event SetBattery 255"),
        ).grid(row=0, column=2, padx=(6, 0))

        state_box = ttk.LabelFrame(parent, text="Direct state controls", padding=10)
        state_box.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        state_box.columnconfigure(0, weight=1)
        self.mode_choice = tk.StringVar()
        self.mode_combo = ttk.Combobox(state_box, textvariable=self.mode_choice, state="readonly")
        self.mode_combo.grid(row=0, column=0, sticky="ew")
        ttk.Button(state_box, text="Set mode", command=self._set_mode).grid(
            row=0, column=1, padx=(6, 0)
        )
        self.mood_choice = tk.StringVar()
        self.mood_combo = ttk.Combobox(state_box, textvariable=self.mood_choice, state="readonly")
        self.mood_combo.grid(row=1, column=0, sticky="ew", pady=(7, 0))
        ttk.Button(state_box, text="Set mood", command=self._set_mood).grid(
            row=1, column=1, padx=(6, 0), pady=(7, 0)
        )

        timer_box = ttk.LabelFrame(parent, text="Timer", padding=10)
        timer_box.grid(row=3, column=0, sticky="ew", pady=(0, 8))
        timer_box.columnconfigure(0, weight=1)
        self.timer_entry = ttk.Entry(timer_box)
        self.timer_entry.insert(0, "30")
        self.timer_entry.grid(row=0, column=0, sticky="ew")
        ttk.Label(timer_box, text="seconds").grid(row=0, column=1, padx=5)
        ttk.Button(timer_box, text="Start", command=self._start_timer).grid(row=0, column=2)
        ttk.Button(
            timer_box, text="Cancel", command=lambda: self._send("event CancelTimer")
        ).grid(row=0, column=3, padx=(6, 0))

        ttk.Button(timer_box, text="Pause", command=lambda: self._send("event PauseTimer")).grid(row=1, column=2)
        ttk.Button(timer_box, text="Resume", command=lambda: self._send("event ResumeTimer")).grid(row=1, column=3)
        ttk.Label(timer_box, text="Face theme (0-7)").grid(row=2, column=0)
        self.theme_var = tk.StringVar(value="0")
        theme_choice = ttk.Combobox(timer_box, textvariable=self.theme_var,
                                   values=tuple(str(i) for i in range(8)), width=4, state="readonly")
        theme_choice.grid(row=2, column=1)
        ttk.Button(timer_box, text="Apply theme", command=lambda: self._send(
            "event SetTheme " + self.theme_var.get())).grid(row=2, column=2, columnspan=2)

        ttk.Button(timer_box, text="Work / break", command=lambda: self._send("event StartFocusCycle")).grid(row=3, column=0)
        ttk.Button(timer_box, text="Next phase", command=lambda: self._send("event NextFocusPhase")).grid(row=3, column=1)
        ttk.Button(timer_box, text="Test cue", command=lambda: self._send("event TestSound 1")).grid(row=3, column=2)
        self.muted_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(timer_box, text="Mute", variable=self.muted_var, command=lambda: self._send(
            "event SetMuted " + str(int(self.muted_var.get())))).grid(row=3, column=3)

        time_box = ttk.LabelFrame(parent, text="Simulation clock", padding=10)
        time_box.grid(row=4, column=0, sticky="ew", pady=(0, 8))
        self.paused_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(time_box, text="Paused", variable=self.paused_var).pack(side="left")
        self.speed_var = tk.StringVar(value="1×")
        ttk.Combobox(
            time_box,
            textvariable=self.speed_var,
            values=("1×", "10×", "60×"),
            width=6,
            state="readonly",
        ).pack(side="left", padx=8)
        ttk.Button(time_box, text="+1 second", command=lambda: self._send("advance 1")).pack(
            side="left"
        )

        power_box = ttk.LabelFrame(parent, text="Power and flash", padding=10)
        power_box.grid(row=5, column=0, sticky="ew")
        ttk.Button(power_box, text="Power cycle", command=lambda: self._send("power_cycle")).pack(
            side="left"
        )
        ttk.Button(power_box, text="Factory reset", command=self._factory_reset).pack(
            side="left", padx=(7, 0)
        )
        self.storage_var = tk.StringVar(value="Flash idle")
        ttk.Label(power_box, textvariable=self.storage_var).pack(side="right")

    def _build_ble_controls(self, parent: ttk.Frame) -> None:
        parent.columnconfigure(0, weight=1)
        connection_box = ttk.LabelFrame(parent, text="Radio link", padding=10)
        connection_box.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        self.connection_var = tk.StringVar(value="Disconnected")
        ttk.Label(connection_box, textvariable=self.connection_var).pack(side="left")
        self.connection_button = ttk.Button(connection_box, text="Connect", command=self._toggle_ble)
        self.connection_button.pack(side="right")

        command_box = ttk.LabelFrame(parent, text="Raw command characteristic", padding=10)
        command_box.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        command_box.columnconfigure(0, weight=1)
        self.ble_entry = ttk.Entry(command_box, font="TkFixedFont")
        self.ble_entry.insert(0, "01")
        self.ble_entry.grid(row=0, column=0, sticky="ew")
        ttk.Button(command_box, text="Write", command=self._send_ble).grid(
            row=0, column=1, padx=(6, 0)
        )
        ttk.Label(
            command_box,
            text="Examples: 01 interact  •  04 1e 00 00 00 start 30 s  •  06 status",
            wraplength=390,
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(7, 0))

        status_box = ttk.LabelFrame(parent, text="16-byte status characteristic", padding=10)
        status_box.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        status_box.columnconfigure(0, weight=1)
        self.status_hex_var = tk.StringVar()
        ttk.Entry(
            status_box,
            textvariable=self.status_hex_var,
            state="readonly",
            font="TkFixedFont",
        ).grid(row=0, column=0, sticky="ew")
        ttk.Button(status_box, text="Refresh", command=lambda: self._send("status")).grid(
            row=0, column=1, padx=(6, 0)
        )

        event_box = ttk.LabelFrame(parent, text="Developer event injection", padding=10)
        event_box.grid(row=3, column=0, sticky="ew")
        event_box.columnconfigure(0, weight=1)
        self.raw_event = ttk.Entry(event_box, font="TkFixedFont")
        self.raw_event.insert(0, "RequestStatus 0")
        self.raw_event.grid(row=0, column=0, sticky="ew")
        ttk.Button(event_box, text="Inject", command=self._inject_event).grid(
            row=0, column=1, padx=(6, 0)
        )
        ttk.Label(
            event_box,
            text="Use an EventKind name or numeric ID, followed by its value.",
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(7, 0))

    def _build_inspector(self, parent: ttk.Frame) -> None:
        parent.rowconfigure(0, weight=2)
        parent.rowconfigure(1, weight=1)
        parent.columnconfigure(0, weight=1)

        self.inspector = ttk.Treeview(parent, columns=("value",), show="tree headings")
        self.inspector.heading("#0", text="State field")
        self.inspector.heading("value", text="Value")
        self.inspector.column("#0", width=180)
        self.inspector.column("value", width=170)
        self.inspector.grid(row=0, column=0, sticky="nsew")

        log_box = ttk.LabelFrame(parent, text="Event / effect trace", padding=6)
        log_box.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        log_box.rowconfigure(0, weight=1)
        log_box.columnconfigure(0, weight=1)
        self.log = tk.Text(log_box, height=10, wrap="word", state="disabled", font="TkFixedFont")
        self.log.grid(row=0, column=0, sticky="nsew")

    def _load_catalog(self, snapshot: dict[str, Any]) -> None:
        modes = [item for item in snapshot["catalog"]["modes"] if item["name"] != "driving"]
        moods = snapshot["catalog"]["moods"]
        self.mode_lookup = {item["name"].title(): item["id"] for item in modes}
        self.mood_lookup = {item["name"].title(): item["id"] for item in moods}
        self.mode_combo.configure(values=tuple(self.mode_lookup))
        self.mood_combo.configure(values=tuple(self.mood_lookup))
        self.mode_choice.set(next(iter(self.mode_lookup)))
        self.mood_choice.set(next(iter(self.mood_lookup)))

    def _send(self, command: str) -> None:
        try:
            snapshot = self.backend.send(command)
        except BackendError as error:
            messagebox.showerror("Pico simulator", str(error))
            return
        self._apply_snapshot(snapshot, log_event=True)

    def _apply_snapshot(self, snapshot: dict[str, Any], *, log_event: bool) -> None:
        self.snapshot = snapshot
        state = snapshot["state"]
        self.mode_var.set(state["mode_name"].upper())
        self.mood_var.set(state["mood_name"].upper())
        self.muted_var.set(state.get("sound_muted", False))
        self.timer_var.set(format_duration(state["timer_remaining_seconds"]) + (" PAUSED" if state.get("timer_paused") else ""))
        self.interactions_var.set(str(state["interaction_count"]))
        self.dock_var.set(state["dock"])
        if state["battery_percent"] != 255:
            self.battery_var.set(state["battery_percent"])

        connected = state["connected"]
        self.connection_var.set("Encrypted link connected" if connected else "Advertising / disconnected")
        self.connection_button.configure(text="Disconnect" if connected else "Connect")
        self.status_hex_var.set(snapshot["status_hex"])
        self.led_canvas.itemconfigure(
            self.led_item,
            fill="#7dff8b" if snapshot["led_on"] else "#293329",
        )
        self.storage_var.set(
            f"Flash pending → t={snapshot['save_due_seconds']} s"
            if snapshot["storage_pending"]
            else "Flash idle"
        )
        self._render_display(snapshot["frame_hex"])
        self._update_inspector(snapshot)

        effects = snapshot.get("effect_names", [])
        effect_text = " + ".join(effects) if effects else "no effects"
        message = snapshot.get("message") or ""
        self.footer_var.set(
            f"{snapshot['event']} → {effect_text}" + (f" — {message}" if message else "")
        )

        if snapshot.get("chirped"):
            self._play_chirp()
        if log_event and (
            snapshot["event"] != "Advance"
            or effects
            or snapshot.get("storage_saved")
        ):
            self._append_log(snapshot)

    def _render_display(self, frame_hex: str) -> None:
        if frame_hex == self._last_frame_hex:
            return
        self._last_frame_hex = frame_hex
        try:
            frame = bytes.fromhex(frame_hex)
        except ValueError:
            return
        expected = DISPLAY_WIDTH * DISPLAY_HEIGHT // 8
        if len(frame) != expected:
            return

        image = tk.PhotoImage(width=DISPLAY_WIDTH, height=DISPLAY_HEIGHT)
        rows = []
        for y in range(DISPLAY_HEIGHT):
            colors = [
                "#a5ffbc" if pixel_is_on(frame, x, y) else "#0c1710"
                for x in range(DISPLAY_WIDTH)
            ]
            rows.append("{" + " ".join(colors) + "}")
        image.put(" ".join(rows))
        self.display_image = image
        self.display_scaled = image.zoom(DISPLAY_SCALE, DISPLAY_SCALE)
        self.oled.itemconfigure(self.oled_item, image=self.display_scaled)

    def _update_inspector(self, snapshot: dict[str, Any]) -> None:
        for item in self.inspector.get_children():
            self.inspector.delete(item)
        for key, value in snapshot["state"].items():
            self.inspector.insert("", "end", text=key, values=(value,))
        self.inspector.insert("", "end", text="effects", values=(snapshot["effects"],))
        self.inspector.insert("", "end", text="capabilities", values=(snapshot["capabilities"],))
        self.inspector.insert("", "end", text="storage_pending", values=(snapshot["storage_pending"],))

    def _append_log(self, snapshot: dict[str, Any]) -> None:
        effects = ", ".join(snapshot.get("effect_names", [])) or "None"
        marker = "✓" if snapshot.get("ok", True) else "✕"
        line = (
            f"{marker} t={snapshot['state']['uptime_seconds']:>5}  "
            f"{snapshot['event']:<20} → {effects}"
        )
        if snapshot.get("storage_saved"):
            line += "  [flash saved]"
        if snapshot.get("message"):
            line += f"\n  {snapshot['message']}"
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _play_chirp(self) -> None:
        self.footer_var.set(self.footer_var.get() + "  ♪ 2.4 kHz / 45 ms")
        if os.name == "nt":
            def beep() -> None:
                try:
                    import winsound

                    winsound.Beep(2400, 45)
                except (ImportError, RuntimeError):
                    pass

            threading.Thread(target=beep, daemon=True).start()
        else:
            self.root.bell()

    def _set_mode(self) -> None:
        selected = self.mode_lookup.get(self.mode_choice.get())
        if selected is not None:
            self._send(f"event SetMode {selected}")

    def _set_mood(self) -> None:
        selected = self.mood_lookup.get(self.mood_choice.get())
        if selected is not None:
            self._send(f"event SetMood {selected}")

    def _start_timer(self) -> None:
        try:
            seconds = int(self.timer_entry.get())
        except ValueError:
            messagebox.showwarning("Timer", "Enter a whole number of seconds.")
            return
        self._send(f"event StartTimer {seconds}")

    def _toggle_ble(self) -> None:
        command = "event Disconnected" if self.snapshot["state"]["connected"] else "event Connected"
        self._send(command)

    def _send_ble(self) -> None:
        self._send("ble " + self.ble_entry.get())

    def _inject_event(self) -> None:
        value = self.raw_event.get().strip()
        if value:
            self._send("event " + value)

    def _factory_reset(self) -> None:
        if messagebox.askyesno(
            "Factory reset Pico",
            "Clear the simulated flash record and reboot Pico?",
        ):
            self._send("factory_reset")

    def _clock_tick(self) -> None:
        if self.closed:
            return
        if not self.paused_var.get():
            speed = {"1×": 1, "10×": 10, "60×": 60}.get(self.speed_var.get(), 1)
            self._send(f"advance_ms {speed * 100}")
        self.root.after(100, self._clock_tick)

    def close(self) -> None:
        self.closed = True
        self.backend.close()
        self.root.destroy()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Pico desktop hardware simulator")
    parser.add_argument("--state", type=Path, default=DEFAULT_STATE_PATH)
    parser.add_argument("--rebuild", action="store_true")
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0x5049434F)
    parser.add_argument("--backend", type=Path, help="use an already-built backend")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        executable = args.backend or build(force=args.rebuild)
        backend = BackendProcess(executable, args.state.resolve(), args.seed)
    except (BackendError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Could not start Pico simulator: {error}", file=sys.stderr)
        return 1

    root = tk.Tk()
    PicoSimulatorApp(root, backend)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
