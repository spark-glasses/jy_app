# Spark home

Spark uses the internal app name `home`. The router registers this app in place
of the vendor Home menu. `app.c` opens the root page. `view.c` keeps the frame,
header, content, and footer and dispatches to one body module per page kind
(`body_list.c`, `body_detail.c`, `body_grid.c`, `body_doc.c`), each of which
owns its labels and lays them out from measured text heights. `display.c`
parses typed items; `body_list.c` and `body_detail.c` draw each type's list card
and detail page. `navigation.c`
holds the current and return-list displays, selection, open, and Back.
`reply.c` owns the footer reply panel and `fonts.c` the bitmap font cache. The
frame starts hidden. The framework owns and destroys the page objects.

Startup selects English (`en-US`) and opens Spark. The language selection and
guide apps are not registered, and guide commands are not handled. Saved setup
progress does not block head gestures, sleep, notifications, or phone commands.
System status bars, connection overlays, notifications, and calls remain available.
Head-up and head-down control load as disabled. Touchpad long press toggles the
screen before page or popup input. Double-tap remains available to the active
page while the screen is on and does not wake the screen. Screen state is reported
to the phone, which starts glasses capture on screen on and stops it on screen
off. All Spark text uses the resident Open Runde bitmap fonts (12, 14, 16, and
18 px); the vendor TTF font only fills in missing glyphs. Reply
text is owned by Spark and is drawn in an app-local overlay. The page has no app launcher. The reply
shows at most three lines and ends longer text with an ellipsis. Other
app routes remain available to phone commands.

The shared status bar is at the top, with the page content below it. The
disconnect overlay also keeps its status bar at the top. It reserves 25 pixels,
uses 14-pixel text, and uses 16-pixel battery images. Clock visibility follows
the existing system behavior.

The first page uses the existing `jytek` resources. A custom product overlay is
not needed until Spark has its own images or strings.

## Display pages

The phone sends the items the glasses draw. Each item carries its artifact type
and that type's fields, with dates and senders already written out on the
phone; the glasses decide how each type looks. The phone sends the same fields
back to the server as its display snapshot. The glasses store the complete list
(up to 20 items) in RAM and pack whole cards into a 256 px content area with
4 px gaps and at most five visible cards. Card heights are per type (see the
table below), and the phone pages lists with the same heights. Only five row
controls are allocated. The selected item determines the visible page.

The phone sends reliable MessagePack on app ID `1`, business `Display`, command
`update`. `revision` identifies an immutable request within one `displayID`:

```json
{"id":1,"payload":{"seq":7,"type":128,"biz":"Display","cmd":"update","data":{"revision":"7","displayID":"result-1","navigationSequence":"0","reply":"Done","page":{"title":"Notes","hint":"1 items","selected":0,"items":[{"id":"nt_1","type":"note","hasMore":false,"title":"Plan","content":"Meet at 10:00","date":"Sep 12, 2026"}]}}}}
```

`page`, `item`, `reply`, `assistant`, and `screen` are independent optional
fields; a request carries at most one of `page` and `item`. A list uses `page`:
`{title, hint, selected, items}`. One opened item uses `item`. `screen` accepts only `"off"` and must
arrive together with an empty page and an empty reply; anything else is
rejected with `ErrBadParam`. The receiver applies and paints the request, which
is the clear, then turns the screen off and reports the screen state with
trigger `phoneDismiss`, then ACKs. A retry of the same revision is ACKed without
touching the screen. A dismiss is never stale: it applies even when it echoes an
older navigation sequence. The phone sends it only on the dismiss action, so the
frame is blank when the screen next comes on. The glasses never turn the screen
on from a display update.

An item is `{id, type, hasMore, ...fields}`. Every key must belong to the type,
every required field must be present, and every value must have its kind;
anything else is rejected with `ErrBadParam`. Fields in brackets are optional.

