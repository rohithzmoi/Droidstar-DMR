#!/bin/bash
# Script to fix Swift version in Xcode project after qmake generation
# Usage: ./fix_swift_version.sh [path_to_xcodeproj]

XCODEPROJ="${1:-build_ios/DroidStar.xcodeproj}"
if [ ! -d "$XCODEPROJ" ]; then
    echo "Error: Xcode project not found at $XCODEPROJ"
    exit 1
fi

PBXPROJ="$XCODEPROJ/project.pbxproj"
if [ ! -f "$PBXPROJ" ]; then
    echo "Error: project.pbxproj not found"
    exit 1
fi

echo "Fixing Swift version in $XCODEPROJ..."

# Set SWIFT_VERSION to 5.0 for all build configurations (both project and target level)
# This handles empty strings, existing values, and ensures it's set everywhere
sed -i '' 's/SWIFT_VERSION = "";/SWIFT_VERSION = 5.0;/g' "$PBXPROJ"
sed -i '' 's/SWIFT_VERSION = "[^"]*";/SWIFT_VERSION = 5.0;/g' "$PBXPROJ"

# Also ensure it's set in buildSettings dictionaries (for all configurations)
# Add SWIFT_VERSION if it doesn't exist in buildSettings sections
perl -i -pe 's/(buildSettings = \{)/$1\n\t\t\t\tSWIFT_VERSION = 5.0;/g if /buildSettings = \{/ && !/SWIFT_VERSION/;' "$PBXPROJ"

# Ensure CLANG_ENABLE_MODULES is set
sed -i '' 's/CLANG_ENABLE_MODULES = "";/CLANG_ENABLE_MODULES = YES;/g' "$PBXPROJ"
sed -i '' 's/CLANG_ENABLE_MODULES = "[^"]*";/CLANG_ENABLE_MODULES = YES;/g' "$PBXPROJ"

# Ensure ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES is set
sed -i '' 's/ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES = "";/ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES = YES;/g' "$PBXPROJ"
sed -i '' 's/ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES = "[^"]*";/ALWAYS_EMBED_SWIFT_STANDARD_LIBRARIES = YES;/g' "$PBXPROJ"

echo "Swift version set to 5.0 in all configurations"
echo "Done! Please rebuild in Xcode."
