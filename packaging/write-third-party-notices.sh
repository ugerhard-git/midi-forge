#!/usr/bin/env bash
# Writes THIRD-PARTY-NOTICES.txt for a release zip: the licences of the third-party code compiled into the
# plug-ins, taken from the fetched sources, so the file always matches what was built.
#
# Usage: packaging/write-third-party-notices.sh <dependency folder> <output file> [--with-audio-unit]
#   <dependency folder>  CMake's FETCHCONTENT_BASE_DIR (holds juce-src and clap_juce_extensions-src)
#   --with-audio-unit    also add Apple's Audio Unit SDK (macOS builds)
set -euo pipefail

if [ $# -lt 2 ]; then
    echo "Usage: $0 <dependency folder> <output file> [--with-audio-unit]" >&2
    exit 2
fi
dependencies="$1"
output="$2"
withAudioUnit="${3:-}"

juce="$dependencies/juce-src"
clapExtensions="$dependencies/clap_juce_extensions-src"

# Appends one component: its name, where it comes from, what it is used for, and its licence text.
addNotice() {
    local name="$1" source="$2" usage="$3" licenceFile="$4"
    if [ ! -f "$licenceFile" ]; then
        echo "Licence file missing: $licenceFile" >&2
        exit 1
    fi
    {
        echo "================================================================================"
        echo "$name"
        echo "$source"
        echo "Used for: $usage"
        echo "================================================================================"
        echo
        tr -d '\r' < "$licenceFile"
        echo
        echo
    } >> "$output"
}

cat > "$output" << 'HEADER'
MIDI Forge - third-party notices
================================

MIDI Forge is free software under the GNU Affero General Public License, version 3 (see LICENSE.txt). It is
built with JUCE (https://juce.com), which is used under the same licence. The plug-ins also contain the
following third-party code, each under its own licence, reproduced below.

HEADER

addNotice "VST 3 SDK (Steinberg Media Technologies GmbH)" "https://github.com/steinbergmedia/vst3sdk" \
    "the VST3 plug-in" "$juce/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"
addNotice "clap-juce-extensions" "https://github.com/free-audio/clap-juce-extensions" \
    "the CLAP plug-in" "$clapExtensions/LICENSE.md"
addNotice "CLAP" "https://github.com/free-audio/clap" \
    "the CLAP plug-in" "$clapExtensions/clap-libs/clap/LICENSE"
addNotice "clap-helpers" "https://github.com/free-audio/clap-helpers" \
    "the CLAP plug-in" "$clapExtensions/clap-libs/clap-helpers/LICENSE"
if [ "$withAudioUnit" = "--with-audio-unit" ]; then
    addNotice "Audio Unit SDK (Apple Inc.)" "https://github.com/apple/AudioUnitSDK" \
        "the Audio Unit plug-in" "$juce/modules/juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt"
fi

echo "Wrote $output"
