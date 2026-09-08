# SmartRoomController - Taller de Sistemas Embebidos

La documentación completa con la solución al taller, los diagramas de las máquinas de estado, los casos de prueba y las respuestas a las preguntas de análisis se encuentra en el archivo:
[TALLER_SISTEMAS_EMBEBIDOS.md](TALLER_SISTEMAS_EMBEBIDOS.md)

---

## Cambios realizados en el código (`src/main.cpp`)

Para lograr cumplir con todos los requisitos funcionales (RF) solicitados en el taller, se modificó el código original aplicando las siguientes mejoras técnicas:

1. **Separación de responsabilidades:**
   Se dividió el código monolítico del `loop()` en cuatro funciones principales para mantener el orden y limpieza: `readSensors()`, `updateState()`, `updateDoor()` y `updateOutputs()`.

2. **Implementación de Máquinas de Estados:**
   Se crearon dos máquinas de estado usando enumeradores (`enum`) y bloques `switch/case`:
   - `SystemState` (`NORMAL`, `VENTILATION`, `ALARM`, `SENSOR_ERROR`) para el clima y la seguridad.
   - `DoorState` (`CLOSED`, `OPEN`, `CLOSING`) para manejar la apertura y cierre del servo.

3. **Ajuste a umbrales estrictos de temperatura (RF4 y RF5):**
   Se corrigieron las constantes del programa a los valores exactos requeridos: > 35 °C para ventilación, retorno a < 30 °C y >= 40 °C para la condición crítica de alarma.

4. **Temporizador no bloqueante con `millis()` (RF3 y RF6):**
   Para el cierre automático de la puerta tras 5 segundos, se descartó el uso de `delay()`. En su lugar, se guardó el registro de tiempo en la variable `closeStartTime` y se evalúa constantemente de forma asíncrona usando `millis()`. Esto es vital, ya que permite cancelar el cierre regresando al estado `OPEN` si alguien se acerca antes de que termine el tiempo.

5. **Prioridad absoluta de Alarmas (RF5):**
   Se programó una condición de máxima prioridad al inicio de la función de la puerta. Si el sistema general se encuentra en estado `ALARM`, la puerta fuerza su estado a `CLOSED` y se bloquea, ignorando por completo cualquier evento proveniente del sensor de movimiento (PIR) o distancia.

6. **Implementación de Fail-Safe (Reto Opcional):**
   Se agregó una validación para detectar fallos de hardware en el DHT22 usando la función `isnan()`. Si el sensor deja de funcionar, el sistema pasa al estado seguro `SENSOR_ERROR` y bloquea la puerta para prevenir incidentes de seguridad.