| Type | Fields | List card |
|---|---|---|
| `todo` | `content`, `completed` (bool), [`due`], [`repeat`] | 40 px: checkbox, text, due date on the right |
| `note` | `title`, `content`, [`date`] | 48 px: title; date, then content |
| `email` | `sender`, [`address`], `subject`, `preview`, `time`, `sentAt` | 60 px: sender, address, time; subject, then preview |
| `email_draft` | `to`, `from`, `subject`, `body`, `status` | 60 px: recipients and status; subject, then body |
| `calendar_event` | `title`, `when`, [`location`], [`response`] | 60 px: title; time |
| `contact` | `name`, [`organization`], [`jobTitle`], [`phone`], [`email`] | 48 px: name; job title and organization |
| `places` | `name`, [`address`], [`rating`], [`open`] | 48 px: name; open, then address |
| `route` | `title`, [`via`], [`duration`], [`distance`], [`mode`] | 48 px: title; duration, then distance and via |
| `card` | `blocks`, or `headers` and `rows` | 48 px: its first two lines |

A card's `blocks` are `[[tag, text], ...]`: one through 24 blocks whose tag is
`h` (heading) or `p` (paragraph) with non-empty text. A grid card has two or
three `headers` (empty strings allowed) and one through twenty `rows`, each an
array with exactly one string cell per header. A card never carries both.

Each line of a list card is one line: line breaks read as spaces and long text
ends with an ellipsis. An opened item shows its type's detail page: a header
with the type's title (Reminder for a todo, the subject for mail, the name for
a contact or place, otherwise the title) and "More available" when `hasMore` is
set, then a lead line, a wrapping body, and a grey line under it, each filled
by the type. A card's detail is its text or grid with no header band.

Omission keeps the current value. An empty `items` array in a list page clears the page. An empty reply
clears the footer. At least one field must be present; explicit null is
invalid. The phone compares prepared content against its last ACK and omits
unchanged fields. After uncertain delivery, the next new request replaces both
fields.

The receiver renders an accepted update before it ACKs, so the ACK means the
change is on screen. Swipes and Back paint the same way. Painting goes through
`system_ui_paint_now`, which draws nothing while the screen is off and hands the
request to the SDL main thread on the simulator. Spark never asks the system
shell for a full-screen refresh while it is active.

Wrapped detail fields are positioned from their measured height. A card has no
header band, and its body starts at the frame top. A grid divides the content width into equal columns with a 16 px gap; headings
use the 18 px font over a rule, cells use the body font and wrap inside their
column, and each row is as tall as its tallest cell. Headings and cells are
both Open Runde, at 18 and 14 px. A doc stacks one full-width wrapped label
per block: headings in 16 px with 10 px above, paragraphs in 14 px with 6 px
between blocks. Docs scroll like a detail body. Grids scroll like a
detail body. List pages use a short header so all five rows fit. LVGL objects
remain allocated across replacements, including the 3 heading and 60 cell
labels of the grid and the 24 block labels of the doc.

The receiver validates all supplied fields before applying either update.
Only the grid cells and the doc blocks borrow strings owned by the current
display; list, header, and detail labels copy text because ellipsis can modify
the label buffer and some lines are joined from several fields. Replacing a
display detaches old cell and block strings before releasing them.
Reply-only updates do not replace page data or reset page scroll position.

The ACK contains `data: {"batch_id":"7"}`. A retry uses the same `displayID`,
revision, and payload. The receiver ACKs it without parsing the page or
redrawing it; it does not compare payloads. Reusing a revision for a different
request in the same display violates the sender contract. Older revisions in
the same display receive `ErrSeqErr`. A different `displayID` starts a new
revision sequence and must include a page. ACK confirms UI state, not physical
display completion. Leaving Spark clears the page, reply, and accepted revision.

Limits: canonical UInt64 decimal revisions; 20 stored items; five visible cards;
unique nonempty item IDs up to 256 UTF-8 bytes; each item text field up to 4096
bytes; list title and hint and grid headers and cells up to 256 bytes; replies
up to 65534 bytes per field, subject to a 48 KiB request budget. The receiver
uses MessagePack's parsed message size and validates string limits before
copying. The phone caps item text at 600 characters and 768 UTF-8 bytes, checks
the encoded size, and reserves 512 bytes for request fields in the total
budget. Selection is a zero-based index into the full list, or zero for an
empty list. UTF-8 with embedded NUL is rejected. Spark must already be active.

