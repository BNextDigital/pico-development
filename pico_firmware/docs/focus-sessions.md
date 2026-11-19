# Focus sessions and since-boot history

Focus now has a local round goal, a clearer countdown/progress view, and a
four-entry session history. It reuses the existing Timer, confirmed Work/Break
sequencing, duration editors, optional sound cues and OLED wake policy.

## Local controls

Open Focus from the icon menu. Navigate cycles eight actions, shown one at a time below the large countdown
and short progress context ([pocket UI](pocket-ui.md)):

| Index | Action | Behavior |
| --- | --- | --- |
| 0 | Work Duration | Existing one-minute preview editor |
| 1 | Start Timer / Pause / Resume | Existing single timer or active timer control |
| 2 | Cancel | Stop timer/session; retain completed work in history |
| 3 | Home | Return to the face while timing continues |
| 4 | Start Cycle / Next Phase | Start Work/Break session or confirm the waiting phase |
| 5 | Break Duration | Existing one-minute preview editor |
| 6 | Round Goal | Continuous (0), or 1–12 work rounds, with Save/Cancel |
| 7 | History | Browse four most recent recorded sessions |

Goal applies to Start Cycle, not Start Timer. Continuous remains the default,
preserving existing single-timer and BLE Work/Break behavior. Goal is a local
since-boot choice; it resets to Continuous on reboot. Work and break durations
continue to use durable settings. Changing goal during a running session only
changes the next session; the current goal is snapshotted at start.

Each non-final Work completion waits for explicit confirmation before Break.
Each Break completion waits before the next Work. Pausing, waiting and breaks
never count as completed work time. A bounded session ends immediately after
its final Work round, without an extra final break. It wakes the OLED for the
existing completion alert, then opens History after the temporary expression.
The caller cannot advance an already finished session into another phase.

Navigate browses history; Action or Back returns to Focus. History records are
newest first and show work rounds, fully completed work time, and Finished or
Stopped Early. Sessions cancelled before completing any Work are omitted.
Cancelling, replacing the cycle with a single timer, changing mode or entering
the car dock records completed Work once before ending the cycle. Unfinished
partial rounds are excluded. Merely visiting Home/menu/history does not end it.
Single timers do not create session-history entries.

The screen explicitly says THIS BOOT: this is bounded RAM history, not a daily
streak, lifetime statistic or crash-resistant journal. Neither it nor the goal
is written to flash. Reboot clears them; durable preferences and interaction
counts are preserved. Persistent history needs a separately scoped storage and
retention design; calendar summaries also need a reliable time source.

## Implementation and compatibility

FocusSession remains a sequencer without a clock. It snapshots goal, counts
completed Work and saturates accumulated completed-work seconds. Core passes
the actual completed timer's total, so edits to duration mid-round do not
rewrite elapsed work. The same Timer handles pause/resume/cancel and completion.
Core owns a fixed four-record array and archives exactly once per finish/stop.
No per-tick persistence, allocation, extra worker or periodic wake was added.

Screen FocusSummary and SettingKey FocusGoal append existing IDs. Existing mode
IDs, command IDs, UUIDs, status packet and persistence v5 stay unchanged. The
existing StartFocusCycle/NextFocusPhase commands honor the locally selected
goal, while a fresh boot retains continuous compatibility. No app-side goal or
history protocol is introduced. Simulator event injection adds SetFocusGoal
(0–12) and OpenFocusSummary, and JSON exposes goal, active goal, completed work
time, finished state and bounded history. The GUI uses the production renderer
and existing virtual buttons.

## Validation

Run `bash tests/host/run.sh` and `bash simulator/run-tests.sh`. Coverage includes
bounded finish without a final break, continuous compatibility, pause/wait/break
time exclusion, goal snapshot, invalid goals, preview/cancel/save/return paths,
exactly-once archival, cancellation/mode/car exits, four-record eviction,
clock-driven completion, saturation and reboot semantics. Existing menu tests
are updated for the eight-action Focus menu. Cooking retains its four actions.

Local validation passes: host regression and GPIO/display adapter models, all
29 simulator tests, ASan/UBSan core run (leak detection disabled), packed asset
regeneration and whitespace checks. Framebuffer previews were inspected.

Host/simulator validation does not establish target compilation or physical
OLED readability/timing. Power bench work is deferred by request. Use the
existing SDK 3.4.2 XIAO profile and wiring; no erase/bond reset is needed.

## Demo

1. Open Focus, set Work to one minute and Break to one minute.
2. Save Round Goal = 2, then choose Start Cycle.
3. Pause/resume; finish Work, confirm Break, finish Break, confirm Work.
4. Finish the second Work. Confirm the alert, no extra Break and automatic
   summary showing 2 rounds / 2 minutes of completed work.
5. Start another cycle, complete one Work and cancel. History should show a
   Stopped Early record above the finished one. Browse, Back, restart.
6. Reboot: history/goal reset, saved durations/theme/OLED/audio settings remain.
