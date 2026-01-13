# Fix Xcode Info.plist Duplicate Error

## Problem

After running `qmake` to regenerate the iOS Xcode project, you may encounter this error:

```
Multiple commands produce '/Volumes/Personal/Droidstar/DroidStar/build_ios/Debug-iphonesimulator/DroidStarLiveActivityExtensionExtension.appex/Info.plist'
```

This happens because Xcode automatically adds `Info.plist` to the "Copy Bundle Resources" build phase, but it's already being processed as the target's `INFOPLIST_FILE`.

## Quick Manual Fix (in Xcode)

1. Open the project in Xcode
2. Select **DroidStarLiveActivityExtensionExtension** target
3. Go to **Build Phases** tab
4. Expand **Copy Bundle Resources**
5. Find and remove `Info.plist` (click minus button)
6. Clean Build Folder (Cmd+Shift+K)
7. Build (Cmd+B)

## Automated Fix Script

Run this script after each `qmake` to automatically fix the issue:

### macOS/Linux Script

```bash
#!/bin/bash
# fix_xcode_info_plist.sh
# Run this after qmake to fix the Info.plist duplicate error

PROJECT="/Volumes/Personal/Droidstar/DroidStar/build_ios/DroidStar.xcodeproj/project.pbxproj"

if [ ! -f "$PROJECT" ]; then
    echo "Error: Xcode project file not found at $PROJECT"
    echo "Make sure you've run qmake first"
    exit 1
fi

# Remove Info.plist from Resources build phase for the extension
# This removes lines like: F7A593682F15EABC0075C08B /* Info.plist in Resources */
sed -i '' '/Info\.plist in Resources/d' "$PROJECT"

# Also remove the reference from the Copy Bundle Resources section
# This removes lines like: F7A593682F15EABC0075C08B /* Info.plist in Resources */,
sed -i '' '/Info\.plist in Resources.*\/\* Resources \*\//d' "$PROJECT"

echo "✅ Fixed Info.plist duplicate in Xcode project"
echo "You can now open the project in Xcode and build"
```

### Usage

1. Make the script executable:
   ```bash
   chmod +x scripts/fix_xcode_info_plist.sh
   ```

2. Run after qmake:
   ```bash
   cd /Volumes/Personal/Droidstar/DroidStar
   ./scripts/fix_xcode_info_plist.sh
   ```

### Alternative: One-liner

If you prefer a one-liner, you can add this to your build script:

```bash
# After qmake
sed -i '' '/Info\.plist in Resources/d' build_ios/DroidStar.xcodeproj/project.pbxproj
```

## Why This Happens

The extension's `Info.plist` is used in two ways:
1. ✅ **INFOPLIST_FILE** (in Build Settings) - This is correct and needed
2. ❌ **Copy Bundle Resources** - This is wrong and causes the duplicate

When qmake regenerates the project, Xcode's auto-discovery adds all extension files (including Info.plist) to "Copy Bundle Resources" automatically, causing the conflict.

## Notes

- The script uses `sed -i ''` which is macOS syntax. For Linux, use `sed -i` (without the empty string)
- Always run this **after** qmake and **before** opening in Xcode
- The fix is safe - it only removes the duplicate resource reference, not the actual Info.plist file
