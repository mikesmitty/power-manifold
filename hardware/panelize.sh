#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
MODULE=${1%/}
MODULE_DIR="${SCRIPT_DIR}/${MODULE}"
OUTPUT="${MODULE_DIR}/production"

if [ -z "$MODULE" ]; then
    echo "Usage: $0 <module> [panel-option|single]"
    exit 1
fi

if [ -n "$2" ] && [ "$2" != "single" ]; then
    BATCH_MOD="-p ${MODULE_DIR}/panelize-${2}.json"
fi

mkdir -p "$OUTPUT"
if [ "$2" != "single" ] && [ -e "${MODULE_DIR}/panelize.json" ]; then
    kikit panelize -p "${MODULE_DIR}/panelize.json" $BATCH_MOD -p :jlcTooling "${MODULE_DIR}/${MODULE}.kicad_pcb" "${OUTPUT}/${MODULE}.kicad_pcb"
    kikit fab jlcpcb --no-drc --assembly --autoname --schematic "${MODULE_DIR}/${MODULE}.kicad_sch" "${OUTPUT}/${MODULE}.kicad_pcb" "$OUTPUT/"
else
    kikit fab jlcpcb --no-drc --assembly --autoname --schematic "${MODULE_DIR}/${MODULE}.kicad_sch" "${MODULE_DIR}/${MODULE}.kicad_pcb" "$OUTPUT/"
fi
