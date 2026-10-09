#!/bin/bash
# PITCH installer: double-click this file (it sits next to PITCH.component and PITCH.vst3).
cd "$(dirname "$0")"
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/Components/PITCH.component ~/Library/Audio/Plug-Ins/VST3/PITCH.vst3
cp -R PITCH.component ~/Library/Audio/Plug-Ins/Components/
cp -R PITCH.vst3 ~/Library/Audio/Plug-Ins/VST3/
# the plug-in is not signed by Apple: tell macOS it is safe to open
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/PITCH.component ~/Library/Audio/Plug-Ins/VST3/PITCH.vst3
killall -9 AudioComponentRegistrar 2>/dev/null
echo ""
echo "PITCH installed (Audio Unit + VST3). Open Ableton and rescan plug-ins."
