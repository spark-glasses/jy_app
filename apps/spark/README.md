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
off. Detail body and reply text use the resident Open Runde bitmap font. Reply
text is owned by Spark and is drawn in an app-local overlay. The page has no app launcher. Swipes scroll an
overflowing reply before they change page selection. Other
app routes remain available to phone commands.

The shared status bar is at the top, with the page content below it. The
disconnect overlay also keeps its status bar at the top. It reserves 25 pixels,
uses 14-pixel text, and uses 16-pixel battery images. Clock visibility follows
the existing system behavior.

The first page uses the existing `jytek` resources. A custom product overlay is
not needed until Spark has its own images or strings.

## Display pages

The server returns the turn's final item set and focus item. The last changed
surviving item receives focus; a later explicit display call replaces the set
and selects its first item. When no tool changes the display, the server omits
it from the response. The phone prepares all compact cards. The glasses pack
complete rows into a 256 px content area while parsing the list.
Rows use 40 px (reminder), 48 px (note), or 60 px (email and event), with
4 px gaps and at most five visible rows. A note uses
its title on line 1, then its edited date and a smaller preview on line 2. Dates and sender details are
formatted on the phone. The glasses store the complete list (up to 20 rows) in
RAM and calculate page boundaries from row heights. Only five row controls are allocated.
The focused item determines the visible page. Artifact payloads stay on the phone.

The phone sends reliable MessagePack on app ID `1`, business `Display`, command
`update`. `revision` identifies an immutable request within one `displayID`:

```json
{"id":1,"payload":{"seq":7,"type":128,"biz":"Display","cmd":"update","data":{"revision":"7","displayID":"result-1","navigationSequence":"0","reply":"Done","page":{"kind":"list","title":"Notes","hint":"1 item","selected":0,"rowGap":4,"rows":[{"id":"note-1","mark":"","primary":"Plan","secondary":"Meet at 10:00","meta":"Sep 12, 2026","layout":"note","height":48}]}}}}
```

`page`, `item`, and `reply` are independent optional fields. Lists use `page`.
Details use the compact array `item`: `[id, title, hint, primary, body, meta]`.
The body is either a UTF-8 string or `[uncompressedBytes, deflateBase64]`. The phone
uses raw DEFLATE with Base64 only when the final JSON field is smaller. The receiver
also accepts the older full item page. Omission keeps the current value. An empty `rows` array in
a list page clears the page. An empty reply clears the footer. At least one
field must be present; explicit null is invalid. The phone compares prepared
content against its last ACK and omits unchanged fields. After uncertain
delivery, the next new request replaces both fields.

`kind` selects the fixed `list` or `item` template. A list accepts zero through
20 rows. An item accepts exactly one row. Detail views use the page title and
hint, followed by the row's primary, secondary, and meta text. Wrapped fields
are positioned from their measured height. List pages use a short header so
all five rows fit. LVGL objects remain allocated across replacements.

The receiver validates all supplied fields before applying either update.
Only the three detail labels borrow strings owned by the current display;
list and header labels copy text because ellipsis can modify the label buffer.
Replacing a display detaches old detail strings before releasing them.
Reply-only updates do not replace page data or reset page scroll position.

The ACK contains `data: {"batch_id":"7"}`. A retry uses the same `displayID`,
revision, and payload. The receiver ACKs it without parsing the page or
redrawing it; it does not compare payloads. Reusing a revision for a different
request in the same display violates the sender contract. Older revisions in
the same display receive `ErrSeqErr`. A different `displayID` starts a new
revision sequence and must include a page. ACK confirms UI state, not physical
display completion. Leaving Spark clears the page, reply, and accepted revision.

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
Nonempty lists include `rowGap: 4`. Page offsets and counts are derived from
the row heights and five-row limit, and are not sent. The phone uses the same
packing rule to report the visible page to the server.
Email rows can include `address` and `subject`; `subject` is the emphasized prefix
of `secondary`. Name/address/time share line one. Subject/preview share line two.
Calendar list rows contain the title and date/time range. The detail view contains the location.
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

A click reports `open` once for the selected row. More clicks in the same list
state are ignored until an item is applied or a list-navigation gesture occurs. A
connected phone can send the detail for the same display ID. The
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
continues to report only the visible page to the server. Vendor popup input
priority remains unchanged.

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

## Reply overlay

`Display.update` can set the reply without replacing the page. Replies use Open
Runde Medium at 14 pixels. Spark loads the bitmap font from ROMFS and uses the
vendor system font as a fallback. Source, conversion instructions, and the OFL
license are in `fonts/`.

The reply panel is an opaque Spark page overlay without a border or corner
radius. Its text starts close to the assistant avatar and has room for three full
14-pixel-font lines. The reply and detail body use an 18-pixel row pitch. The
reply reserves three rows and reduces the main widget height by the same space.
The assistant avatar is vertically centered against those rows. The panel grows
upward from the bottom and stays below the status bar. At the screen limit, the
reply scrolls. A new reply starts at the top. Empty text hides the panel.

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
