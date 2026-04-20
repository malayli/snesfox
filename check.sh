#!/bin/bash

CPU_FILE="cpu.cpp"
OPCODES_FILE="opcodes.cpp"

if [ ! -f "$CPU_FILE" ]; then
    echo "Erreur: fichier introuvable: $CPU_FILE"
    exit 1
fi

if [ ! -f "$OPCODES_FILE" ]; then
    echo "Erreur: fichier introuvable: $OPCODES_FILE"
    exit 1
fi

# Tous les opcodes déclarés dans la table opcodes.cpp
all_opcodes=$(
    grep -E '0x[0-9A-Fa-f]{2}' "$OPCODES_FILE" \
    | grep -o '0x[0-9A-Fa-f]\{2\}' \
    | tr '[:lower:]' '[:upper:]' \
    | sort -u
)

# Tous les opcodes présents dans CPU::step()
implemented=$(
    grep -o 'case 0x[0-9A-Fa-f]\{2\}' "$CPU_FILE" \
    | awk '{print $2}' \
    | tr '[:lower:]' '[:upper:]' \
    | sort -u
)

echo "=== Missing Opcodes ==="
echo

missing_list=()

for op in $all_opcodes; do
    if ! echo "$implemented" | grep -q "^$op$"; then
        missing_list+=("$op")
    fi
done

if [ ${#missing_list[@]} -eq 0 ]; then
    echo "None"
else
    for op in "${missing_list[@]}"; do
        echo "$op"
    done
fi

echo
echo "Total missing: ${#missing_list[@]}"