# Fan Control ESP32

Sistema de control para ventilador y luz basado en ESP32, con servidor web embebido, telemetria de temperatura/humedad y actualizacion OTA.

La web se sirve desde `LittleFS` y el ESP32 actua como cliente WiFi en una red existente. Desde el navegador se pueden enviar ordenes por RF para controlar el ventilador y la luz, ademas de consultar datos en tiempo real.

## Caracteristicas

- ESP32 con framework Arduino y PlatformIO
- Servidor web asyncrono en el propio microcontrolador
- Interfaz web servida desde `LittleFS`
- Actualizacion OTA con `AsyncElegantOTA`
- Lectura de sensor DHT11
- Envio de comandos por radiofrecuencia con `RCSwitch`
- Sincronizacion de hora por NTP
- Resolucion local con mDNS usando `http://fancontrol.local`
- Reconexion automatica a WiFi si se pierde la red

## Hardware esperado

- Placa ESP32 DevKit
- Sensor DHT11 conectado a `GPIO19`
- Emisor RF conectado a `GPIO18`
- Un receptor controlado por RF para ventilador / luz

## Asignacion de pines

- `GPIO19` -> DHT11
- `GPIO18` -> TX RF

## Estructura del proyecto

```text
.
├── src/
│   └── main.cpp
├── data/
│   ├── index.html
│   └── style.css
├── platformio.ini
├── lib/
├── include/
└── test/
```

## Requisitos

- VS Code con PlatformIO
- Librerias instaladas por `platformio.ini`
- Red WiFi local accesible desde el ESP32

## Librerias usadas

- `AsyncTCP-esphome`
- `ESPAsyncWebServer-esphome`
- `AsyncElegantOTA`
- `DHT sensor library for ESPx`
- `ESP32Time`
- `RCSwitch`

## Compilacion y subida

1. Abre el proyecto en PlatformIO.
2. Conecta el ESP32 por USB.
3. Compila y sube el firmware.
4. Sube el contenido de `data/` a `LittleFS` si has cambiado la web.

En PlatformIO suele hacerse con:

- `Upload`
- `Upload Filesystem Image`

## Endpoints HTTP

- `GET /` -> pagina principal
- `GET /style.css` -> hoja de estilos
- `GET /localtime` -> hora local
- `GET /localdate` -> fecha local
- `GET /temperature` -> temperatura actual
- `GET /humidity` -> humedad actual
- `GET /botones?button=luz|2h|4h|8h|high|med|low|off` -> envio de orden RF
- `GET /update` -> actualizacion OTA

## Comandos RF

El firmware envia estos codigos por RF:

- `luz` -> `111101010001`
- `2h` -> `111110100010`
- `4h` -> `111110100100`
- `8h` -> `111110101000`
- `high` -> `111101101001`
- `med` -> `111100100011`
- `low` -> `111100010111`
- `off` -> `111101000101`

## Funcionamiento interno

- La temperatura y la humedad se actualizan cada 30 segundos.
- La hora y la fecha se refrescan periodicamente desde `ESP32Time`.
- Si la WiFi cae, el firmware intenta reconectarse.
- La web se actualiza por peticiones AJAX simples desde el navegador.

## Nota importante

En `data/index.html` hay un formulario que llama a `GET /temporizador`, pero ese endpoint no esta implementado en `src/main.cpp`.

Si quieres usar el temporizador personalizado, hay que añadir su handler en el firmware.

## Recomendaciones de mejora

- Mover credenciales WiFi a un archivo de configuracion separado
- Añadir el endpoint `/temporizador`
- Sustituir las peticiones AJAX repetitivas por `fetch()` o WebSocket si quieres una interfaz mas fluida
- Cambiar `HTTP GET` con datos sensibles por `POST` si el proyecto crece
- Añadir control de estado para evitar mandar comandos RF repetidos sin necesidad

## Autor y mantenimiento

Este proyecto esta pensado como base practica para control domotico simple desde un ESP32. La estructura actual es buena para evolucionar a un sistema mas completo sin complicarlo demasiado.
