# Spark home

Spark uses the internal app name `home`. The router registers this app in place
of the vendor Home menu. `app.c` opens the root page, and `view.c` shows `hello`
with the shared label widget. The framework owns and destroys the page objects.

Startup selects English (`en-US`) and opens Spark. The language selection and
guide apps are not registered, and guide commands are not handled. Saved setup
progress does not block head gestures, sleep, notifications, or phone commands.
System status bars, connection overlays, notifications, and calls remain available.
Head-up and head-down control load as disabled. Touchpad long press toggles the
screen before page or popup input. Double-tap remains available to the active
page while the screen is on and does not wake the screen. Screen state drives
the footer avatar and the phone's mic capture.
The page has no app launcher. Swipes scroll an overflowing footer reply. Other
app routes remain available to phone commands.

The shared status bar is at the top, with the page content below it. The
disconnect overlay also keeps its status bar at the top. Battery images and
clock visibility follow the existing system behavior.

The first page uses the existing `jytek` resources. A custom product overlay is
not needed until Spark has its own images or strings.

## Assistant replies

The phone sends a reliable MessagePack request on app ID `1`:

```json
{"id":1,"payload":{"seq":1,"type":128,"biz":"Spark","cmd":"setReply","data":{"text":"Your reply"}}}
```

`seq` is the application request sequence. Reply data requires `text`.
The handler validates UTF-8 and rejects embedded NUL bytes. The label copies
the text before the handler sends the normal ACK. Invalid requests receive a
NAK and leave the current reply unchanged. Existing screen-off and popup
command restrictions still apply. Spark must be the active app.

Replies use Open Runde Medium at 14 pixels. The reply UI loads one bitmap file
from ROMFS into SRAM during initialization and keeps it resident. There is no
runtime font or size selector. Missing glyphs use the system font. Other UI text
keeps its existing font. Source, conversion instructions, and the OFL license
are in `fonts/`. Each accepted reply logs its sequence and byte count. UI command
cycles log render timing even below the slow-cycle threshold.

The footer keeps the ball at the lower left and wraps reply text to its right.
The footer itself stays 72 pixels tall. The bubble is opaque and grows upward
over the page, so the page container and active page keep their size and are
not redrawn for a reply. The bubble is sized from one text measurement and
stays below the status bar. At the screen limit, the reply scrolls while the
ball stays fixed. A new reply starts at the top; repeated text keeps its scroll
position. Empty text hides the bubble. Disconnecting or leaving Spark clears
the reply.

After a build, check short, multiline, Unicode, and screen-length replies.
Check forward/backward scrolling, repeated text, and empty text. Check that
missing or non-string `text` receives a NAK without changing the display.
Check that calls and notifications still receive input before the reply.

## Simulator

From the firmware repository root:

```sh
bash simulator/FloatairSimulator/develop-simulator.sh /path/to/os_sdk.7z --product jytek
```

After the SDK is cached, omit the archive argument. To rebuild incrementally:

```sh
cmake --build simulator/FloatairSimulator/build-macos-llvm
cd simulator/FloatairSimulator/build-macos-llvm
./floatair_simulator
```

A rebuild refreshes `jyt_d/`. Startup and factory reset select English and disable
the guides, including when an older saved configuration has unfinished setup.

Run `simulator/FloatairSimulator/simulator_event_panel.py` with a Python that has
Tk support. On first launch, English is already selected. Use Host Connected to
dismiss the connection overlay. Slide Forward and Slide Backward
must leave `hello` visible. Calls and Host Disconnected must still show their
normal popups. Use Spark Reply to preview or clear reply text without a phone
connection. Restarting must use English without a setup screen.

The simulator window's TCP status refers to the phone test server. The event
panel can simulate a Host connection while that TCP server is disconnected.
