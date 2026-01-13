## DroidStar iOS Live Activity / Dynamic Island Setup (qmake + Xcode)

This project uses **Qt/qmake** to generate an Xcode project. The Live Activity UI must live in an **iOS Widget Extension** target (WidgetKit), while the app starts/updates/ends the activity from the **main app** target (ActivityKit).

### Critical: why your extension/target-membership “disappears”

When you re-run **iOS `qmake`**, it regenerates `DroidStar.xcodeproj`. That regeneration typically wipes:

- **Swift Target Membership** checkmarks
- the **Widget Extension target**
- embedding settings, linker flags changes, build settings edits, etc.

**Rule:** generate the Xcode project once, then do all Xcode setup below, and **do not regenerate** unless you’re ready to repeat the setup.

---

## Prerequisites

- **iOS 16.1+** required for Live Activities / Dynamic Island.
- A Dynamic Island device is required to see the island UI. (Live Activities can still show on Lock Screen on non-Island devices.)
- Ensure the app `Info.plist` contains:
  - `NSSupportsLiveActivities` = `YES` (already present in this repo’s `Info.plist`)

---

## 1) Generate the Xcode project (once)

1. (Optional) delete any previously generated `DroidStar.xcodeproj` if it’s in a broken state.
2. Run the **iOS qmake** you use to generate the Xcode project.
3. Open the generated `DroidStar.xcodeproj` in Xcode.

---

## 2) Create the Widget Extension target (with Live Activity)

In Xcode:

1. **File → New → Target…**
2. Choose **Widget Extension**
3. **Product Name**: `DroidStarLiveActivityExtension`
4. Ensure **Include Live Activity** is checked
5. Finish

This creates:

- an **extension target**
- a Swift file for the Widget/Live Activity UI
- an extension `Info.plist`

---

## 3) Set correct Target Membership for Swift files

Open each file → right sidebar → **Target Membership**.

### A) `LiveActivityManager.swift`

- ✅ **DroidStar** (main app target)
- ⛔️ **DroidStarLiveActivityExtension** (extension target)

### B) `DroidStarActivityAttributes.swift`

- ✅ **DroidStar** (main app target)
- ✅ **DroidStarLiveActivityExtension** (extension target)

### C) `ios/DroidStarLiveActivityExtension/DroidStarLiveActivityExtension.swift`

- ✅ **DroidStarLiveActivityExtension**
- ⛔️ **DroidStar**

**Why:** the `ActivityAttributes` type must be shared between app + extension, but the Widget UI must only compile into the extension.

---

## 4) Main app target configuration (DroidStar)

Select **Target → DroidStar** (main app).

### A) Embed the extension into the app (required)

1. Go to **Build Phases**
2. Ensure **Embed App Extensions** exists and contains:
   - `DroidStarLiveActivityExtension.appex`
3. It should be **Embed & Sign**

(Depending on Xcode version you may also see this under **General → Frameworks, Libraries, and Embedded Content**.)

### B) Signing

Configure Signing Team for the app target so the app can sign/launch.

---

## 5) Extension target configuration (DroidStarLiveActivityExtension)

Select **Target → DroidStarLiveActivityExtension**.

### A) Signing

Set a valid Team so the extension can sign.

### B) iOS Deployment Target

Set **iOS Deployment Target = 16.1** (or higher).

### C) Required extension build setting

Set:

- **Build Settings → APPLICATION_EXTENSION_API_ONLY = YES**

### D) Remove Qt from the extension (must be clean)

The extension must **not** link to Qt or Qt permission plugins.

1. **Build Phases → Link Binary With Libraries**
   - Remove any Qt libraries/frameworks (examples):
     - `Qt6Core`, `Qt6Gui`, `Qt6Qml`, `Qt6Quick`, etc.
     - any `libQt*.a` or `Qt*.framework`

2. **Build Settings → Other Linker Flags (OTHER_LDFLAGS)**
   - Remove Qt-specific forced-undefined symbols (examples):
     - `-Wl,-u,_QDarwinCameraPermissionRequest`
     - `-Wl,-u,_QDarwinMicrophonePermissionRequest`
     - any other `-u _QDarwin…`
   - Good state is typically:
     - empty, or
     - only `$(inherited)` if Xcode insists

---

## 6) Build + run (recommended order)

1. **Product → Clean Build Folder**
2. Build the extension:
   - Select scheme `DroidStarLiveActivityExtension` → **Build**
3. Build/run the app:
   - Select scheme `DroidStar` → **Run**

---

## 7) Enable Live Activities in iOS settings

On the simulator/device:

- **Settings → DroidStar → Live Activities → ON**
- Also check **Settings → Notifications → Live Activities** (if present on your iOS version)

If Live Activities are disabled, iOS will refuse to show them.

---

## 8) How to actually see it

- Connect DroidStar so it starts updating the activity.
- Then go to **Home** or **Lock Screen**:
  - Live Activity appears on Lock Screen.
  - Dynamic Island appears on Dynamic Island devices.

Tip: the island can be easier to notice **outside** the foreground app.

---

## 9) Debugging: what to look for in the Xcode console

This repo includes logs that explain the most common failure modes.

### A) Swift class not found (Target Membership not set)

If you see logs like:

- `[DroidStar][LiveActivity] LiveActivityManager class not found...`

Then `LiveActivityManager.swift` is **not** compiled into the main app target.
Re-check **Target Membership** for:

- `LiveActivityManager.swift` → ✅ DroidStar
- `DroidStarActivityAttributes.swift` → ✅ DroidStar

### B) Live Activities disabled by system

If you see logs like:

- `areActivitiesEnabled == false`

Then enable Live Activities in Settings as described above.

### C) Activity.request failed

If you see:

- `[DroidStar][LiveActivity] Error starting Live Activity: ...`

The error text usually indicates missing entitlements/settings, OS version issues, or an extension embed/signing problem.

---

## 10) Persistence warning (again)

If you re-run iOS `qmake` and regenerate `DroidStar.xcodeproj`, you will need to:

- recreate the extension target
- re-embed the `.appex`
- re-set Swift Target Membership
- re-remove Qt linkage from the extension

If you want regeneration-safe setup, use a separate extension Xcode project and an `.xcworkspace` (so qmake regen won’t delete the extension project).

