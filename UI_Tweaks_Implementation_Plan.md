# Spectral Noise Cancellation: UI Tweaks Implementation Plan

**Scope:** menu bar, keyboard shortcuts, timeline zoom, drag and drop, and related polish
**Platform target:** macOS first (native menu bar), Windows/Linux supported through the same code
**DSP impact:** none. The STFT, noise profile, spectral subtraction, and overlap-add code is not touched. All changes are in the UI and application layer.

---

## Table of contents

1. Goals and non-goals
2. Current state and target state
3. Architecture overview
4. Phase 1: Command system
5. Phase 2: Native menu bar and removing on-screen buttons
6. Phase 3: Drag and drop
7. Phase 4: Parameter keybinds, shortcut editor, persistence
8. Phase 5: Timeline zoom and scroll
9. Phase 6: Polish (recent files, status line, unsaved-work guard, errors)
10. Default shortcut sheet
11. Edge cases
12. Test plan
13. Suggested commit sequence
14. Additions to the technical report
15. Assumptions to verify against your code

---

## 1. Goals and non-goals

### Goals

- Move file-level actions (open, load noise, export) into a standard top menu bar: **File, Edit, View, Process, Playback, Help**.
- Give every action and every user parameter a keyboard shortcut, and let the user rebind them.
- Add zoom and horizontal scroll to the timeline, kept in sync across the waveform and both spectrograms.
- Accept audio files by drag and drop, with separate drop zones for input audio and noise file.
- Keep the on-screen UI for live controls only (α knob, Process, Play, Original/Processed toggle).

### Non-goals

- No changes to the signal-processing classes (`FrameProcessor`, `NoiseProfile`, `SpectralSubtractor`, `AudioReconstructor`).
- No plugin formats.
- No undo/redo system (can be added later on top of the command layer).

---

## 2. Current state and target state

### 2.1 Current UI (from screenshot, 29 Oct)

| Area | Contents |
|---|---|
| Header | Title, **Open audio** button, file name label ("No file loaded"), **Original / Processed** toggle |
| Top strip | Waveform view (supports drag-to-mark noise) |
| Middle | **Original** spectrogram card, **Processed** spectrogram card |
| Bottom left | **Noise profile** card with **Load noise file** button |
| Bottom middle | **Noise reduction** card: α knob with value box (1.00) and help text |
| Bottom right | **Actions** card: **Process**, **Play**, **Save WAV** |
| macOS menu bar | Only the default app menu ("Spectral Noise Cancellation") |

### 2.2 Target UI

```
 macOS bar:  Spectral Noise Cancellation | File | Edit | View | Process | Playback | Help
┌───────────────────────────────────────────────────────────────────────────────┐
│ Spectral Noise Cancellation    speech_noisy.wav · 48 kHz · 0:42   [Orig][Proc] │  header
├───────────────────────────────────────────────────────────────────────────────┤
│ waveform (zoomable, drag = mark noise)                    ◀ drop zone: INPUT  │
│ ═══════════════════ scrollbar ═══════════════════                             │
├──────────────────────────────────────┬────────────────────────────────────────┤
│ Original spectrogram (zoom-linked)   │ Processed spectrogram (zoom-linked)    │
├──────────────────────────────┬───────┴──────────────┬─────────────────────────┤
│ Noise profile                │ Noise reduction      │ Actions                 │
│  ◀ drop zone: NOISE FILE     │   (α knob)           │  [Process ⌘↩]           │
│                              │                      │  [Play  Space]          │
└──────────────────────────────┴──────────────────────┴─────────────────────────┘
```

### 2.3 Mapping of current controls

| Current control | New home | Stays on screen? |
|---|---|---|
| Open audio | File > Open Audio… (⌘O) | No. Replaced by a file info label |
| Load noise file | File > Open Noise File… (⌘⇧O) | Optional small link-style button in the Noise profile card |
| Save WAV | File > Export Processed Audio… (⌘E) | No |
| Process | Process > Apply Noise Reduction (⌘↩) | Yes |
| Play | Playback > Play/Pause (Space) | Yes |
| Original / Processed toggle | Playback > Listen to Original / Processed (1 / 2, Tab flips) | Yes |
| α knob | Process > Increase / Decrease / Reset α | Yes |

---

## 3. Architecture overview

### 3.1 Principle: one action layer

Every user action exists exactly once, as a **command**. Menus, keyboard shortcuts, and on-screen buttons are three different ways to trigger the same command.

```
 Menu item ─────┐
 Key press ─────┼──► ApplicationCommandManager ──► CommandID ──► MainComponent::perform()
 Button click ──┘                                                     │
                                                                      ▼
                                              existing functions: loadInputFile(),
                                              loadNoiseFile(), process(), exportWav(), ...
```

Benefits:

- Enabled/disabled state is defined once in `getCommandInfo()` and is reflected automatically in the menu, the shortcut, and the button.
- Shortcut text appears automatically in menus and button tooltips.
- User rebinding comes for free through `KeyPressMappingSet`.

### 3.2 New and changed files

Rename to match your project; the responsibilities matter more than the names.

| File | New / changed | Responsibility |
|---|---|---|
| `Commands.h` | New | `CommandIDs` enum, category names, menu ID ranges |
| `MainMenuModel.h/.cpp` | New | `juce::MenuBarModel`; builds all menus from command IDs |
| `ParameterModel.h/.cpp` | New | Holds α (and future parameters) as `juce::Value`s with ranges, steps, defaults |
| `ViewState.h/.cpp` | New | Shared visible time range, zoom and scroll maths |
| `TimelineComponent.h/.cpp` | New | Base class for waveform and spectrogram views: wheel/pinch handling, time ↔ pixel mapping |
| `AppSettings.h/.cpp` | New | Wraps `juce::ApplicationProperties`: key mappings, recent files, last α, window bounds |
| `ValueToast.h/.cpp` | New (optional) | Small overlay showing "α 1.20" when changed by keyboard |
| `MainComponent.h/.cpp` | Changed | Becomes `ApplicationCommandTarget` and `FileDragAndDropTarget`; owns command manager, menu model, view state |
| `WaveformView` / `SpectrogramView` | Changed | Inherit `TimelineComponent`; draw only the visible range |
| `Main.cpp` | Changed | Menu bar attachment on Windows/Linux, quit confirmation, Finder "Open With" handling |
| `AudioFileManager` | Unchanged API | Reused by File menu and drag and drop |

### 3.3 Ownership

To keep things simple, `MainComponent` owns:

- `juce::ApplicationCommandManager commandManager;`
- `MainMenuModel menuModel;` (holds a reference to `commandManager`)
- `ParameterModel params;`
- `ViewState viewState;`
- `AppSettings settings;`
- `juce::RecentlyOpenedFilesList recentFiles;`
- `juce::TooltipWindow tooltipWindow { this };`

Declare `commandManager` **before** `menuModel` and any buttons that reference it, so construction and destruction order is safe.

---

## 4. Phase 1: Command system

### 4.1 `Commands.h`

