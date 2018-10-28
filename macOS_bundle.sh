#!/usr/bin/env bash

VER_BBP="${1}";
VER_BBP_PREV="${2}"

# backup previous dmg just in case...
mv biblepaycore.dmg biblepaycore-"${VER_BBP}".dmg

# create icon icns file from png
#
sips -s format icns src/qt/res/icons/bitcoin.png --out biblepay.icns

# create Info.plist file
#
echo '<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>English</string>
	<key>CFBundleExecutable</key>
	<string>Biblepay Core</string>
	<key>CFBundleIconFile</key>
	<string>biblepay.icns</string>
	<key>CFBundleIdentifier</key>
	<string>org.biblepay.biblepay-qt</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>Biblepay Core</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>'"${VER_BBP}"'</string>
	<key>CFBundleVersion</key>
	<string>1</string>
</dict>
</plist>' > Info.plist

#
# prepare .app bundle
#
APPNAME="Biblepay Core";
DIR="${APPNAME}.app/Contents/MacOS";
DIR2="${APPNAME}.app/Contents";
QT_PATH="/usr/local/opt/qt/lib"
QT_VERSION=5
QT_LOCAL="/usr/local/Cellar/qt/5.11.0/lib"

if [ -a "${APPNAME}.app" ]; then
	echo "${PWD}/${APPNAME}.app already exists, deleting...";
	rm -rf "${APPNAME}.app" 
	#exit 1;
fi;

mkdir -p "${DIR}";
mkdir -p "${DIR2}/Resources";
mkdir -p "${DIR2}/Frameworks";

cp "Info.plist" "${DIR2}/Info.plist";
cp "biblepay.icns" "${DIR2}/Resources/biblepay.icns";
cp "src/qt/biblepay-qt" "${DIR}/${APPNAME}";
chmod +x "${DIR}/${APPNAME}";

echo "${PWD}/$APPNAME.app";

# codesign 
#
: <<'COMMENT'
codesign -s "Developer ID Application: F.J. Ortiz (QC6288YCNV)" --verbose=4 --keychain "/Users/mippl/Library/Keychains/login.keychain" "./${DIR}/Biblepay Core"
COMMENT

/usr/local/Cellar/qt/5.11.0/bin/macdeployqt "Biblepay Core.app" -codesign="Developer ID Application: F.J. Ortiz (QC6288YCNV)"

codesign -vvv -d "Biblepay Core.app"

# create DMG
#
hdiutil create -volname "Biblepay Core" -srcfolder "Biblepay Core.app" -ov -format UDZO "biblepaycore.dmg"


