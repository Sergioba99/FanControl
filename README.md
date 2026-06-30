# Fan Control ESP32

Sistema de control para un ventilador de techo y su luz usando un ESP32, una interfaz web embebida y envio de comandos por radiofrecuencia. El proyecto tambien integra lectura de temperatura/humedad, temporizador personalizado, modo automatico configurable, OTA y persistencia de ajustes en la memoria NVS del ESP32.

La web se sirve desde `LittleFS` y el ESP32 actua como cliente WiFi dentro de una red existente. Desde el navegador se pueden enviar ordenes RF, consultar datos en tiempo real, configurar el modo automatico y ajustar la repeticion de los comandos RF.

## Caracteristicas

- ESP32 con framework Arduino y PlatformIO.
- Servidor web asyncrono en el propio microcontrolador.
- Servidor AsyncTCP fijado al nucleo 0 y logica Arduino en el nucleo 1.
- Cola FreeRTOS para desacoplar las peticiones web de RF, temporizadores y ajustes.
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
- Cambio CET/CEST automatico para la zona horaria de Espana.
- Resolucion local con mDNS usando `http://fancontrol.local`.
- Portal cautivo para configurar WiFi sin recompilar el firmware.
- Hasta cinco perfiles WiFi persistentes, editables y eliminables.
- Eleccion entre DHCP e IP estatica.
- Reconexion WiFi no bloqueante y portal de recuperacion si la red no esta disponible.

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
│   ├── fan_rf.h
│   ├── logic_commands.h
│   ├── main.cpp
│   └── wifi_profiles.h
├── data/
│   ├── app.js
│   ├── config.html
│   ├── index.html
│   ├── style.css
│   ├── wifi.html
│   └── wifi.js
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
- `DNSServer`
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
- `/wifi` muestra el portal de configuracion de red.
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

- Estado de red, IP actual, SSID, RSSI y modo de asignacion.
- Acceso al portal WiFi, contador de perfiles y borrado de todas las redes guardadas.
- Umbrales del control automatico.
- Repeticiones RF por modo.
- Acceso a la actualizacion OTA.

## Configuracion WiFi

En el primer arranque sin redes guardadas, el ESP32 crea el punto de acceso `FanControl-Setup` con la contrasena `fancontrol`. El portal cautivo se abre automaticamente; tambien se puede acceder mediante `http://192.168.4.1/wifi`.

Antes de abrir el punto de acceso, el ESP32 escanea las redes cercanas y las muestra en el portal. El boton `Actualizar redes` permite lanzar posteriormente un escaneo asincrono manual; durante unos segundos el portal puede responder con mas latencia debido al cambio de canal. El formulario tambien permite escribir manualmente un SSID oculto y elegir entre:

- `DHCP`: la red asigna automaticamente la direccion.
- `IP estatica`: se configuran IP, puerta de enlace, mascara y DNS.

Despues de guardar, el punto de acceso permanece activo y la pagina muestra la IP obtenida. El guardado, la lista de redes y la consulta manual del estado funcionan aunque el navegador cautivo no ejecute JavaScript. Al pulsar `Finalizar`, se cierra `FanControl-Setup`. El dispositivo queda accesible mediante la IP mostrada o `http://fancontrol.local`.

Se pueden guardar hasta cinco perfiles. Cada uno conserva sus propias credenciales y configuracion DHCP/IP estatica. El portal permite crear perfiles, editarlos sin volver a mostrar la contrasena y eliminarlos individualmente. Al editar, una contrasena vacia conserva la existente.

Durante el arranque se prueba primero la ultima red que funciono y despues el resto de perfiles guardados. Cada intento dispone de ocho segundos. Si ninguno conecta, se abre el portal sin bloquear el temporizador, el control automatico ni el resto del firmware. Desde `/config` se puede consultar la IP, ver el numero de perfiles o borrar todas las redes guardadas.

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
- `GET /wifi` -> portal de configuracion WiFi.
- `GET /wifi/status` -> conexion, IP, SSID, RSSI y modo IP en JSON.
- `GET /wifi/scan` -> redes WiFi disponibles en JSON.
- `POST /wifi/scan/start` -> inicia un escaneo WiFi manual asincrono.
- `POST /wifi/save` -> guarda las credenciales y la configuracion IP.
- `POST /wifi/profile/delete` -> elimina un perfil WiFi individual.
- `POST /wifi/finish` -> cierra el punto de acceso tras conectar.
- `POST /wifi/reset` -> borra todos los perfiles y abre el portal.
- `GET /localtime` -> hora local.
- `GET /localdate` -> fecha local.
- `GET /temperature` -> temperatura actual.
- `GET /humidity` -> humedad actual.
- `GET /api/status` -> telemetria, temporizador y automatico en una unica respuesta.
- `GET /system/status` -> nucleos usados, ocupacion de la cola y heap libre.
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
- Hasta cinco perfiles con credenciales WiFi y modo DHCP/IP estatica.
- IP, puerta de enlace, mascara y DNS independientes por perfil.
- Indice de la ultima red conectada correctamente.

El estado guardado es logico: el ESP32 recuerda el ultimo comando que envio, pero el ventilador no confirma recepcion RF. Si se usa el mando original o un comando RF falla, el estado puede quedar desincronizado.

## Funcionamiento interno

- El nucleo 0 atiende WiFi, HTTP y OTA; el nucleo 1 procesa RF, temporizadores, sensores y control automatico.
- Los endpoints validan y encolan las ordenes. Si la cola esta llena responden con HTTP `503` sin bloquear el servidor.
- La cola procesa una orden por iteracion para evitar que varias transmisiones RF consecutivas monopolicen la logica.
- Los comandos consecutivos de velocidad se compactan conservando el ultimo; los comandos de luz no se compactan porque son alternancias.
- Los estados compartidos entre nucleos se consultan mediante snapshots protegidos por un mutex FreeRTOS.
- La pagina principal consulta un estado unificado cada 10 segundos y mantiene localmente la cuenta atras del temporizador.
- Los cambios de automatico, RF y estado del ventilador se agrupan durante un segundo antes de escribir NVS.
- La temperatura y la humedad se actualizan cada 30 segundos.
- La hora y la fecha se refrescan periodicamente desde `ESP32Time`.
- El modo automatico se evalua periodicamente y evita cambios demasiado seguidos.
- Los comandos manuales cancelan el temporizador y desactivan el automatico.
- El temporizador desactiva el automatico mientras esta activo.
- Si la WiFi cae, el firmware prueba los perfiles guardados sin detener el resto del sistema y habilita el portal cuando ninguno conecta.
- La web se actualiza por peticiones AJAX desde `app.js`.

## Limitaciones conocidas

- El control RF es unidireccional: no hay confirmacion real del ventilador.
- El estado de luz encendida/apagada no se puede verificar sin hardware adicional.
- Para verificar estado real haria falta medir consumo, luz ambiente o movimiento.

## Posibles mejoras

- Añadir una pagina de diagnostico con RSSI, uptime, heap libre y ultimo comando RF.
- Migrar operaciones de configuracion de `GET` a `POST` si el proyecto crece.
