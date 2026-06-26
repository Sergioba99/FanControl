# Fan Control ESP32

Sistema de control para un ventilador de techo y su luz usando un ESP32, una interfaz web embebida y envio de comandos por radiofrecuencia. El proyecto tambien integra lectura de temperatura/humedad, temporizador personalizado, modo automatico configurable, OTA y persistencia de ajustes en la memoria NVS del ESP32.

La web se sirve desde `LittleFS` y el ESP32 actua como cliente WiFi dentro de una red existente. Desde el navegador se pueden enviar ordenes RF, consultar datos en tiempo real, configurar el modo automatico y ajustar la repeticion de los comandos RF.

## Caracteristicas

- ESP32 con framework Arduino y PlatformIO.
- Servidor web asyncrono en el propio microcontrolador.
- Interfaz web servida desde `LittleFS`.
- JavaScript separado en `data/app.js`.
- Pagina principal de control y pagina independiente de configuracion.
- Actualizacion OTA con `AsyncElegantOTA`.
- Lectura de sensor DHT11.
- Envio de comandos por radiofrecuencia con `RCSwitch`.
- Temporizador personalizado desde la web.
- Modo automatico por temperatura y humedad.
- Umbrales del modo automatico configurables y persistentes.
- Repeticiones RF configurables por modo y persistentes.
- Guardado del ultimo modo conocido del ventilador.
- Sincronizacion de hora por NTP.
- Resolucion local con mDNS usando `http://fancontrol.local`.
- Reconexion automatica a WiFi si se pierde la red.

## Hardware esperado

- Placa ESP32 DevKit.
- Sensor DHT11 conectado a `GPIO19`.
- Emisor RF conectado a `GPIO18`.
- Ventilador/luz controlado por RF compatible con los codigos configurados.

## Asignacion de pines

- `GPIO19` -> DHT11.
- `GPIO18` -> TX RF.

## Estructura del proyecto

```text
.
├── src/
│   └── main.cpp
├── data/
│   ├── app.js
│   ├── config.html
│   ├── index.html
│   └── style.css
├── platformio.ini
├── lib/
├── include/
└── test/
```

## Requisitos

- VS Code con PlatformIO.
- Librerias instaladas por `platformio.ini`.
- Red WiFi local accesible desde el ESP32.

## Librerias usadas

- `AsyncTCP-esphome`
- `ESPAsyncWebServer-esphome`
- `AsyncElegantOTA`
- `DHT sensor library for ESPx`
- `ESP32Time`
- `RCSwitch`
- `LittleFS`
- `Preferences`

## Compilacion y subida

Compilar firmware:

```powershell
platformio run --environment esp32dev
```

Generar imagen de `LittleFS`:

```powershell
platformio run --target buildfs --environment esp32dev
```

Si cambias `src/main.cpp`, sube `firmware.bin`. Si cambias archivos dentro de `data/`, sube tambien `littlefs.bin`.

Con ElegantOTA:

- Firmware: `.pio/build/esp32dev/firmware.bin`
- Web/LittleFS: `.pio/build/esp32dev/littlefs.bin`

## Interfaz web

- `/` muestra la pagina principal de control.
- `/config` muestra la pagina de configuracion.
- `/update` abre la interfaz de actualizacion OTA.

La pagina principal incluye:

- Fecha y hora local.
- Temperatura y humedad.
- Estado del temporizador.
- Estado del modo automatico.
- Botones RF del ventilador y la luz.
- Temporizador personalizado.
- Boton `AUTO ON/OFF`.

La pagina de configuracion incluye:

- Umbrales del control automatico.
- Repeticiones RF por modo.
- Acceso a la actualizacion OTA.

## Temporizador personalizado

El temporizador permite seleccionar duracion en horas/minutos y modo del ventilador (`LOW`, `MED` o `HIGH`). Al finalizar, el firmware envia `off` y limpia el estado del temporizador.

Cuando se programa un temporizador:

- Se envia inmediatamente el modo seleccionado.
- Se desactiva el modo automatico.
- La pagina principal muestra el tiempo restante.

## Modo automatico

