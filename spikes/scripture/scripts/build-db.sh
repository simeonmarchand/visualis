#!/usr/bin/env bash
# Downloads KJV and WEB (both public domain) from eBible.org in USFX and builds data/bible.db.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p data/download
for t in eng-kjv2006 eng-web; do
    [[ -f data/download/${t}_usfx.zip ]] || curl -sSfL -A "visualis-spike" -o "data/download/${t}_usfx.zip" "https://ebible.org/Scriptures/${t}_usfx.zip"
    rm -rf "data/download/${t}_usfx" && unzip -q "data/download/${t}_usfx.zip" -d "data/download/${t}_usfx"
done
rm -f data/bible.db
# -I: the downloaded files are data; don't let anything near them change how Python runs.
python3 -I import_usfx.py data/download/eng-kjv2006_usfx/eng-kjv2006_usfx.xml data/bible.db \
    --abbrev KJV --name "King James Version" --license public-domain \
    --attribution "King James Version (public domain)" 2>/dev/null
python3 -I import_usfx.py data/download/eng-web_usfx/eng-web_usfx.xml data/bible.db \
    --abbrev WEB --name "World English Bible" --license public-domain \
    --attribution "World English Bible (public domain)" 2>/dev/null
