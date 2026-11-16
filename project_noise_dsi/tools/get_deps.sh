#!/bin/sh
# Downloads the single-header MP3 decoder used by PROJECT NOISE DSi.
# Pin a commit with MINIMP3_REF=<commit> sh tools/get_deps.sh for reproducible builds.
set -e
REF="${MINIMP3_REF:-master}"
cd "$(dirname "$0")/../source"
curl -fsSL -o minimp3.h "https://raw.githubusercontent.com/lieff/minimp3/${REF}/minimp3.h"
echo "minimp3.h fetched (ref: ${REF})"