El modo automatico calcula el modo objetivo usando temperatura y humedad:

- `LOW` desde el umbral bajo.
- `MED` desde el umbral medio.
- `HIGH` desde el umbral alto.
- Si la humedad supera el umbral configurado, se sube un nivel el modo objetivo.

Los ajustes se guardan en NVS con `Preferences`, por lo que sobreviven a reinicios o cortes de corriente.

## Radiofrecuencia

El firmware envia estos codigos RF:

- `luz` -> `111101010001`
- `2h` -> `111110100010`
- `4h` -> `111110100100`
- `8h` -> `111110101000`
- `high` -> `111101101001`
- `med` -> `111100100011`
- `low` -> `111100010111`
- `off` -> `111101000101`

Las repeticiones de transmision son configurables desde la pagina de configuracion:

- `General`
- `LOW`
- `MED`
- `HIGH`
- `FAN OFF`

Por defecto, `LOW` usa mas repeticiones que el resto para mejorar la fiabilidad si ese comando no engancha a la primera.

## Endpoints HTTP

- `GET /` -> pagina principal.
- `GET /config` -> pagina de configuracion.
- `GET /config.html` -> pagina de configuracion.
- `GET /style.css` -> hoja de estilos.
- `GET /app.js` -> JavaScript de la web.
- `GET /localtime` -> hora local.
- `GET /localdate` -> fecha local.
- `GET /temperature` -> temperatura actual.
- `GET /humidity` -> humedad actual.
- `GET /botones?button=luz|2h|4h|8h|high|med|low|off` -> envio de orden RF.
- `GET /temporizador?button=low|med|high&hours=0&minutes=30` -> programa temporizador.
- `GET /temporizador?cancel=1` -> cancela temporizador.
- `GET /timer/status` -> estado del temporizador en JSON.
- `GET /temporizador/status` -> alias del estado del temporizador.
- `GET /auto/status` -> estado y ajustes del modo automatico en JSON.
- `GET /auto?enabled=1|0` -> activa/desactiva automatico.
- `GET /auto?enabled=1&low=25.0&med=27.0&high=29.0&humidity=65.0` -> guarda automatico.
- `GET /rf/status` -> ajustes RF en JSON.
- `GET /rf?default=20&low=30&med=20&high=20&off=20` -> guarda repeticiones RF.
- `GET /update` -> actualizacion OTA.

## Persistencia

Se usa `Preferences` con el namespace `fancontrol` para guardar:

- Modo automatico activado/desactivado.
- Umbrales de temperatura y humedad.
- Repeticiones RF.
- Ultimo modo conocido del ventilador.

El estado guardado es logico: el ESP32 recuerda el ultimo comando que envio, pero el ventilador no confirma recepcion RF. Si se usa el mando original o un comando RF falla, el estado puede quedar desincronizado.

## Funcionamiento interno

- La temperatura y la humedad se actualizan cada 30 segundos.
- La hora y la fecha se refrescan periodicamente desde `ESP32Time`.
- El modo automatico se evalua periodicamente y evita cambios demasiado seguidos.
- Los comandos manuales cancelan el temporizador y desactivan el automatico.
- El temporizador desactiva el automatico mientras esta activo.
- Si la WiFi cae, el firmware intenta reconectarse.
- La web se actualiza por peticiones AJAX desde `app.js`.

## Limitaciones conocidas

- El control RF es unidireccional: no hay confirmacion real del ventilador.
- El estado de luz encendida/apagada no se puede verificar sin hardware adicional.
- Para verificar estado real haria falta medir consumo, luz ambiente o movimiento.
- Las credenciales WiFi estan dentro de `src/main.cpp`.

## Posibles mejoras

- Mover credenciales WiFi a un archivo no versionado o a configuracion desde portal web.
- Añadir una pagina de diagnostico con RSSI, uptime, heap libre y ultimo comando RF.
- Añadir sensor de luz para verificar la lampara.
- Añadir medicion de consumo para verificar si el ventilador esta realmente encendido.
- Migrar operaciones de configuracion de `GET` a `POST` si el proyecto crece.
