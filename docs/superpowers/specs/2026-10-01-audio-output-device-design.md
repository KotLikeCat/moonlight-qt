# Audio output device selection — design

Status: approved by the user in chat on 2026-10-01 ("Дизайн а норм"). Branch `feat/audio` (based on `feat/clipboard-sync`).

## Goal
The stream's audio plays on a device the user picks in Moonlight (e.g. headphones), while the rest of the Mac keeps using the macOS default output.

## Behaviour
- Settings → Audio gets a combo box **"Audio output device"**: first entry "System default", then every SDL playback device name. The choice persists globally (all hosts) as the device name; empty string = system default.
- If the saved device is not connected when the settings page opens, the list still shows it as "<name> (not connected)" so the choice is not silently lost.
- Stream start: the saved device is opened if connected. If it is not connected (or opening it fails), audio goes to the system default and the user gets one launch warning: "Audio device \"<name>\" is not connected. Using the system default." (once per session).
- The device disappears mid-stream (AirPods die): the existing renderer-reinit path reopens audio; the device is missing, so it falls back to the system default. No automatic switch back during the same stream.
- Only the stream's audio is affected. The existing "Mute audio when Moonlight is not the active window" and host-audio options keep working.

## Design
- `app/streaming/audio/audiodevice.{h,cpp}` (new, pure, unit-testable):
  `QString AudioDevice::resolve(const QStringList& available, const QString& preferred)` → `preferred` if it is non-empty and present in `available` (exact match), otherwise an empty string (= default).
- `StreamingPreferences`: new `QString audioOutputDevice` (settings key `audiooutputdevice`, default `""`), Q_PROPERTY with NOTIFY signal, loaded/saved like the other preferences.
- Device list for QML: `Q_INVOKABLE QStringList SystemProperties::getAudioOutputDevices()` — `SDL_InitSubSystem(SDL_INIT_AUDIO)`, collect `SDL_GetAudioDeviceName(i, 0)` for `i < SDL_GetNumAudioDevices(0)`, `SDL_QuitSubSystem(SDL_INIT_AUDIO)`. Init/Quit must stay paired (the SDL renderer asserts `!SDL_WasInit(SDL_INIT_AUDIO)` when a stream starts); never called while streaming (the settings page is not reachable then).
- `SdlAudioRenderer(QString preferredDevice)`: after its existing `SDL_InitSubSystem(SDL_INIT_AUDIO)`, `prepareForPlayback` enumerates playback devices, calls `AudioDevice::resolve`, and opens `SDL_OpenAudioDevice(name or NULL, 0, ...)`. If the named open fails it retries with NULL. Exposes `bool preferredDeviceMissing() const` (true when a non-empty preference was not used).
- `Session::createAudioRenderer` passes `m_Preferences->audioOutputDevice` to `SdlAudioRenderer` (adjust `TRY_INIT_RENDERER` or construct it explicitly; SLAudio unchanged). After a successful `initializeAudioRenderer`, if the renderer is an `SdlAudioRenderer` with `preferredDeviceMissing()` and the warning has not been shown this session, call `emitLaunchWarning(...)` (check that emitting from the audio-init thread is safe; if launch warnings are only shown before the stream window appears, a log line is enough for the mid-stream case).
- `SettingsView.qml`: the combo box inside the Audio Settings group, below "Audio configuration"; model built from `SystemProperties.getAudioOutputDevices()` when the page loads; selecting an entry writes `StreamingPreferences.audioOutputDevice`. Strings through `qsTr`.

## Testing
- QtTest project `tests/audio` (mirrors `tests/clipboard`): `resolve` returns the preferred name when present, empty when missing, empty when preference is empty, exact match only (no prefix match), works with non-ASCII names ("Наушники AirPods").
- Build the app (`build` dir) to prove it compiles.
- Manual: pick headphones → stream audio in headphones, a YouTube tab on the Mac in speakers; unplug headphones before stream start → warning + default output.