Page offsets and counts are derived from the card heights and five-card limit,
and are not sent. The phone uses the same packing rule to report the visible
page to the server. No stars or time ruler are drawn. Selection dims the other
cards without a white highlight. When only selection changes, the renderer
updates the two affected cards and keeps the text buffers and geometry.
Crossing a page boundary reuses the same controls with the next items. The
phone sends the item again on Open.

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
decimal-string `navigationSequence`. `selected`, `open`, and `opened` carry
`artifact_id`; `dismissed` and `pageDismissed` do not. `dismissed` clears the
page and reply. A corrective `pageDismissed` keeps a newer reply. The display ID remains stable across
reply updates and detail navigation. A new server display gets a new ID.
The phone rejects older sequences and reports from other displays. A later
selection report can also recover a lost Back report. The phone keeps only the
newest report while a send is pending. An `opened` report
describes an already visible detail and does not request another send.

Updates echo the latest report sequence. For the same display ID, an older
sequence cannot replace a view after a local swipe, Back, or timeout clear. The
receiver ACKs that request and reports the actual view (`selected`, `opened`,
`dismissed`, or `pageDismissed`), including on a retry and on reply-only updates,
so a lost report heals on the next exchange. The phone continues to report only the
visible page to the server. Vendor popup input priority remains unchanged.

### Content lifetime

The page and reply live until an explicit dismiss, three minutes of screen off,
or a host disconnect. The glasses alone decide the timeout: Spark installs the
system screen-state listener, which fires on every change regardless of the
current page, records the monotonic time on screen off, and on screen on clears
the page, return list, and reply when three minutes have passed. The on
notification arrives while the LCD is still dark, so the clear paints before
the screen lights up. The clear keeps the display ID, revision, and assistant
state, bumps the navigation sequence, and sends a `dismissed` report without
`artifact_id`. The phone applies it to its
mirror like a swipe and never runs a second clock. Any update accepted while
the screen is off restarts the clock, so content that arrived in the dark is
shown at the next screen on. A screen on with nothing to clear sends nothing.
On the host disconnect event the glasses run the full clear, revision
included, and the phone drops its retained content, so a reconnect always
starts a new empty display on both sides.

### Offline checks

```sh
python3 tests/run_spark_display_tests.py --artifacts /tmp/spark-display-tests
```

This compiles the receiver, parser, view, and test with sanitizers and links
cached native LVGL objects; it does not build or flash firmware. It checks
independent updates, retry ACKs, malformed and non-strict items, string
ownership, five-card geometry, per-type cards and detail pages, 20-item
navigation, local Back, delayed detail responses, wrapped detail layout, input
reports, lifecycle, and partial redraws. The optional artifact directory
receives a frame per check, including `types-1.ppm` to `types-3.ppm` and a
`detail-<type>.ppm` for each type.

## Reply overlay

`Display.update` can set the reply without replacing the page. Replies use Open
Runde Medium at 14 pixels. Spark loads the bitmap fonts from ROMFS and uses the
vendor system font only as a glyph fallback. Source, conversion instructions, and the OFL
license are in `fonts/`.

The reply panel is an opaque Spark page overlay without a border or corner
radius. Its text starts close to the assistant avatar and has room for three full
14-pixel-font lines. The reply and detail body use an 18-pixel row pitch. The
reply reserves three rows and reduces the main widget height by the same space.
The assistant avatar is vertically centered against those rows. The panel is a
fixed three lines high; longer text ends with an ellipsis and does not scroll.
Empty text hides the panel.

After a build, check short, multiline, Unicode, and screen-length replies with
a display replacement. Check repeated text and empty text. Check that missing or non-string `reply` receives a NAK without
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
connection. Use Spark Display to preview each type's list or full item with
sample data. Card Text is a text card (headings, numbered steps, and bullets);
Grid is a grid card. Mixed shows every type in turn, its cards alternating text
and grid. Spark Reply has a 30-word sample. Restarting must use English
without a setup screen.

The simulator window's TCP status refers to the phone test server. The event
panel can simulate a Host connection while that TCP server is disconnected.