```cpp
#pragma once
#include <JuceHeader.h>

namespace CommandIDs
{
    enum : juce::CommandID
    {
        // File
        openAudio = 0x2000,
        openNoiseFile,
        exportProcessed,
        closeFile,

        // Edit
        learnNoiseFromSelection = 0x2100,
        clearSelection,
        clearNoiseProfile,
        resetAllParameters,

        // View
        zoomIn = 0x2200,
        zoomOut,
        zoomToFit,
        zoomToSelection,
        followPlayhead,
        toggleSpectrograms,

        // Process
        applyNoiseReduction = 0x2300,
        alphaUp,
        alphaDown,
        alphaUpFine,
        alphaDownFine,
        alphaReset,

        // Playback
        playPause = 0x2400,
        stop,
        returnToStart,
        toggleLoop,
        listenOriginal,
        listenProcessed,
        toggleListenSource,

        // Help / settings
        showKeyboardShortcuts = 0x2500,
        showAudioSettings,
        showAbout
    };
}

namespace CommandCategories
{
    static constexpr const char* file     = "File";
    static constexpr const char* edit     = "Edit";
    static constexpr const char* view     = "View";
    static constexpr const char* process  = "Process";
    static constexpr const char* playback = "Playback";
    static constexpr const char* help     = "Help";
}

namespace MenuItemRanges
{
    // Non-command menu items (recent files) live in their own ID range
    // so they can never collide with a CommandID.
    static constexpr int recentFilesBase = 0x3000;
    static constexpr int recentFilesMax  = 20;
}
```

Leave gaps between categories (0x2000, 0x2100, …) so new commands can be added later without renumbering. Never reuse or renumber an ID once shortcuts have been saved, because saved key mappings refer to IDs.

### 4.2 `MainComponent` becomes a command target

```cpp
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener
{
public:
    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform (const juce::ApplicationCommandTarget::InvocationInfo& info) override;
    ...
};
```

Returning `nullptr` from `getNextCommandTarget()` is fine: when no target handles a command, JUCE falls back to the `JUCEApplication` instance, which handles `StandardApplicationCommandIDs::quit`.

### 4.3 Registration (constructor)

```cpp
MainComponent::MainComponent()
    : menuModel (commandManager, recentFiles, [this] (const juce::File& f) { loadInputFile (f); })
{
    commandManager.registerAllCommandsForTarget (this);
    commandManager.registerAllCommandsForTarget (juce::JUCEApplication::getInstance()); // quit
    commandManager.setFirstCommandTarget (this);

    settings.restoreKeyMappings (*commandManager.getKeyMappings()); // must come AFTER registration

    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    ...existing UI setup...
}
```

### 4.4 `getAllCommands`

```cpp
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    using namespace CommandIDs;
    c.addArray ({ openAudio, openNoiseFile, exportProcessed, closeFile,
                  learnNoiseFromSelection, clearSelection, clearNoiseProfile, resetAllParameters,
                  zoomIn, zoomOut, zoomToFit, zoomToSelection, followPlayhead, toggleSpectrograms,
                  applyNoiseReduction, alphaUp, alphaDown, alphaUpFine, alphaDownFine, alphaReset,
                  playPause, stop, returnToStart, toggleLoop,
                  listenOriginal, listenProcessed, toggleListenSource,
                  showKeyboardShortcuts, showAudioSettings, showAbout });
}
```

### 4.5 Command specification table

This table is the single source of truth. Implement `getCommandInfo()` from it.

`⌘` = `ModifierKeys::commandModifier`, which is Cmd on macOS and Ctrl on Windows/Linux, so one definition covers both.

