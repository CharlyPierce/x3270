import os
from dotenv import load_dotenv

load_dotenv()
USERID = os.getenv("USERID", "IBMUSER")  # Leer desde .env o usar valor predeterminado
PASSWORD = os.getenv("PASSWORD", "IBMUSER")  # Leer desde .env o usar valor predeterminado

acct = "send"

def crear_script(nombre_archivo, ruta_host, nombre_componente):
    # Asegurarse de que el nombre del archivo esté en mayúsculas
    nombre_cpt = nombre_archivo
    # if(acct!='receive'):
    #     nombre_cpt =  nombre_archivo
    #     ruta_host = ruta_host + '(' + nombre_componente 

    if "IBMUSER.PS.ARCHIVO" in ruta_host:
        # Obtener el nombre sin la extensión
        nombre_cpt_sin_extension = '.'.join(nombre_cpt.split('.')[:-1])  # Elimina la última parte después del punto
        # Construir la nueva ruta
        ruta_host = f"{ruta_host}.{nombre_cpt_sin_extension}"

    elif ("IBMUSER.PO" in ruta_host) or ("IBMUSER.COBOL.COPYS" in ruta_host):  # Verifica si ruta_host contiene IBMUSER.PO._LO_QUE_SEA
        # Actualizar variables como se indica
        nombre_cpt = nombre_archivo
        ruta_host = f"{ruta_host}({nombre_componente})"

    else:
        # Manejo del error
        raise ValueError("Error: ruta_host no contiene un formato válido (IBMUSER.PS.ARCHIVO o IBMUSER.PO._LO_QUE_SEA)")



    # Ruta del archivo script.s3270
    ruta_script = ".script/script.s3270"
    print('<-------------->')
    print('ACCION: ', acct)
    print('User: ',USERID)
    print('Ruta host: ',ruta_host)
    print('<-------------->')
    # Contenido del archivo
    contenido = f'''Wait(40, stringAt, 1, 1, "z/OS")
String("LOGON {USERID}")
Enter()
Wait(10, stringAt, 4, 5, "Enter LOGON")
MoveCursor1(8,20)
String("{PASSWORD}")
Enter()
Wait(25, stringAt, 24, 2, "***")
Enter()
Wait(10, stringAt, 4, 2, "Option")
String("TSO PROFILE NOPREFIX")
Enter()
Wait(10, stringAt, 4, 2, "Option")
String("6")
Enter()
Wait(10, stringAt, 4, 2, "Enter")
MoveCursor1(6,8)
Transfer(direction={acct},localfile={nombre_cpt},hostfile={ruta_host},mode=ascii,exist=replace,host=tso)
Wait(0.2,seconds)
Enter()
Enter()
Enter()
Wait(0.2,seconds)
PF(3)
PF(3)
PF(3)
Wait(0.2,seconds)
Enter()
Wait(0.2,seconds)
PF(3)
Wait(10, stringAt, 5, 2, "Process")
String("2")
Wait(0.2,seconds)
Enter()
Wait(0.2,seconds)
String("logoff")
Wait(0.2,seconds)
Enter()
Wait(10, stringAt, 1, 1, "z/OS")
EOF'''

    # Crear o sobrescribir el archivo
    with open(ruta_script, "w") as archivo:
        archivo.write(contenido)

    with open(ruta_script, "r") as archivo:
        contenido_leido = archivo.readlines()
        print(":::::::::::::::::::::::::::::::::::::::::::::::::::::::")
        print(contenido_leido[17].strip())
        print(" [ WAITING ]  ENVIANDO DATOS  ...")
        print(":::::::::::::::::::::::::::::::::::::::::::::::::::::::")
