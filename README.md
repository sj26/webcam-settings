# webcam-settings

Keep webcam settings from resetting on macOS. A tiny native command, not
another webcam app.

For now, it does one thing: keep **Logitech UVC webcams at 50 Hz power-line
frequency** to reduce flicker under 50 Hz lighting. This is anti-flicker,
**not 50 fps**. It does not change resolution, frame rate, or exposure mode.

Webcams can forget this setting after reconnecting, waking, or opening a
video app. `webcam-settings` checks every two seconds and reapplies it only
when needed. No window, menu-bar icon, video capture, or third-party runtime.

## Install

### Homebrew

```sh
brew install sj26/tap/webcam-settings
brew services start webcam-settings
```

The formula builds from source. Homebrew manages the background service:

```sh
webcam-settings --status
brew services stop webcam-settings
brew uninstall webcam-settings
```

Use either the Homebrew service or the manual LaunchAgent below, not both.
To switch from a manual installation, run `make uninstall` first.

### From source

Requires macOS and Apple's Command Line Tools (`xcode-select --install`).
Run from your logged-in desktop account, **without sudo**:

```sh
git clone https://github.com/sj26/webcam-settings.git
cd webcam-settings
make install
```

This builds the command, installs it at `~/.local/bin/webcam-settings`, and
starts a user LaunchAgent named `com.sj26.webcam-settings`. It starts again at
login, and launchd restarts it if it exits. Installation uses your home
directory, not a hard-coded username. Re-run `make install` to update it.

No admin access or Camera permission grant was needed on the tested Mac.
The helper does not access video frames, disable macOS security, seize USB
interfaces, or detach drivers. macOS must already allow the USB accessory.

## Use

For a manual installation, use `~/.local/bin/webcam-settings` if
`~/.local/bin` is not on your `PATH`.

```sh
# Read settings without changing them.
webcam-settings --status

# Apply 50 Hz once.
webcam-settings

# Watch in the foreground instead of installing the LaunchAgent.
webcam-settings --watch
```

Example status output (the USB registry ID varies):

```text
C922 Pro Stream Webcam (USB registry 0x1001489fe):
  interface=0 unit=3 current=1 (1=50 Hz, 2=60 Hz) factory-default=2
```

`current=1` means 50 Hz is active. `factory-default=2` means the camera still
defaults to 60 Hz; it does **not** mean the current setting failed to change.
Exit codes are 0 for success, 1 for a control/access failure, 2 when no
matching camera is connected, and 64 for invalid arguments. `--status` can
succeed with any current value because it only reads.

To inspect the manually installed background helper (use `brew services info
webcam-settings` for Homebrew):

```sh
launchctl print gui/$(id -u)/com.sj26.webcam-settings
/usr/bin/log show --last 10m --style compact --predicate 'process == "webcam-settings"'
```

To stop it and remove the installed files:

```sh
make uninstall
```

Uninstalling leaves the camera's current setting alone. The source checkout
can be kept or deleted separately.

## Scope and limitations

- Matches **all connected Logitech USB VideoControl interfaces**, not one
  model, serial number, or port. Each camera must expose the standard UVC
  Processing Unit power-line-frequency control. A failure on one camera does
  not stop attempts on the others. Unsupported controls are reported, not
  replaced with vendor-specific guesses.
- **50 Hz only**, for now. Do not use it if you need 60 Hz or automatic
  anti-flicker. There is no settings file or general camera-control UI.
- Re-enumerates devices each time, so reconnects and transient errors can be
  retried. Each USB request has a one-second timeout. Normal correction is
  about two seconds, but scheduling, multiple cameras, and access errors can
  delay it. An app that keeps overwriting the value may cause brief flicker
  between corrections.
- Runs while you are logged in. It does not prevent sleep or run during it.
- Does **not** rewrite firmware or save to EEPROM. Standard UVC exposes
  GET_DEF but no SET_DEF/nonvolatile-save operation for this control. No
  supported Logitech persistent-default rewrite was found.
- Hardware-tested with a **C922 on Apple Silicon, macOS 27.0.1**. Other
  Logitech models and macOS versions are not yet hardware-verified. Internal
  FaceTime cameras and non-Logitech devices are left alone.

## Development and validation

```sh
make        # Build with system frameworks only.
make check  # Compiler warnings, Clang static analysis, and plist validation.
make clean
```

The C922 was read back at 50 Hz using an independent USB probe. Deliberate
changes to 60 Hz and disabled anti-flicker recovered within three seconds,
including after restarting the helper. Brightness, contrast, and exposure
mode stayed unchanged; macOS retained ownership of the video interfaces.
An injected inaccessible camera ahead of the real C922 did not prevent the
C922 from being corrected. Read-only status and the no-camera/error exit
codes were also checked. Multiple physical cameras have not been tested.

Physical unplug/replug, sleep/wake, login cycles, and visible flicker under
room lighting have not been verified. USB control readback is not a visual
test. Check the picture in your usual video app and use `--status` after a
reconnect to confirm `current=1`.

## References and license

- [Apple: USB device interfaces](https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/USBBook/USBDeviceInterfaces/USBDevInterfaces.html)
- [uvc-util](https://github.com/jtfrey/uvc-util): a useful reference for macOS UVC controls.
- [MIT license](LICENSE).
