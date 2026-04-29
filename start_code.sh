#!/bin/bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 {run/compile/reverse} <file>"
    exit 1
fi

ACTION=$1   
INPUT_FILE=$2 

TXT_FILE="${INPUT_FILE%.*}.txt"
ASM_FILE="${INPUT_FILE%.*}.asm"
BIN_FILE="${INPUT_FILE%.*}.bin"
REVERSE_OUT="${INPUT_FILE%.*}_reversed.vdm"

case "$ACTION" in
    "run")
        # make > /dev/null
        ./frontend.out $INPUT_FILE $TXT_FILE
        echo "front" 
        #./middle.out $TXT_FILE $TXT_FILE
        ./backend.out $TXT_FILE $ASM_FILE
        nasm -f elf64 $ASM_FILE -o $BIN_FILE
        ld $BIN_FILE -o prog.out
        ./prog.out
        ;;
    
    "compile")
        ./frontend.out $INPUT_FILE $TXT_FILE
        ./backend.out $TXT_FILE $ASM_FILE
        ./asm.out $ASM_FILE $BIN_FILE
        echo -e "compile to $BIN_FILE"
        ;;

    "reverse")
        ./frontend.out $INPUT_FILE $TXT_FILE
        ./reverse_end.out $TXT_FILE $REVERSE_OUT
        ;;        

    *)
        echo "Unknown operation: $ACTION"
        echo "Usage: $0 {run/compile/reverse} <file>"
        exit 1
        ;;
esac
