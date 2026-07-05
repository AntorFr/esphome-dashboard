#!/usr/bin/env bash
# Interim patch for the mono-MP3 playback regression in the ESPHome speaker media_player.
#
# Symptom: mono MP3 audio (local files, HA media, and — crucially — Home Assistant TTS, which is
# mono) decodes its header then produces ZERO samples, so nothing plays. Stereo MP3 and FLAC work.
# Root cause: audio_decoder.cpp sized the MP3 output buffer as samples_per_frame * channels, which
# is too small for mono, so the frame never decodes.
#
# Known issue:  https://github.com/esphome/esphome/issues/16829
# Official fix: https://github.com/esphome/esphome/pull/17106  ("[audio] Fix mono channel MP3 playback")
#
# The fix is merged on `dev` and ships in ESPHome 2026.7.0. Until that release is installed, run this
# script once after (re)creating the venv. It is idempotent and a no-op once the fix is present
# upstream. Remove this script (and this step) after upgrading to >= 2026.7.0.
set -euo pipefail

VENV="${1:-.venv}"
F=$(find "$VENV" -path '*/esphome/components/audio/audio_decoder.cpp' 2>/dev/null | head -1)
if [[ -z "${F}" ]]; then
  echo "audio_decoder.cpp not found under ${VENV} — is the venv set up?" >&2
  exit 1
fi

python3 - "$F" <<'PY'
import sys
path = sys.argv[1]
src = open(path).read()
buggy = """  } else if (result == micro_mp3::MP3_OUTPUT_BUFFER_TOO_SMALL) {
    // Reallocate to decode the frame on the next call
    if (this->mp3_decoder_->get_channels() > 0) {
      this->free_buffer_required_ =
          this->mp3_decoder_->get_samples_per_frame() * this->mp3_decoder_->get_channels() * sizeof(int16_t);
    } else {
      // Fallback to worst-case size if channel info isn't available
      this->free_buffer_required_ = this->mp3_decoder_->get_min_output_buffer_bytes();
    }"""
fixed = """  } else if (result == micro_mp3::MP3_OUTPUT_BUFFER_TOO_SMALL) {
    // Fallback to worst-case size
    this->free_buffer_required_ = this->mp3_decoder_->get_min_output_buffer_bytes();"""
if buggy not in src:
    if fixed in src:
        print("Already fixed (or on ESPHome >= 2026.7.0):", path)
        sys.exit(0)
    print("Neither buggy nor fixed block found — ESPHome layout changed; re-check PR #17106.",
          file=sys.stderr)
    sys.exit(1)
open(path, "w").write(src.replace(buggy, fixed))
print("Patched mono-MP3 fix into", path)
PY
