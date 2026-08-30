#!/usr/bin/env bash

REPO_DIR="$(git rev-parse --show-toplevel)"
LCSC_ID=$1

if [ -z "$LCSC_ID" ]; then
    echo "Usage: $0 <lcsc_id>"
    exit 1
fi

if [ ! -d "${REPO_DIR}/hardware/libraries" ]; then
    echo "libraries folder does not exist"
    exit 1
fi

cd "${REPO_DIR}/hardware/libraries"
easyeda2kicad --full --project-relative --overwrite --output "${REPO_DIR}/hardware/libraries/Local Library" --lcsc_id "$LCSC_ID"

# easyeda2kicad emits model paths relative to this directory, but KIPRJMOD is
# the project dir (hardware/<project>), so point the reference back up here.
sed -i.bak 's|${KIPRJMOD}/Local Library.3dshapes|${KIPRJMOD}/../libraries/Local Library.3dshapes|' "Local Library.pretty/"*.kicad_mod
rm -f "Local Library.pretty/"*.kicad_mod.bak
