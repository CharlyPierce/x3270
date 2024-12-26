#!/bin/bash

# Definición de colores ANSI
RED='\033[0;31m'   # Rojo para errores
GREEN='\033[0;32m' # Verde para éxito
NC='\033[0m'       # Reset (sin color)

# $1 = fileDirname
# $2 = fileBasenameNoExtension
# $3 = file (el archivo .cbl)
# A partir de $4 se pasan las opciones adicionales (ej. -I rutas)

# Capturar la salida del comando, incluyendo stderr
OUTPUT=$(cobc -x -o "$1/$2" "$3" "${@:4}" 2>&1)
EXIT_CODE=$?

# Verificar el código de salida
if [ $EXIT_CODE -eq 0 ]; then
  # Compilación exitosa
  echo -e "${GREEN}${OUTPUT}${NC}"
  echo -e "${GREEN}Compilación exitosa${NC}"
else
  # Compilación con errores
  # Usamos echo sin -e para imprimir exactamente como viene la salida,
  # respetando los números de línea y el formato.
  echo "${RED}${OUTPUT}${NC}"
  echo -e "${RED}Error en la compilación${NC}"
fi

# Eliminar automáticamente el archivo compilado si existe
if [ -f "$1/$2" ]; then
  rm "$1/$2"
  echo -e "${GREEN}Archivo compilado eliminado automáticamente: $1/$2${NC}"
fi