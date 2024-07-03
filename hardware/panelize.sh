#!/usr/bin/env bash

MODULE=${1%/}
OUTPUT=$2
PANELIZE=$3
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
MODULE_DIR="${SCRIPT_DIR}/${MODULE}"
PCB="${MODULE_DIR}/${MODULE}.kicad_pcb" 

if [ -z "$MODULE" ]; then
    echo "Usage: $0 <module> [output-dir]"
    exit 1
fi

if [ -z "$OUTPUT" ]; then
    OUTPUT=$HOME/pdusb-panelized
fi

if [ -z "$PANELIZE" ]; then
    kikit panelize -p "${MODULE_DIR}/panelize.json" -p :jlcTooling "${MODULE_DIR}/${MODULE}.kicad_pcb" "$OUTPUT/panel.kicad_pcb"
    PCB="$OUTPUT/panel.kicad_pcb"
fi
kikit fab jlcpcb --no-drc --assembly --schematic "${MODULE_DIR}/${MODULE}.kicad_sch" "$PCB" "$OUTPUT/"