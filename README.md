# Medidor de CO2 con MQ-135 y ESP32

Prototipo de un dispositivo portatil para medir la calidad del aire interior
mediante la concentracion de dioxido de carbono (CO2), con indicadores visuales
y sonoros de alerta.

## Caracteristicas

- Lectura de CO2 con sensor **MQ-135** conectado al ADC1 del **ESP32**.
- **Calibracion automatica** del RZero con precalentado, ventana de muestreo y
  mediana, guardada en la memoria NVS del ESP32.
- Filtrado **EMA** de las lecturas para estabilizar la medida.
- Pantalla **OLED SSD1306 (128x64, I2C)** con lectura instantanea, resistencia,
  RZero, valor ADC y barra de nivel.
- **Semaforo** con 3 LEDs: verde (< 800 ppm), amarillo (800 - 1199 ppm),
  rojo (>= 1200 ppm).
- **Buzzer** de alerta intermitente en zona roja y pitido preventivo cada 3
  minutos en zona amarilla.
- **Boton BOOT** (GPIO0) mantenido 2 segundos para forzar recalibracion.
- **Consola serie** (115200 baudios) con comandos `c` (calibrar), `p` (print)
  y `r` (raw).

## Hardware

| Componente | Conexion |
|---|---|
| ESP32 DevKit v1 | - |
| Sensor MQ-135 | Salida analogica -> GPIO34, VCC -> 3V3, GND -> GND |
| Pantalla OLED SSD1306 (I2C) | SDA -> GPIO21, SCL -> GPIO22, VCC -> 3V3, GND -> GND |
| LED verde | Anodo -> resistencia 220R -> GPIO25, catodo -> GND |
| LED amarillo | Anodo -> resistencia 220R -> GPIO26, catodo -> GND |
| LED rojo | Anodo -> resistencia 220R -> GPIO27, catodo -> GND |
| Buzzer TMB12A05 | Positivo -> GPIO14, negativo -> GND |
| Boton BOOT | GPIO0 (activo en bajo, pull-up interno) |

## Instalacion de las librerias

El proyecto usa **PlatformIO**. Las dependencias se instalan automaticamente al
compilar por primera vez, ya que estan declaradas en `platformio.ini`:

```ini
lib_deps =
    adafruit/Adafruit SSD1306 @ ^2.5.7
    adafruit/Adafruit GFX Library @ ^1.11.9
    adafruit/Adafruit BusIO @ ^1.14.1
    phoenix1747/MQ135 @ ^1.1.0
```

### Instalacion automatica (recomendada)

```bash
pio pkg install
pio run
pio run --target upload
```

### Instalacion manual con PlatformIO Core

```bash
pio pkg install -l "adafruit/Adafruit SSD1306@^2.5.7"
pio pkg install -l "adafruit/Adafruit GFX Library@^1.11.9"
pio pkg install -l "adafruit/Adafruit BusIO@^1.14.1"
pio pkg install -l "phoenix1747/MQ135@^1.1.0"
```

### Alternativa con Arduino IDE

Si se prefiere compilar con el Arduino IDE, hay que instalar las mismas
librerias desde el Gestor de Librerias:

| Libreria | Autor |
|---|---|
| Adafruit SSD1306 | Adafruit |
| Adafruit GFX Library | Adafruit |
| Adafruit BusIO | Adafruit |
| MQ135 | phoenix1747 |

En el gestor de librerias del Arduino IDE, dentro del gestor de tarjetas hay
que instalar ademas el paquete **esp32 by Espressif Systems** para tener el
soporte de la placa ESP32.

## Estructura del proyecto

```
.
|-- platformio.ini        Configuracion de placa, framework y dependencias
|-- src/
|   `-- main.cpp          Codigo principal del prototipo
`-- README.md
```

## Compilacion y subida

```bash
pio run                              # compila
pio run --target upload              # sube a la placa
pio device monitor --baud 115200     # monitor serie
```

## Calibracion

1. Colocar el sensor en aire limpio (idealmente exterior, resguardado del
   viento y lejos de fuentes de CO2).
2. Pulsar y mantener el boton BOOT durante 2 segundos, o enviar el comando `c`
   por el monitor serie.
3. El proceso realiza un precalentado de 60 s y despues una ventana de
   calibracion de 120 s tomando una muestra cada 3 s.
4. El RZero resultante (mediana de las muestras) se guarda en la memoria NVS y
   se conserva entre reinicios.

El sensor MQ-135 no es un sensor NDIR, por lo que la medida es una estimacion
de CO2 equivalente. El RZero depende de la temperatura y la humedad del
ambiente, por lo que conviene recalibrar si cambian las condiciones de uso.

## Licencia

Proyecto academico de demostracion.
