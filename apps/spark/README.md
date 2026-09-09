# Spark home

Spark uses the internal app name `home`. The router registers this app in place
of the vendor Home menu. `app.c` opens the root page. `view.c` keeps a frame,
header, item label, and list labels for display requests. The frame starts hidden.
The framework owns and destroys the page objects.

Startup selects English (`en-US`) and opens Spark. The language selection and
guide apps are not registered, and guide commands are not handled. Saved setup
progress does not block head gestures, sleep, notifications, or phone commands.
System status bars, connection overlays, notifications, and calls remain available.
Head-up and head-down control load as disabled. Touchpad long press toggles the
screen before page or popup input. Double-tap remains available to the active
page while the screen is on and does not wake the screen. Screen state is reported
to the phone, which starts glasses capture on screen on and stops it on screen
off. The footer avatar is visible while the screen is on and shows a static
audio-wave mark only after the phone reports `SystemStatus setListenState` 1,
so it shows real capture rather than the screen state.
The page has no app launcher. Swipes scroll an overflowing footer reply. Other
app routes remain available to phone commands.

The shared status bar is at the top, with the page content below it. The
disconnect overlay also keeps its status bar at the top. Battery images and
clock visibility follow the existing system behavior.

The first page uses the existing `jytek` resources. A custom product overlay is
not needed until Spark has its own images or strings.

## Display pages

The server returns the turn's final item set and focus item. The last changed
surviving item receives focus; a later explicit display call replaces the set
and selects its first item. When no tool changes the display, the server omits
it from the response. The phone prepares all compact cards. The glasses pack
complete rows into a 256 px content area while parsing the list.
Rows use 40 px (reminder), 64 px (note, email, event), or 88 px (event with location),
with 8 px gaps and at most five visible rows. Dates and sender details are
formatted on the phone. The glasses store the complete list (up to 20 rows) in
RAM and calculate page boundaries from row heights. Only five row controls are allocated.
The focused item determines the visible page. Artifact payloads stay on the phone.

The phone sends reliable MessagePack on app ID `1`, business `Display`, command
`update`. `revision` identifies an immutable request:

```json
{"id":1,"payload":{"seq":7,"type":128,"biz":"Display","cmd":"update","data":{"revision":"7","displayID":"result-1","navigationSequence":"0","reply":"Done","page":{"kind":"list","title":"Notes","hint":"1 item","selected":0,"rowGap":8,"rows":[{"id":"note-1","mark":"","primary":"Plan","secondary":"Meet at 10:00","meta":"","layout":"note","height":64}]}}}}
```

`page` and `reply` are independent optional fields. Omission keeps the current
value. An empty `rows` array in a list page clears the page. An empty reply
clears the footer. At least one field must be present; explicit null is invalid.
The phone compares prepared content against its last ACK and omits unchanged
fields. After uncertain delivery, the next new request replaces both fields.

`kind` selects the fixed `list` or `item` template. A list accepts zero through
20 rows. An item accepts exactly one row. Detail views use the page title and
hint, followed by the row's primary, secondary, and meta text. Wrapped fields
are positioned from their measured height. List pages use a short header so
all five rows fit. LVGL objects remain allocated across replacements.

The receiver validates all supplied fields before applying either update.
Only the three detail labels borrow strings owned by the current display;
list and header labels copy text because ellipsis can modify the label buffer.
Replacing a display detaches old detail strings before releasing them. Reply-only updates do not touch page
labels, layout, or scroll position.

The ACK contains `data: {"batch_id":"7"}`. A retry uses the same revision and
payload. The receiver ACKs the current revision without parsing the page or
redrawing it; it does not compare payloads. Reusing a revision for a different
request violates the sender contract. Older revisions receive `ErrSeqErr`.
ACK confirms UI state, not physical display completion. Leaving Spark or
losing the connection clears the page, reply, and accepted revision.

Limits: canonical UInt64 decimal revisions; 20 stored rows; five visible rows; unique nonempty row IDs
up to 256 UTF-8 bytes; each display text field up to 4096 bytes; replies up to
65534 bytes per field, subject to a 48 KiB request budget. The receiver uses
MessagePack's parsed message size and validates string limits before copying.
The phone checks encoded JSON presentation size and reserves
512 bytes for request fields in the total budget. List text is capped
at 96 characters and 256 UTF-8 bytes per field; detail text keeps its existing limit.
Selection is a zero-based index into the full list, or zero for an empty list. UTF-8 with embedded NUL is
rejected. Spark must already be active.

