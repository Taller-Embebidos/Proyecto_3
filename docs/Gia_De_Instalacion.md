# Instalación y Generación de Imagen FreeRTOS para ESP32-DevKitC-V4

## 1. Objetivo

Este documento describe el procedimiento completo para:

1. Instalar el entorno de desarrollo **ESP-IDF** en Linux dentro de la carpeta **proyectoESP**.  
2. Crear un proyecto basado en **FreeRTOS para ESP32** dentro de esa misma carpeta.  
3. Integrar el código del detector de rayos (GPIO + ISR + WiFi + NTP + MQTT + NVS).  
4. Generar la **imagen de firmware** (`.bin`) del proyecto.  
5. Flashear dicha imagen en un **ESP32-DevKitC-V4**.

El resultado será un firmware completamente funcional, basado en FreeRTOS, con publicación MQTT hacia ThingsBoard.

---

## 2. Requisitos

### Hardware  
- ESP32-DevKitC-V4  
- Cable USB-A a Micro-USB  
- Punto de acceso WiFi disponible  

### Software  
- Linux (Fedora, Ubuntu o similar)  
- Git  
- Python 3  
- Toolchain ESP-IDF (se instala más adelante)  
- Editor (VS Code, nano, vim)

Dependencias mínimas necesarias

```bash
sudo dnf install cmake ninja-build python3 python3-pip git wget flex bison gperf
```
---

## 3. Instalar ESP-IDF

1. Crear la carpeta de trabajo principal **proyectoESP**:

```bash
mkdir -p ~/proyectoESP
cd ~/proyectoESP
```

2. Clonar ESP-IDF:

```bash
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
```

3. Instalar dependencias y toolchains:

```bash
./install.sh esp32
```

4. Activar ESP-IDF (este comando debe ejecutarse **siempre antes de compilar**):

```bash
source ./export.sh
```

Esto agrega todas las herramientas (compiladores, cmake, python env) al entorno.

---

## 4. Crear un Proyecto Nuevo

Ejecutar:

```bash
cd ~/proyectoESP
mkdir -p proyectos_esp32
cd proyectos_esp32

idf.py create-project rayos_esp32
cd rayos_esp32
```

Esto genera la estructura inicial:

```
rayos_esp32/
│── CMakeLists.txt
└── main/
    ├── CMakeLists.txt
    └── main.c  ← ejemplo inicial
```

---

## 5. Reemplazar `main.c` con el Código del Proyecto

1. Abrir el archivo:

```bash
nano main/main.c
```

2. Borrar todo el contenido.  
3. Pegar el código completo entregado en este repositorio (versión ESP-IDF del detector de rayos).  
   Este archivo implementa:

- GPIO4 + interrupción  
- Antirrebote por tiempo  
- WiFi STA  
- SNTP (zona horaria GMT-6)  
- Cliente MQTT (ThingsBoard)  
- NVS para persistencia  
- Publicación del JSON: `id_rayo`, `disp_id`, `acumulado_rayos`, `epoch`, `fecha`, `hora`  

Guardar cambios y cerrar.

---

## 6. Configurar Credenciales

En `main.c` modificar si es necesario:

```c
static const char *WIFI_SSID = "SU_RED";
static const char *WIFI_PASS = "SU_PASSWORD";

static const char *TB_TOKEN = "TOKEN_DE_THINGSBOARD";
static const uint32_t DEVICE_ID = 1;
```

El token de ThingsBoard se usa como **username** en la conexión MQTT.

---

## 7. Compilar el Firmware (Imagen)

### 7.1 Establecer el target ESP32

Antes de compilar activar ESP-IDF desde:

```bash
cd ~/proyectoESP/esp-idf
source ./export.sh
```

Luego:

```bash
cd ~/proyectoESP/proyectos_esp32/rayos_esp32
idf.py set-target esp32
```

### 7.2 Compilar

```bash
idf.py build
```

Si todo es correcto, aparecerá:

```
Project build complete.
```

Los binarios finales estarán en `build/`:

```
build/
 ├── bootloader/bootloader.bin
 ├── partition_table/partition-table.bin
 └── rayos_esp32.bin   ← Firmware principal
```

Estos tres `.bin` forman la **imagen de firmware del proyecto**.

---

## 8. Flashear la Imagen al ESP32

1. Conectar la placa por USB.  
2. Identificar el puerto:

```bash
ls /dev/ttyUSB*
```

Ejemplo: `/dev/ttyUSB0`.

3. Flashear:

```bash
idf.py -p /dev/ttyUSB0 flash
```

4. Abrir monitor serie:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Atajos:
- Salir: `Ctrl + ]`  
- Reset del ESP32: botón EN/RST de la placa  

---

## 9. Estructura Final del Proyecto

```
~/proyectoESP/
│── esp-idf/
│
└── proyectos_esp32/
    └── rayos_esp32/
        │── CMakeLists.txt
        │── sdkconfig
        │── build/                     ← Imagen del firmware
        └── main/
            ├── CMakeLists.txt
            └── main.c                 ← Código fuente del proyecto
```

---

## 10. Empaquetar la Imagen para Entregar

Se recomienda entregar los `.bin` en un archivo `.zip`:

```bash
cd build
zip rayos_firmware.zip bootloader/bootloader.bin partition_table/partition-table.bin rayos_esp32.bin
```

---

## 11. Notas Importantes

- ESP-IDF ya incluye FreeRTOS; no es necesario instalarlo aparte.  
- El sistema sigue contando rayos sin WiFi ni MQTT; los datos quedan guardados en NVS.  
- Al recuperar conectividad, la publicación a ThingsBoard se reanudará.  
- El GPIO4 está configurado con **pull-up** + interrupción **FALLING**.  
- El antirrebote se maneja por tiempo usando `esp_timer_get_time()`.

---

## 12. Referencias

- Documentación oficial ESP-IDF: https://docs.espressif.com/projects/esp-idf  
- Cliente MQTT ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html  
- NVS API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html  
