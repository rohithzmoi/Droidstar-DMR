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
