#!/usr/bin/env python3
from dotenv import load_dotenv
from makes import crear_script
import subprocess
import os.path
import shutil
import time
import sys
import os
from colorama import Fore, Style, init
import time
import math



# Inicializa colorama
init()
# Define las palabras clave a buscar
palabras_clave = ["data: Transfer(): File exists", "error"]

def main():
    # Cargar variables de entorno
    load_dotenv()
    inicio = time.time()

    USERID = os.getenv("USERID", "IBMUSER")
    PASSWORD = os.getenv("PASSWORD", "IBMUSER") 
    IP = os.getenv("IP", "192.168.100.96")
    PUERTO = os.getenv("PUERTO", "23")
    HOST = f"{IP}:{PUERTO}"
    ruta_actual = os.getcwd()

    if len(sys.argv) < 3:
        print("Uso: python3 upl.py <nombre_componente> <ruta_local>")
        sys.exit(1)

    nombre_cpt = sys.argv[1]
    nombre_componente = nombre_cpt.upper()
    ruta_local = sys.argv[2]

    # Extraer el nombre del archivo de la ruta local
    nombre_archivo = os.path.basename(ruta_local)

    # Extraer la carpeta padre de la ruta local
    carpeta_pad = ruta_actual+'/'+nombre_archivo
    carpeta_padre = os.path.basename(os.path.dirname(ruta_local)).lower()

    # Mapeo de carpetas válidas a rutas
    mapping = {
        'src':  'IBMUSER.PO.SRC',
        'prc':  'IBMUSER.PO.PRC',
        'ctc':  'IBMUSER.PO.CTC',
        'rus':  'IBMUSER.PO.RUS',
        'jcl':  'IBMUSER.PO.JCL',
        'copys': 'IBMUSER.COBOL.COPYS',
        'archivo': 'IBMUSER.PS.ARCHIVO'
    }

    if carpeta_padre not in mapping:
        print(f"Error: La carpeta padre '{carpeta_padre}' no es válida. Use una de: {', '.join(mapping.keys())}")
        sys.exit(1)

    ruta_host      = mapping[carpeta_padre]
    ruta_host_comp = ruta_host

    # Copiar el archivo
    try:
        shutil.copy(ruta_local, ruta_actual)
        print(f"Archivo copiado temporalmente a: {ruta_actual}")
    except Exception as e:
        print(f"Error al copiar el archivo: {e}")
        sys.exit(1)
        
    # Imprimir las variables recibidas
    print(f"Nombre del componente: {nombre_componente}")
    print(f"Ruta Archivo Origen: {ruta_local}")
    print(f"Libreria Componente: {carpeta_padre}")
    print(f"Ruta libreria: {ruta_host}")
    print(f"Nombre del archivo: {nombre_archivo}")

    crear_script(nombre_archivo, ruta_host_comp,nombre_componente)
    time.sleep(5)
    # Crear y ejecutar el comando x3270
    
    command = f"s3270 -script {HOST} < {ruta_actual}/.script/script.s3270"

    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, text=True)
    out, err = process.communicate()

    # Mostrar los resultados
    # print("Salida x3270:", out)
    # print("Errores x3270:", err)


    lineas = out.strip().split("\n")

    # Variable para rastrear si hay coincidencia de "Transfer complete"
    coincidencia = False

    # Recorre las líneas y busca "Transfer complete"
    for linea in lineas:
        if "Transfer complete" in linea:
            coincidencia = True
            print(Fore.GREEN + "ARCHIVOS ENVIADOS CORRECTAMENTE" + Style.RESET_ALL)
            break  # Sal del bucle si encuentras la coincidencia
        else:
            # print(Fore.YELLOW + linea + Style.RESET_ALL)
            pass

    # Si no se encontró "Transfer complete", mostrar mensaje de error
    if not coincidencia:
        print(Fore.RED + "ERROR EN EL ENVÍO" + Style.RESET_ALL)



    #Eliminar el archivo temporal
    try:
        os.remove(carpeta_pad)
        print(f"Archivo temporal eliminado: {carpeta_pad}")
    except Exception as e:
        print(f"Error al eliminar el archivo temporal: {e}")

    fin = time.time()
    tiempo_total = fin - inicio
    tiempo_total = math.ceil(tiempo_total)

    print(Fore.BLUE + f"TIEMPO TOTAL: {tiempo_total} s" + Style.RESET_ALL)
if __name__ == "__main__":
    main()