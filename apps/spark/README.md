# Spark home

Spark uses the internal app name `home`. The router registers this app in place
of the vendor Home menu. `app.c` opens the root page, and `view.c` shows `hello`
with the shared label widget. The framework owns and destroys the page objects.

Language selection and the optional guide still run before Home. System status
bars, connection overlays, notifications, and calls keep their existing behavior.
The page has no app launcher or swipe actions. Other app routes remain available
to phone commands.

The shared status bar is at the top, with the page content below it. The
disconnect overlay also keeps its status bar at the top. Battery images and
clock visibility follow the existing system behavior.

The first page uses the existing `jytek` resources. A custom product overlay is
not needed until Spark has its own images or strings.

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

A rebuild refreshes `jyt_d/` and resets setup. Restarting the existing executable
without a rebuild retains the saved setup state.

Run `simulator/FloatairSimulator/simulator_event_panel.py` with a Python that has
Tk support. On first launch, use Long Press to confirm English, then Host
Connected to dismiss the connection overlay. Slide Forward and Slide Backward
must leave `hello` visible. Calls and Host Disconnected must still show their
normal popups. Restarting must retain the selected language.

The simulator window's TCP status refers to the phone test server. The event
panel can simulate a Host connection while that TCP server is disconnected.