List rows include `layout` (`reminder`, `note`, `email`, `event`) and `height`.
Nonempty lists include `rowGap: 8`. Page offsets and counts are derived from
the row heights and five-row limit, and are not sent. The phone uses the same
packing rule to report the visible page to the server.
Email rows can include `address` and `subject`; `subject` is the emphasized prefix
of `secondary`. Name/address/time share line one. Subject/preview share line two.
Calendar rows are independent cards: title, date/time range, optional location.
No stars or time ruler are drawn. List text uses single-line ellipsis; the source
strings stay unchanged. Selection dims the other rows without a white highlight.
When only selection changes, the renderer updates the two affected rows and
keeps the text buffers and geometry. Crossing a page boundary reuses the same
controls with the next rows. Detail text is requested from the phone on Open.

### Input

List swipes select and render locally, stopping at either end of the complete
list. Then `selected` reports the absolute `artifact_id`. A lost report does
not block navigation; the next selection report restores the phone mirror.
The phone updates its mirror without sending the list back.

A click opens the selected row locally and reports `open`. A connected phone
can replace that preview with fuller detail for the same display ID. The
glasses keep the list and restore it locally on double click, then report
`selected`. A new display ID replaces the content and clears the stored return
list. Detail swipes still scroll text.

Reports use app ID `30003` and include `revision`, `displayID`, and an increasing
decimal-string `navigationSequence`. The display ID remains stable across
reply updates and detail navigation. A new server display gets a new ID.
The phone rejects older sequences and reports from other displays. A later
selection report can also recover a lost Back report. The phone keeps only the
newest report while a send is pending. An `opened` report
describes an already visible detail and does not request another send.

Updates echo the latest report sequence. For the same display ID, an older
sequence cannot replace a view after a local swipe or Back. The receiver ACKs
that request and reports the actual view, including on a retry. The phone
continues to report only the visible page to the server. The existing footer
scroll and popup priorities remain.

### Offline checks

```sh
python3 tests/run_spark_display_tests.py --artifacts /tmp/spark-display-tests
```

This compiles the receiver, parser, view, and test with sanitizers and links
cached native LVGL objects; it does not build or flash firmware. It checks
independent updates, retry ACKs, malformed input, string ownership, five-row
geometry, 20-row navigation, local Back, delayed detail responses, wrapped
detail layout, input reports, lifecycle, and partial redraws.
The optional artifact directory receives `list.ppm` and `item.ppm`.

## Footer reply

`Display.update` can set the footer without replacing the page.
Replies use Open Runde Medium at 14 pixels. The reply UI loads one bitmap file
from ROMFS into SRAM during initialization and keeps it resident. There is no
runtime font or size selector. Missing glyphs use the system font. Other UI text
keeps its existing font. Source, conversion instructions, and the OFL license
are in `fonts/`. UI command cycles log render timing even below the slow-cycle
threshold.

The footer keeps the ball at the lower left and wraps reply text to its right.
The footer itself stays 72 pixels tall. The bubble is opaque and grows upward
over the page, so the page container and active page keep their size and are
not redrawn for a reply. The bubble is sized from one text measurement and
stays below the status bar. At the screen limit, the reply scrolls while the
ball stays fixed. A new reply starts at the top; repeated text keeps its scroll
position. Empty text hides the bubble.

After a build, check short, multiline, Unicode, and screen-length replies with
a display replacement. Check forward/backward scrolling, repeated text, and
empty text. Check that missing or non-string `reply` receives a NAK without
changing the display. Check that calls and notifications still receive input
before the reply.

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
must leave the empty Spark frame hidden. Calls and Host Disconnected must still show their
normal popups. Use Spark Reply to preview or clear reply text without a phone
connection. Use Spark Display to preview each supported list or full item with
sample data. Mixed previews use the same card heights and page header. Restarting must use English
without a setup screen.

The simulator window's TCP status refers to the phone test server. The event
panel can simulate a Host connection while that TCP server is disconnected.