| Command | Menu name | Category | Default key | Active when | Ticked when |
|---|---|---|---|---|---|
| openAudio | Open Audio… | File | ⌘O | not processing | |
| openNoiseFile | Open Noise File… | File | ⌘⇧O | input loaded, not processing | |
| exportProcessed | Export Processed Audio… | File | ⌘E | processed audio exists and is not stale | |
| closeFile | Close File | File | ⌘W | input loaded | |
| learnNoiseFromSelection | Learn Noise from Selection | Edit | ⌘L | selection exists | |
| clearSelection | Clear Selection | Edit | Esc | selection exists | |
| clearNoiseProfile | Clear Noise Profile | Edit | ⌘⌫ | profile exists | |
| resetAllParameters | Reset All Parameters | Edit | (none) | always | |
| zoomIn | Zoom In | View | ⌘= | input loaded, not at max zoom | |
| zoomOut | Zoom Out | View | ⌘- | input loaded, not fully zoomed out | |
| zoomToFit | Zoom to Fit | View | ⌘0 | input loaded | |
| zoomToSelection | Zoom to Selection | View | Z | selection exists | |
| followPlayhead | Follow Playhead | View | F | input loaded | follow on |
| toggleSpectrograms | Show Spectrograms | View | ⌘⇧S | always | visible |
| applyNoiseReduction | Apply Noise Reduction | Process | ⌘↩ | input + profile, not processing | |
| alphaUp | Increase α | Process | ] | always | |
| alphaDown | Decrease α | Process | [ | always | |
| alphaUpFine | Increase α (Fine) | Process | ⇧] | always | |
| alphaDownFine | Decrease α (Fine) | Process | ⇧[ | always | |
| alphaReset | Reset α | Process | \\ | α ≠ default | |
| playPause | Play / Pause | Playback | Space | input loaded | |
| stop | Stop | Playback | . | playing | |
| returnToStart | Return to Start | Playback | Home and ↩ | input loaded | |
| toggleLoop | Loop Selection | Playback | ⌘⇧L | selection exists | loop on |
| listenOriginal | Listen to Original | Playback | 1 | input loaded | source = original |
| listenProcessed | Listen to Processed | Playback | 2 | processed exists | source = processed |
| toggleListenSource | Toggle Original / Processed | Playback | Tab | processed exists | |
| showKeyboardShortcuts | Keyboard Shortcuts… | Help | ⌘, (Settings slot on macOS) | always | |
| showAudioSettings | Audio Device Settings… | Help | (none) | always | |
| showAbout | About Spectral Noise Cancellation | Help | (none) | always | |

Notes on key choices:

- On some keyboards `⇧]` produces `}`. JUCE matches on key code plus modifiers, so `KeyPress (']', shift, 0)` normally works, but **test this on your keyboard layout**. If it fails, use `⌥]` / `⌥[` for fine steps instead.
- `Tab` is also used for focus traversal. Because buttons and the knob will not take keyboard focus (section 4.8), this is safe. If you keep focusable widgets, pick `T` instead.
- On macOS, `⌘,` is the standard "Settings…" shortcut. Putting the shortcut editor there makes the app feel native.

### 4.6 `getCommandInfo` (pattern)

```cpp
void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using namespace CommandIDs;
    namespace Cat = CommandCategories;
    const auto cmd   = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    switch (id)
    {
        case openAudio:
            info.setInfo ("Open Audio...", "Load the noisy recording to clean", Cat::file, 0);
            info.addDefaultKeypress ('o', cmd);
            info.setActive (! isProcessing);
            break;

        case openNoiseFile:
            info.setInfo ("Open Noise File...", "Load a noise-only recording for the profile", Cat::file, 0);
            info.addDefaultKeypress ('o', cmd | shift);
            info.setActive (hasInputAudio() && ! isProcessing);
            break;

        case exportProcessed:
            info.setInfo ("Export Processed Audio...", "Save the cleaned audio as WAV", Cat::file, 0);
            info.addDefaultKeypress ('e', cmd);
            info.setActive (hasProcessedAudio() && ! processedIsStale && ! isProcessing);
            break;

        case zoomIn:
            info.setInfo ("Zoom In", "Show less time, more detail", Cat::view, 0);
            info.addDefaultKeypress ('=', cmd);
            info.addDefaultKeypress ('+', cmd);   // second binding for keyboards where + is unshifted
            info.setActive (hasInputAudio() && viewState.canZoomIn());
            break;

        case alphaUp:
            info.setInfo ("Increase Alpha", "Stronger noise reduction", Cat::process, 0);
            info.addDefaultKeypress (']', 0);
            break;

        case playPause:
            info.setInfo (isPlaying() ? "Pause" : "Play", "Start or pause playback", Cat::playback, 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            info.setActive (hasInputAudio());
            break;

        case listenOriginal:
            info.setInfo ("Listen to Original", "Play the unprocessed input", Cat::playback, 0);
            info.addDefaultKeypress ('1', 0);
            info.setActive (hasInputAudio());
            info.setTicked (listenSource == ListenSource::original);
            break;

        case toggleSpectrograms:
            info.setInfo ("Show Spectrograms", "Show or hide the spectrogram panels", Cat::view, 0);
            info.addDefaultKeypress ('s', cmd | shift);
            info.setTicked (spectrogramsVisible);
            break;

        // ... remaining commands follow the table in 4.5 ...

        default:
            break;
    }
}
```

### 4.7 `perform`

```cpp
bool MainComponent::perform (const InvocationInfo& info)
{
    using namespace CommandIDs;

    switch (info.commandID)
    {
        case openAudio:               chooseAndLoadInputFile();             break;
        case openNoiseFile:           chooseAndLoadNoiseFile();             break;
        case exportProcessed:         chooseAndExportProcessed();           break;
        case closeFile:               closeCurrentFile();                   break;

        case learnNoiseFromSelection: learnNoiseFromCurrentSelection();     break;
        case clearSelection:          selection = {}; repaint();            break;
        case clearNoiseProfile:       noiseProfile.reset(); markStale();    break;
        case resetAllParameters:      params.resetAll();                    break;

        case zoomIn:                  viewState.zoomAround (zoomAnchor(), 2.0);  break;
        case zoomOut:                 viewState.zoomAround (zoomAnchor(), 0.5);  break;
        case zoomToFit:               viewState.zoomToFit();                     break;
        case zoomToSelection:         viewState.setVisibleRange (selection.padded (0.05)); break;
        case followPlayhead:          followPlayheadEnabled = ! followPlayheadEnabled;     break;
        case toggleSpectrograms:      spectrogramsVisible = ! spectrogramsVisible; resized(); break;

        case applyNoiseReduction:     runProcessing();                      break;
        case alphaUp:                 params.nudgeAlpha (+1, false); showAlphaToast(); break;
        case alphaDown:               params.nudgeAlpha (-1, false); showAlphaToast(); break;
        case alphaUpFine:             params.nudgeAlpha (+1, true);  showAlphaToast(); break;
        case alphaDownFine:           params.nudgeAlpha (-1, true);  showAlphaToast(); break;
        case alphaReset:              params.resetAlpha();           showAlphaToast(); break;

        case playPause:               togglePlayback();                     break;
        case stop:                    stopPlayback();                       break;
        case returnToStart:           setPlayheadPosition (0.0);            break;
        case toggleLoop:              loopEnabled = ! loopEnabled;          break;
        case listenOriginal:          setListenSource (ListenSource::original);  break;
        case listenProcessed:         setListenSource (ListenSource::processed); break;
        case toggleListenSource:      flipListenSource();                   break;

        case showKeyboardShortcuts:   showShortcutEditor();                 break;
        case showAudioSettings:       showAudioDeviceDialog();              break;
        case showAbout:               showAboutBox();                       break;

        default: return false;
    }

    commandManager.commandStatusChanged();   // refresh menus, ticks, enabled states
    return true;
}
```

`zoomAnchor()` returns the playhead time if the playhead is inside the visible range, otherwise the centre of the visible range.

**Rule:** any time state changes outside `perform()` (file finished loading, processing finished, playback stopped at end of file, selection changed by mouse), call `commandManager.commandStatusChanged()` so menu items and buttons re-evaluate their enabled state.

### 4.8 Keyboard focus

This is the most common reason shortcuts "randomly stop working".

1. `MainComponent` has `setWantsKeyboardFocus (true)` and grabs focus after load:
   `grabKeyboardFocus();` at the end of `loadInputFile()` and in `mouseDown()`.
2. All `TextButton`s and the α knob: `setWantsKeyboardFocus (false)`. Otherwise a focused button consumes Space/Return.
3. The knob's value text box is a `TextEditor` while editing. That is fine; it should consume digits while the user types. After `onReturnKey`/focus lost, focus returns to `MainComponent`.
4. Also attach the key listener to the top-level window, so shortcuts work even if a child somehow keeps focus:

```cpp
void MainComponent::parentHierarchyChanged()
{
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->addKeyListener (commandManager.getKeyMappings());
}
```

(Remove it in the destructor if the window can outlive the component.)

### 4.9 Rewiring existing buttons

Replace each button's `onClick` lambda with:

```cpp
processButton.setCommandToTrigger (&commandManager, CommandIDs::applyNoiseReduction, true);
playButton   .setCommandToTrigger (&commandManager, CommandIDs::playPause, true);
```

The third argument (`generateTooltip = true`) makes JUCE create a tooltip like "Apply Noise Reduction (⌘↩)" automatically, which needs a `juce::TooltipWindow` member. The button's enabled state also follows `setActive()` automatically.

For the Original/Processed toggle, keep it as two radio buttons but have their `onClick` call:

```cpp
originalButton .onClick = [this] { commandManager.invokeDirectly (CommandIDs::listenOriginal,  true); };
processedButton.onClick = [this] { commandManager.invokeDirectly (CommandIDs::listenProcessed, true); };
```

and update their toggle state inside `setListenSource()` so keyboard and mouse stay in sync.

**Checkpoint:** at the end of Phase 1, the app looks identical, but every button goes through the command manager and the default shortcuts already work.

---

## 5. Phase 2: Native menu bar and removing on-screen buttons

### 5.1 `MainMenuModel`

```cpp
class MainMenuModel : public juce::MenuBarModel
{
public:
    MainMenuModel (juce::ApplicationCommandManager& cm,
                   juce::RecentlyOpenedFilesList& recent,
                   std::function<void (const juce::File&)> onOpenRecent);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu   getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void              menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    juce::ApplicationCommandManager& commandManager;
    juce::RecentlyOpenedFilesList& recentFiles;
    std::function<void (const juce::File&)> openRecent;
};
```

```cpp
MainMenuModel::MainMenuModel (juce::ApplicationCommandManager& cm,
                              juce::RecentlyOpenedFilesList& recent,
                              std::function<void (const juce::File&)> onOpenRecent)
    : commandManager (cm), recentFiles (recent), openRecent (std::move (onOpenRecent))
{
    setApplicationCommandManagerToWatch (&commandManager);  // auto-refresh on commandStatusChanged()
}

juce::StringArray MainMenuModel::getMenuBarNames()
{
    return { "File", "Edit", "View", "Process", "Playback", "Help" };
}

juce::PopupMenu MainMenuModel::getMenuForIndex (int index, const juce::String&)
{
    using namespace CommandIDs;
    juce::PopupMenu m;
    auto add = [&] (juce::CommandID id) { m.addCommandItem (&commandManager, id); };

    switch (index)
    {
        case 0: // File
        {
            add (openAudio);
            add (openNoiseFile);

            juce::PopupMenu recentMenu;
            recentFiles.createPopupMenuItems (recentMenu, MenuItemRanges::recentFilesBase,
                                              false /*showFullPaths*/, true /*skip missing*/);
            m.addSubMenu ("Open Recent", recentMenu, recentMenu.getNumItems() > 0);

            m.addSeparator();
            add (exportProcessed);
            m.addSeparator();
            add (closeFile);
           #if ! JUCE_MAC
            m.addSeparator();
            add (juce::StandardApplicationCommandIDs::quit);
           #endif
            break;
        }
        case 1: // Edit
            add (learnNoiseFromSelection);
            add (clearSelection);
            m.addSeparator();
            add (clearNoiseProfile);
            add (resetAllParameters);
            break;

        case 2: // View
            add (zoomIn); add (zoomOut); add (zoomToFit); add (zoomToSelection);
            m.addSeparator();
            add (followPlayhead);
            add (toggleSpectrograms);
            break;

        case 3: // Process
            add (applyNoiseReduction);
            m.addSeparator();
            add (alphaUp); add (alphaDown); add (alphaUpFine); add (alphaDownFine); add (alphaReset);
            break;

        case 4: // Playback
            add (playPause); add (stop); add (returnToStart);
            m.addSeparator();
            add (toggleLoop);
            m.addSeparator();
            add (listenOriginal); add (listenProcessed); add (toggleListenSource);
            break;

        case 5: // Help
           #if ! JUCE_MAC
            add (showKeyboardShortcuts);
            add (showAudioSettings);
            m.addSeparator();
            add (showAbout);
           #else
            add (showAudioSettings);   // keyboard shortcuts + about live in the app menu on macOS
           #endif
            break;
    }
    return m;
}

void MainMenuModel::menuItemSelected (int itemID, int)
{
    using namespace MenuItemRanges;
    if (itemID >= recentFilesBase && itemID < recentFilesBase + recentFilesMax)
        if (openRecent)
            openRecent (recentFiles.getFile (itemID - recentFilesBase));
}
```

Command items are dispatched by the command manager directly; `menuItemSelected` only sees non-command items, which here means recent files.

### 5.2 Attaching the menu bar

**macOS** (in `MainComponent` constructor and destructor):

```cpp
#if JUCE_MAC
    juce::PopupMenu appMenuExtras;
    appMenuExtras.addCommandItem (&commandManager, CommandIDs::showAbout);
    appMenuExtras.addSeparator();
    appMenuExtras.addCommandItem (&commandManager, CommandIDs::showKeyboardShortcuts);
    juce::MenuBarModel::setMacMainMenu (&menuModel, &appMenuExtras);
#endif

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);   // must happen before menuModel is destroyed
   #endif
}
```

Result on macOS: **Spectral Noise Cancellation** (About, Keyboard Shortcuts… ⌘,, Quit ⌘Q) | **File** | **Edit** | **View** | **Process** | **Playback** | **Help**.

**Windows/Linux** (in `Main.cpp`, inside `MainWindow`):

```cpp
#if ! JUCE_MAC
    if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
        setMenuBar (&mc->getMenuModel());
#endif
...
~MainWindow() override
{
   #if ! JUCE_MAC
    setMenuBar (nullptr);
   #endif
}
```

Add `MainMenuModel& getMenuModel() { return menuModel; }` to `MainComponent`.

### 5.3 Removing on-screen buttons and reworking the header

1. Delete `openAudioButton`, `saveWavButton` and their `onClick` code. Their logic now lives in `chooseAndLoadInputFile()` and `chooseAndExportProcessed()`.
2. Replace the "No file loaded" label with a **file info label**: `name · sample rate · duration · channels`, for example `speech_noisy.wav · 48 kHz · 0:42 · mono`. When empty, show "No file loaded — ⌘O or drop a file".
3. In the **Noise profile** card, either remove **Load noise file** or restyle it as a small text link. Recommended: keep it, since it is contextual and discoverable.
4. The **Actions** card now holds only **Process** and **Play**. Either shrink the card (give the width to Noise profile) or add a compact status block under the buttons showing: profile frames used (M), FFT size N, hop H, window, current α. This doubles as the "state the parameters" requirement for the demo.
5. Update placeholder texts:
   - Waveform: "Drop an audio file here or press ⌘O"
   - Noise profile: "Drag on the waveform to mark noise, or drop a noise file here"
   - Processed: "Mark some noise, then press Process (⌘↩)"

### 5.4 File chooser helpers (shared by menu and drag-drop)

```cpp
void MainComponent::chooseAndLoadInputFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open audio", lastDirectory(),
                                                       formatManager.getWildcardForAllFormats());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile())
                                      loadInputFile (f);
                              });
}
```

`fileChooser` must be a member (`std::unique_ptr<juce::FileChooser>`), not a local, because `launchAsync` returns immediately.

Make `loadInputFile (const juce::File&)` and `loadNoiseFile (const juce::File&)` the **only** entry points for loading. File menu, Open Recent, drag and drop, and Finder "Open With" all call them. They:

1. Check the unsaved-work guard (section 9.3).
2. Call `AudioFileManager`, catching and reporting errors (section 9.4).
3. Update `viewState.setTotalLength()`, zoom to fit, rebuild thumbnail and spectrogram image.
4. Add the file to `recentFiles` and save settings.
5. Mark processed output as stale/cleared.
6. Call `commandManager.commandStatusChanged()` and `grabKeyboardFocus()`.

**Checkpoint:** native menus work, the header is clean, nothing is reachable only through a removed button.

---

## 6. Phase 3: Drag and drop

### 6.1 Drop zones

| Zone | Bounds | Effect |
|---|---|---|
| `input` | Header + waveform + both spectrogram cards | Load as input audio |
| `noise` | Noise profile card | Load as noise-only file |
| `none` | Noise reduction and Actions cards | No drop (cursor shows not-allowed); or treat as `input` (your choice; pick one and document it) |

Special case: if **no input is loaded**, a drop on the noise card loads as input instead, and the status line says "Loaded as input — load a recording before adding a noise file".

### 6.2 Implementation

```cpp
enum class DropZone { none, input, noise };
DropZone currentDropZone = DropZone::none;

DropZone MainComponent::dropZoneAt (juce::Point<int> p) const
{
    if (noiseProfileCard.getBounds().contains (p)) return hasInputAudio() ? DropZone::noise : DropZone::input;
    if (inputDropArea().contains (p))              return DropZone::input;
    return DropZone::none;
}

bool MainComponent::isSupportedAudioFile (const juce::File& f) const
{
    return f.existsAsFile()
        && formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (isProcessing) return false;
    for (auto& path : files)
        if (isSupportedAudioFile (juce::File (path)))
            return true;
    return false;
}

void MainComponent::fileDragEnter (const juce::StringArray&, int x, int y) { updateDropZone ({ x, y }); }
void MainComponent::fileDragMove  (const juce::StringArray&, int x, int y) { updateDropZone ({ x, y }); }
void MainComponent::fileDragExit  (const juce::StringArray&)               { updateDropZone ({ -1, -1 }); }

void MainComponent::updateDropZone (juce::Point<int> p)
{
    auto z = dropZoneAt (p);
    if (z != currentDropZone) { currentDropZone = z; repaint(); }
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    auto zone = dropZoneAt ({ x, y });
    currentDropZone = DropZone::none;
    repaint();

    juce::File chosen;
    int supportedCount = 0;
    for (auto& path : files)
        if (juce::File f (path); isSupportedAudioFile (f))
            if (supportedCount++ == 0) chosen = f;

    if (! chosen.existsAsFile() || zone == DropZone::none)
        return;

    if (zone == DropZone::noise) loadNoiseFile (chosen);
    else                         loadInputFile (chosen);

    if (supportedCount > 1)
        setStatus ("Loaded " + chosen.getFileName() + " (other dropped files ignored)");
}
```

`x, y` are relative to `MainComponent`. Because child components (waveform, cards) don't implement `FileDragAndDropTarget`, the drag events bubble up to `MainComponent`, so one implementation covers the whole window.

### 6.3 Visual feedback

Draw the highlight in `paintOverChildren()` so it appears on top of the cards:

```cpp
void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (currentDropZone == DropZone::none) return;

    auto area  = (currentDropZone == DropZone::noise ? noiseProfileCard.getBounds() : inputDropArea()).toFloat();
    auto label = currentDropZone == DropZone::noise ? "Drop to load as noise profile"
                                                    : "Drop to load as input audio";

    g.setColour (accentColour.withAlpha (0.10f));
    g.fillRoundedRectangle (area, cardCornerRadius);
    g.setColour (accentColour);
    g.drawRoundedRectangle (area.reduced (1.0f), cardCornerRadius, 2.0f);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (label, area, juce::Justification::centred);
}
```

Use the same accent orange as the α arc and the Original toggle, so it matches the existing Apple/Figma-style look.

### 6.4 Opening files from Finder / Explorer (optional but cheap)

1. In Projucer, set **Document file extensions** to `wav;aif;aiff;flac;mp3`. This registers the app in Finder's "Open With".
2. In `Main.cpp`:

```cpp
bool moreThanOneInstanceAllowed() override { return false; }

void anotherInstanceStarted (const juce::String& commandLine) override
{
    auto args = juce::StringArray::fromTokens (commandLine, true);
    for (auto a : args)
        if (juce::File f (a.unquoted()); f.existsAsFile())
            if (auto* mc = getMainComponent()) { mc->loadInputFile (f); break; }
}
```

3. Also parse `commandLine` in `initialise()` the same way, for files passed at launch on Windows.

**Checkpoint:** dropping a WAV on the waveform loads it; dropping on the noise card loads the noise file; dropping a `.txt` shows the not-allowed cursor.

---

## 7. Phase 4: Parameter keybinds, shortcut editor, persistence

### 7.1 `ParameterModel`

Today the only user parameter is α. The model is designed so FFT size, window, and hop can be added the same way later.

```cpp
class ParameterModel
{
public:
    struct Spec { double min, max, step, fineStep, defaultValue; };

    // Use the knob's existing range. Values below are examples.
    static constexpr Spec alphaSpec { 0.0, 5.0, 0.1, 0.01, 1.0 };

    juce::Value alpha { alphaSpec.defaultValue };

    double getAlpha() const { return (double) alpha.getValue(); }

    void nudgeAlpha (int direction, bool fine)
    {
        const auto step = fine ? alphaSpec.fineStep : alphaSpec.step;
        auto v = getAlpha() + direction * step;
        v = std::round (v / alphaSpec.fineStep) * alphaSpec.fineStep;   // avoid 1.0000000002 drift
        alpha = juce::jlimit (alphaSpec.min, alphaSpec.max, v);
    }

    void resetAlpha() { alpha = alphaSpec.defaultValue; }
    void resetAll()   { resetAlpha(); }
};
```

Bind the knob to the model so keyboard and mouse always agree:

```cpp
alphaKnob.getValueObject().referTo (params.alpha);
alphaKnob.setDoubleClickReturnValue (true, ParameterModel::alphaSpec.defaultValue);
```

Listen for changes in `MainComponent` (`juce::Value::Listener`) to:

- pass the new α to `SpectralSubtractor`,
- mark processed output stale (7.2),
- call `commandManager.commandStatusChanged()` (so "Reset α" enables/disables).

### 7.2 Stale-output behaviour after α changes

Because processing is triggered by **Process**, changing α makes the existing processed output out of date. Choose one behaviour:

| Option | Behaviour | When to choose |
|---|---|---|
| A (recommended) | Set `processedIsStale = true`. Processed card caption shows "α changed — press ⌘↩ to reprocess". Export is disabled until reprocessed. | Processing takes more than ~0.3 s |
| B | Auto-reprocess 250 ms after the last α change (debounced with `juce::Timer`). | Processing is fast for your demo files |

Option A is safer and easy to explain in the report. Whichever you choose, never export audio that doesn't match the α shown on screen.

### 7.3 Value toast (feedback when changing α by keyboard)

When α changes through a shortcut there is no mouse movement to look at, so show a short overlay:

```cpp
class ValueToast : public juce::Component, private juce::Timer
{
public:
    void show (const juce::String& text)
    {
        message = text;
        setVisible (true);
        setAlpha (1.0f);
        repaint();
        startTimer (900);
    }
private:
    void timerCallback() override
    {
        stopTimer();
        juce::Desktop::getInstance().getAnimator().fadeOut (this, 200);
    }
    void paint (juce::Graphics& g) override { /* rounded pill + text */ }
    juce::String message;
};
```

Place it just above the α knob. Text format: `α 1.20` (use `juce::String (getAlpha(), 2)`).

### 7.4 Keyboard shortcut editor

```cpp
void MainComponent::showShortcutEditor()
{
    juce::DialogWindow::LaunchOptions o;
    auto* editor = new juce::KeyMappingEditorComponent (*commandManager.getKeyMappings(), true);
    editor->setSize (520, 600);
    o.content.setOwned (editor);
    o.dialogTitle                  = "Keyboard Shortcuts";
    o.dialogBackgroundColour       = findColour (juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar            = true;
    o.resizable                    = true;
    o.launchAsync();
}
```

The editor groups commands by category (File, Edit, View, …), lets the user add/remove keys per command, warns about conflicts, and has a "Reset to defaults" button (the `true` argument).

Style it to match the app by setting colours on `KeyMappingEditorComponent` (`backgroundColourId`, `textColourId`) or through your existing `LookAndFeel`.

### 7.5 Persistence (`AppSettings`)

```cpp
class AppSettings
{
public:
    AppSettings()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "SpectralNoiseCancellation";
        o.filenameSuffix      = ".settings";
        o.folderName          = "SpectralNoiseCancellation";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
    }

    void saveKeyMappings (const juce::KeyPressMappingSet& km)
    {
        if (auto xml = km.createXml (true))          // true = only store changes from defaults
            user().setValue ("keyMappings", xml.get());
        user().saveIfNeeded();
    }

    void restoreKeyMappings (juce::KeyPressMappingSet& km)
    {
        if (auto xml = user().getXmlValue ("keyMappings"))
            km.restoreFromXml (*xml);
    }

    void saveRecentFiles (const juce::RecentlyOpenedFilesList& r) { user().setValue ("recentFiles", r.toString()); user().saveIfNeeded(); }
    void restoreRecentFiles (juce::RecentlyOpenedFilesList& r)    { r.restoreFromString (user().getValue ("recentFiles")); }

    void   saveAlpha (double a)          { user().setValue ("alpha", a); }
    double restoreAlpha (double fallback) { return user().getDoubleValue ("alpha", fallback); }

private:
    juce::PropertiesFile& user() { return *props.getUserSettings(); }
    juce::ApplicationProperties props;
};
```

Save key mappings whenever they change: `KeyPressMappingSet` is a `ChangeBroadcaster`, so

```cpp
commandManager.getKeyMappings()->addChangeListener (this);

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == commandManager.getKeyMappings())
        settings.saveKeyMappings (*commandManager.getKeyMappings());
    else if (source == &viewState)
        onViewChanged();
}
```

Settings file location on macOS: `~/Library/Application Support/SpectralNoiseCancellation/SpectralNoiseCancellation.settings`. Mention this in the report and delete it when you want to test first-launch behaviour.

**Checkpoint:** `]`/`[` move the knob, the toast appears, rebinding a key survives a restart.

---

## 8. Phase 5: Timeline zoom and scroll

### 8.1 `ViewState`

All three time-based views (waveform, Original spectrogram, Processed spectrogram) read from one `ViewState`. They never store their own zoom.

```cpp
class ViewState : public juce::ChangeBroadcaster
{
public:
    void setTotalLength (double seconds)
    {
        totalLength = juce::jmax (0.0, seconds);
        zoomToFit();
    }

    double getTotalLength() const                 { return totalLength; }
    juce::Range<double> getVisibleRange() const   { return visible; }

    void setVisibleRange (juce::Range<double> r)
    {
        if (totalLength <= 0.0) return;

        const auto len   = juce::jlimit (minVisibleLength(), totalLength, r.getLength());
        const auto start = juce::jlimit (0.0, totalLength - len, r.getStart());
        juce::Range<double> clamped (start, start + len);

        if (clamped != visible)
        {
            visible = clamped;
            sendSynchronousChangeMessage();   // message thread only; immediate repaint feels smoother
        }
    }

    void zoomAround (double anchorTime, double factor)   // factor > 1 zooms in
    {
        const auto newLen     = visible.getLength() / factor;
        const auto proportion = (anchorTime - visible.getStart()) / visible.getLength();
        const auto newStart   = anchorTime - proportion * newLen;
        setVisibleRange ({ newStart, newStart + newLen });
    }

    void scrollBy (double deltaSeconds)  { setVisibleRange (visible.movedToStartAt (visible.getStart() + deltaSeconds)); }
    void zoomToFit()                     { visible = { 0.0, totalLength }; sendSynchronousChangeMessage(); }

    bool canZoomIn()  const { return visible.getLength() > minVisibleLength() + 1e-9; }
    bool canZoomOut() const { return visible.getLength() < totalLength - 1e-9; }

    float  timeToX (double t, juce::Rectangle<float> plot) const
    {
        return plot.getX() + (float) ((t - visible.getStart()) / visible.getLength()) * plot.getWidth();
    }
    double xToTime (float x, juce::Rectangle<float> plot) const
    {
        return visible.getStart() + (double) ((x - plot.getX()) / plot.getWidth()) * visible.getLength();
    }

private:
    double minVisibleLength() const { return juce::jmin (0.05, totalLength); } // 50 ms
    double totalLength = 0.0;
    juce::Range<double> visible;
};
```

`zoomAround` keeps the time under the anchor (mouse position or playhead) fixed on screen, which is how DAWs behave.

### 8.2 `TimelineComponent` base class

```cpp
class TimelineComponent : public juce::Component, private juce::ChangeListener
{
public:
    explicit TimelineComponent (ViewState& vs) : viewState (vs) { viewState.addChangeListener (this); }
    ~TimelineComponent() override                               { viewState.removeChangeListener (this); }

    // Subclasses return the rectangle where data is drawn (excluding gutters / axes).
    virtual juce::Rectangle<float> getPlotArea() const = 0;

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (viewState.getTotalLength() <= 0.0) return;

        if (e.mods.isCommandDown())
        {
            const auto factor = std::pow (2.0, (double) w.deltaY * zoomSensitivity);
            viewState.zoomAround (viewState.xToTime (e.position.x, getPlotArea()), factor);
        }
        else
        {
            // Trackpad horizontal swipe -> deltaX; mouse wheel / Shift+wheel -> deltaY.
            const auto delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            viewState.scrollBy (-(double) delta * viewState.getVisibleRange().getLength() * scrollSensitivity);
        }
    }

    void mouseMagnify (const juce::MouseEvent& e, float scaleFactor) override
    {
        viewState.zoomAround (viewState.xToTime (e.position.x, getPlotArea()), (double) scaleFactor);
    }

protected:
    ViewState& viewState;
    static constexpr double zoomSensitivity   = 4.0;
    static constexpr double scrollSensitivity = 0.5;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};
```

Sign conventions differ between mouse wheels, trackpads, and the macOS "natural scrolling" setting. **Test once on your MacBook trackpad and an external mouse, and flip the sign of `delta` if scrolling feels inverted.** `w.isReversed` tells you whether natural scrolling is on, if you want to handle it explicitly.

### 8.3 Waveform view

- Inherit `TimelineComponent`.
- Draw with `AudioThumbnail`, which caches peak data, so zooming is cheap:

```cpp
void WaveformView::paint (juce::Graphics& g)
{
    auto plot = getPlotArea();
    auto vis  = viewState.getVisibleRange();

    g.setColour (waveformColour);
    thumbnail.drawChannels (g, plot.toNearestInt(), vis.getStart(), vis.getEnd(), 1.0f);

    drawSelection (g, plot);   // noise region, in seconds -> pixels via viewState.timeToX
    drawPlayhead  (g, plot);
}
```

- If you currently draw the waveform yourself from the sample buffer, switching to `AudioThumbnail` is strongly recommended. A manual min/max pass over the visible range also works but must be done per pixel column, not per sample.
- Thumbnail resolution: create it with `AudioThumbnail (512, formatManager, thumbnailCache)`. At deep zoom (below ~512 samples per pixel) the thumbnail becomes blocky; for that range draw directly from the buffer as a line. This is optional polish.

### 8.4 Spectrogram views

The spectrogram should be computed **once** per file (or per processing run) into a `juce::Image`, then zooming only changes which part of the image is drawn.

**Drawing the visible part with sub-pixel accuracy:**

```cpp
void SpectrogramView::paint (juce::Graphics& g)
{
    if (! spectrogramImage.isValid()) { drawPlaceholder (g); return; }

    auto plot = getPlotArea();
    auto vis  = viewState.getVisibleRange();

    const double framesPerSecond = sampleRate / (double) hopSize;          // fs / H
    const double frameOffset     = (fftSize * 0.5) / (double) hopSize;     // centre-of-frame alignment
    const double srcX = vis.getStart()  * framesPerSecond - frameOffset;
    const double srcW = vis.getLength() * framesPerSecond;

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (plot.toNearestInt());
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);    // honest, blocky frames when zoomed in

    auto t = juce::AffineTransform::translation ((float) -srcX, 0.0f)
                 .scaled (plot.getWidth()  / (float) srcW,
                          plot.getHeight() / (float) spectrogramImage.getHeight())
                 .translated (plot.getX(), plot.getY());

    g.drawImageTransformed (spectrogramImage, t);

    drawSelection (g, plot);
    drawPlayhead  (g, plot);
}
```

Why these details matter:

- **Frame-centre alignment.** Frame m covers samples mH to mH + N − 1, so its centre is at (mH + N/2)/fs. Without `frameOffset`, the spectrogram is shifted by half a frame relative to the waveform, which becomes visible when zoomed in.
- **`drawImageTransformed` instead of `drawImage`.** `drawImage` takes integer source rectangles, so at deep zoom the image jumps one column at a time while the waveform scrolls smoothly. The transform keeps them locked.
- **Low resampling quality.** When zoomed in further than one frame per pixel, nearest-neighbour shows the true STFT frames rather than a blurred interpolation. This is also a nice visual for explaining the hop size in the report.

**Memory guard.** Image width equals the number of frames. At 48 kHz with H = 256, a 1-minute file is about 11,250 frames; at 513 rows that is about 23 MB (ARGB), which is fine. Long files (10 minutes or more) get large, so:

- cap image height at 256–512 rows (resample bins, and use a log-frequency mapping if you like), and
- for files over a few minutes, either cap total width by averaging frames, or split into tiles. For the assignment demo clips this is not needed; note the limit in the report.

### 8.5 Interaction rules

| Input | Action |
|---|---|
| Plain drag on waveform | Mark noise selection (existing behaviour; unchanged) |
| Plain drag on spectrogram | Same as waveform (selection is shared), or nothing (pick one) |
| ⌘ + scroll wheel / ⌘ + vertical swipe | Zoom around mouse |
| Pinch (trackpad) | Zoom around mouse |
| Scroll wheel / two-finger horizontal swipe | Scroll in time |
| ⌘= / ⌘- | Zoom in / out around playhead (or centre) |
| ⌘0 | Zoom to fit |
| Z | Zoom to selection (5% padding each side) |
| Double-click on scrollbar | Zoom to fit |

### 8.6 Selection and playhead under zoom

- Store the noise selection in **seconds** (`juce::Range<double> selection`), never in pixels. Convert with `viewState.xToTime()` on mouse events and `timeToX()` when drawing. This is the change most likely to need edits in your existing code.
- While dragging a selection past the left or right edge of the plot, auto-scroll: in `mouseDrag`, if `e.position.x` is outside the plot, call `viewState.scrollBy()` proportional to the overshoot, driven by a short `Timer` so it keeps scrolling while the mouse is held still.
- Playhead: draw at `timeToX (playheadTime)`; skip if outside the visible range.
- **Follow playhead** (View menu, `F`): during playback, if the playhead passes 90% of the visible width, page forward so it sits at 10%. Disable follow automatically when the user scrolls manually during playback, so the view doesn't fight them.

### 8.7 Scrollbar

Add one horizontal `juce::ScrollBar` directly under the waveform. It controls all three views because they share `ViewState`.

```cpp
juce::ScrollBar timeScrollBar { false };   // false = horizontal

// when ViewState changes:
timeScrollBar.setRangeLimits (0.0, viewState.getTotalLength(), juce::dontSendNotification);
timeScrollBar.setCurrentRange (viewState.getVisibleRange().getStart(),
                               viewState.getVisibleRange().getLength(),
                               juce::dontSendNotification);

// ScrollBar::Listener
void MainComponent::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    const auto len = viewState.getVisibleRange().getLength();
    viewState.setVisibleRange ({ newStart, newStart + len });
}
```

`dontSendNotification` when pushing state into the scrollbar prevents a feedback loop. Set `timeScrollBar.setAutoHide (true)` so it disappears when fully zoomed out, keeping the clean look.

### 8.8 Optional extras

- **Time ruler** above the waveform (mm:ss.ms ticks; tick spacing chosen from 1/2/5 × 10ⁿ to keep ~80 px between labels).
- **Frequency zoom** on spectrograms (e.g. ⌥ + scroll), showing 0–5 kHz for speech.
- **Vertical waveform gain** (⌥ + scroll on waveform) for quiet recordings.

**Checkpoint:** pinch on the waveform zooms all three views together; the selection stays attached to the same audio while zooming; the spectrogram lines up with waveform transients at maximum zoom.

---

## 9. Phase 6: Polish

### 9.1 Recent files

```cpp
recentFiles.setMaxNumberOfItems (MenuItemRanges::recentFilesMax);
settings.restoreRecentFiles (recentFiles);
// in loadInputFile on success:
recentFiles.addFile (file);
settings.saveRecentFiles (recentFiles);
commandManager.commandStatusChanged();   // rebuilds menus
```

On macOS also call `recentFiles.registerRecentFileNatively (file)` so files show in the Dock icon's recent list.

### 9.2 Status line

Use the space where "No file loaded" was (or a thin footer) for transient messages: "Exported clean.wav", "Noise profile learned from 1.2 s (225 frames)", "Loaded as input — load a recording before adding a noise file". Clear after ~4 s with a `Timer`.

### 9.3 Unsaved-work guard

Ask before discarding processed audio that hasn't been exported, when:

- opening another input file (menu, recent, drag and drop, Finder),
- closing the file (⌘W),
- quitting (⌘Q, window close button).

```cpp
void MainComponent::confirmDiscardIfNeeded (std::function<void()> proceed)
{
    if (! hasUnexportedProcessedAudio()) { proceed(); return; }

    auto opts = juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::QuestionIcon)
                    .withTitle ("Discard processed audio?")
                    .withMessage ("The processed audio has not been exported.")
                    .withButton ("Export...")
                    .withButton ("Discard")
                    .withButton ("Cancel");

    juce::AlertWindow::showAsync (opts, [this, proceed] (int result)
    {
        if (result == 1) chooseAndExportProcessed (proceed);   // export, then continue
        else if (result == 2) proceed();
        // 0 = cancel
    });
}
```

For quit, override `systemRequestedQuit()` in `Main.cpp` to call `confirmDiscardIfNeeded ([] { juce::JUCEApplication::quit(); })`.

### 9.4 Error handling

One helper used by every command:

```cpp
void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
    setStatus (title);
}
```

Cases to handle explicitly:

| Situation | Message |
|---|---|
| File unreadable / unsupported codec | "Couldn't open <name>. The file may be damaged or in an unsupported format." |
| Noise file sample rate ≠ input sample rate | "The noise file is 44.1 kHz but the input is 48 kHz. Use a noise file at the same sample rate." (or resample, if you implemented it) |
| Noise file/selection shorter than one FFT frame | "Noise section is too short. Select at least <N/fs> s." |
| Export target not writable | "Couldn't write <path>. Check the folder permissions." |
| Process with no noise profile | Command is disabled, so this can't happen from the UI; still guard in code |

### 9.5 Processing state

If processing runs on a background thread, set `isProcessing = true` before starting and `false` on completion (posted back with `juce::MessageManager::callAsync`), calling `commandStatusChanged()` both times. While processing, Open, Open Noise, Export, Process, and drag and drop are disabled. If processing runs on the message thread, the UI is frozen anyway, but the flag still protects against re-entry.

---

## 10. Default shortcut sheet

For the Help menu, the README, and the report. macOS keys shown; on Windows/Linux, ⌘ is Ctrl.

| Category | Action | Shortcut |
|---|---|---|
| File | Open audio | ⌘O |
| | Open noise file | ⌘⇧O |
| | Export processed audio | ⌘E |
| | Close file | ⌘W |
| Edit | Learn noise from selection | ⌘L |
| | Clear selection | Esc |
| | Clear noise profile | ⌘⌫ |
| View | Zoom in / out | ⌘= / ⌘- |
| | Zoom to fit | ⌘0 |
| | Zoom to selection | Z |
| | Follow playhead | F |
| | Show/hide spectrograms | ⌘⇧S |
| Process | Apply noise reduction | ⌘↩ |
| | α up / down (0.1) | ] / [ |
| | α up / down (0.01) | ⇧] / ⇧[ |
| | Reset α | \\ |
| Playback | Play / pause | Space |
| | Stop | . |
| | Return to start | Home or ↩ |
| | Loop selection | ⌘⇧L |
| | Listen to original / processed | 1 / 2 |
| | Toggle original / processed | Tab |
| App | Keyboard shortcuts | ⌘, |
| | Quit | ⌘Q |
| Mouse | Zoom | ⌘ + scroll, or pinch |
| | Scroll | Scroll / swipe |
| | Mark noise | Drag on waveform |

---

## 11. Edge cases

| Case | Expected behaviour |
|---|---|
| Very short file (< 50 ms) | `minVisibleLength` becomes the file length; zoom in disabled |
| File shorter than one FFT frame | Load succeeds; Process disabled with a status message |
| Zoom while playing | Playhead keeps moving correctly; follow mode respects manual zoom |
| Load new file while zoomed | Zoom resets to fit; selection cleared; processed output cleared |
| Drop while processing | `isInterestedInFileDrag` returns false (not-allowed cursor) |
| Drop folder | Ignored (not a file) |
| Drop mix of `.txt` + `.wav` | `.wav` loads; status notes ignored files |
| Shortcut while knob text box is being edited | Digits go to the text box; command keys with ⌘ still work |
| User rebinds Space to something else | Play/Pause has no key; menu still works; no crash |
| User assigns same key to two commands | `KeyMappingEditorComponent` warns and reassigns |
| Window resized while zoomed | Visible time range stays the same; only pixels per second change |
| Settings file corrupted or missing | Defaults used silently |
| Command ID enum changed after shortcuts saved | Old mappings for removed IDs are ignored; don't renumber existing IDs |

---

## 12. Test plan

### 12.1 Commands and menus

- [ ] Every menu item performs the same action as the old button did.
- [ ] With no file loaded: Export, Close, Process, Zoom, Play are greyed out.
- [ ] After loading: Play and Zoom enable; Process stays disabled until a noise profile exists.
- [ ] After processing: Export and Listen to Processed enable.
- [ ] After changing α (option A): Export disables and the caption shows the stale hint.
- [ ] Ticks update: Listen to Original/Processed, Follow Playhead, Show Spectrograms, Loop.
- [ ] Play menu item reads "Pause" while playing.
- [ ] macOS: About and Keyboard Shortcuts appear in the app menu; Quit works with ⌘Q.

### 12.2 Keybinds

- [ ] Every default shortcut in section 10 works.
- [ ] Shortcuts work after clicking the knob, the Process button, the waveform, and empty space.
- [ ] `]`/`[` move the knob visually and the toast shows the new value.
- [ ] α clamps at min/max with no error.
- [ ] Rebind α Up to `=`; quit; relaunch; the new binding works and `]` does not.
- [ ] "Reset to defaults" in the editor restores the table above.

### 12.3 Drag and drop

- [ ] Drop WAV on waveform → loads as input.
- [ ] Drop WAV on noise card with input loaded → loads as noise file.
- [ ] Drop WAV on noise card with no input → loads as input, status explains.
- [ ] Highlight follows the cursor between zones; disappears on exit.
- [ ] Drop `.txt`, folder, corrupt WAV → no crash; appropriate cursor or error.
- [ ] Finder "Open With" (if implemented) loads the file.

### 12.4 Zoom

- [ ] Pinch, ⌘+scroll, and ⌘=/⌘- all zoom; ⌘0 fits.
- [ ] The point under the mouse stays fixed while zooming with the mouse.
- [ ] Waveform, both spectrograms, selection, and playhead stay aligned at every zoom level.
- [ ] At maximum zoom, transients in the waveform line up with the onset in the spectrogram.
- [ ] Scrollbar drag scrolls all views; scrollbar hides when fully zoomed out.
- [ ] Marking noise while zoomed in selects the correct time range (verify the learned duration in the status line).
- [ ] Follow playhead pages correctly during playback.

### 12.5 Regression

- [ ] Processing result is bit-identical to before the UI changes for the same file, noise region, and α. (Export a WAV before starting Phase 1, then compare with a null test in your DAW: invert one and sum; it should be silent.)

---

## 13. Suggested commit sequence

Small commits make it easy to find where something broke, and give a clear history to mention in the report.

1. `Add Commands.h and make MainComponent an ApplicationCommandTarget`
2. `Route Process/Play/Save/Open buttons through command manager`
3. `Add default key mappings and keyboard focus handling`
4. `Add MainMenuModel with File/Process/Playback menus (macOS + Windows)`
5. `Remove Open/Save buttons; header shows file info`
6. `Add Edit/View/Help menus and enabled/ticked states`
7. `Add drag and drop with input/noise drop zones`
8. `Add ParameterModel; bind alpha knob; alpha shortcuts and toast`
9. `Add stale-output handling after alpha changes`
10. `Add shortcut editor and AppSettings persistence`
11. `Add ViewState and TimelineComponent; zoom waveform`
12. `Zoom-link spectrograms with frame-centre alignment`
13. `Store selection in seconds; scrollbar; follow playhead`
14. `Recent files, status line, unsaved-work guard, error messages`
15. `Shortcut sheet in README`

---

## 14. Additions to the technical report

Add a short section, roughly half a page:

1. **Application architecture.** The diagram from section 3.1 (menu / key / button → command manager → `perform()` → existing processing classes). One sentence on why: a single place for enabled state and behaviour.
2. **UI and processing separation.** The UI changes did not touch the DSP classes; the regression null test (12.5) shows the output is identical.
3. **Parameter handling.** α lives in `ParameterModel`; knob and shortcuts share one `juce::Value`; stale-output rule ensures exported audio always matches the displayed α.
4. **Spectrogram–waveform alignment.** Frame m is drawn centred at (mH + N/2)/fs, which links the display back to the STFT definition in section 4.1 of the brief.
5. **Shortcut table** (section 10).
6. **Demo settings** shown in the Actions card status block: N, H, window, α, and the noise segment length/number of frames M. The brief asks for these explicitly.

This mainly supports the "Application functionality" (8 marks) and "Code structure and JUCE use" (6 marks) criteria.

---

## 15. Assumptions to verify against your code

| Assumption | Where it matters | If different |
|---|---|---|
| One `MainComponent` holds all UI and button callbacks | Sections 3.3, 4 | Put the command target in whichever class owns the loading/processing calls |
| Noise selection is currently stored in pixels or sample indices | Section 8.6 | If already in seconds or samples, only the conversion functions change |
| α knob range is roughly 0–5 with default 1.0 | Section 7.1 | Copy the knob's actual range into `alphaSpec` |
| Spectrograms are drawn from a precomputed image | Section 8.4 | If they are recomputed in `paint()`, move computation to load/process time first |
| Waveform is drawn with `AudioThumbnail` | Section 8.3 | If drawn manually, switch to `AudioThumbnail` (recommended) or add per-column min/max |
| Processing runs on the message thread | Section 9.5 | If threaded, follow the `isProcessing` flag pattern |
| Playback uses `AudioTransportSource` | Sections 4.7, 8.6 | Adapt `togglePlayback`, `setPlayheadPosition`, and playhead time reads |
