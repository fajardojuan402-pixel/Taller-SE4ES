#include <Arduino.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ============================================================
// ASIGNACIÓN DE PINES
// ============================================================
constexpr uint8_t PIN_DHT = 4;
constexpr uint8_t PIN_PIR = 27;
constexpr uint8_t PIN_TRIG = 5;
constexpr uint8_t PIN_ECHO = 18;
constexpr uint8_t PIN_LDR = 34;

constexpr uint8_t PIN_SERVO = 13;
constexpr uint8_t PIN_BUZZER = 25;
constexpr uint8_t PIN_LED_RED = 26;
constexpr uint8_t PIN_LED_YELLOW = 14;
constexpr uint8_t PIN_LED_GREEN = 33;

// ============================================================
// UMBRALES DE CONTROL OBLIGATORIOS (RF)
// ============================================================
constexpr float TEMP_VENT_ON = 35.0;  // RF4: T > 35 °C
constexpr float TEMP_VENT_OFF = 30.0; // RF4: T < 30 °C
constexpr float TEMP_ALARM = 40.0;    // RF5: T >= 40 °C

constexpr float DOOR_OPEN_DISTANCE = 50.0;   // RF2: distancia <= 50 cm
constexpr float DOOR_CLOSE_DISTANCE = 70.0;  // RF3: distancia >= 70 cm
constexpr unsigned long DOOR_CLOSE_DELAY = 5000; // RF3: 5 segundos

// ============================================================
// CADENCIAS (ms)
// ============================================================
constexpr unsigned long FAST_RATE = 50;       // Sensores rápidos: 20 Hz
constexpr unsigned long DHT_RATE = 2000;      // DHT22
constexpr unsigned long DIAG_RATE = 2000;

// ============================================================
// OBJETOS Y VARIABLES
// ============================================================
DHT dht(PIN_DHT, DHT22);
Servo doorServo;

enum SystemState { NORMAL, VENTILATION, ALARM, SENSOR_ERROR };
SystemState systemState = NORMAL;

enum DoorState { CLOSED, OPEN, CLOSING };
DoorState doorState = CLOSED;

float temperature = 25.0;
float humidity = 50.0;
float distance = 400.0;
int lightLevel = 0;
bool motionDetected = false;
bool dhtError = false;

unsigned long lastFastTime = 0;
unsigned long lastDHTTime = 0;
unsigned long lastDiagTime = 0;
unsigned long closeStartTime = 0; // Temporizador para el cierre de la puerta

unsigned long lastToneToggle = 0;
bool buzzerPinState = false;

// Prototipos de funciones para separación de responsabilidades
void readSensors();
void updateState();
void updateDoor();
void updateOutputs();
void updateBuzzerTone();
void printDiagnostics();

// ============================================================
// SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);

    pinMode(PIN_PIR, INPUT);
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_LDR, INPUT);

    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_YELLOW, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    // Asignación de timers exclusiva para ESP32Servo
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    doorServo.setPeriodHertz(50);
    doorServo.attach(PIN_SERVO, 500, 2400);
    doorServo.write(0);

    dht.begin();

    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_BUZZER, LOW);

    Serial.println("\n[INIT] Sistema iniciado correctamente.");
}

// ============================================================
// LOOP PRINCIPAL
// ============================================================
void loop()
{
    const unsigned long now = millis();

    // 1. Tono continuo del buzzer mientras haya alarma (sin bloquear la CPU)
    updateBuzzerTone();

    // 2. Sensores rápidos (LDR, PIR, Distancia) cada 50 ms
    if (now - lastFastTime >= FAST_RATE)
    {
        lastFastTime = now;
        readSensors();
        updateState();
        updateDoor();
        updateOutputs();
    }

    // 3. Sensor DHT22 cada 2000 ms
    if (now - lastDHTTime >= DHT_RATE)
    {
        lastDHTTime = now;
        float t = dht.readTemperature();
        float h = dht.readHumidity();

        if (isnan(t) || isnan(h)) {
            dhtError = true;
        } else {
            dhtError = false;
            temperature = t;
            humidity = h;
        }
    }

    // 4. Reporte serial
    if (now - lastDiagTime >= DIAG_RATE)
    {
        lastDiagTime = now;
        printDiagnostics();
    }
}

// ============================================================
// BUZZER NO BLOQUEANTE (Tono cuadrado limpio a 1 kHz)
// ============================================================
void updateBuzzerTone()
{
    if (systemState == ALARM || systemState == SENSOR_ERROR)
    {
        unsigned long currentMicros = micros();
        // 500 microsegundos = 1000 Hz
        if (currentMicros - lastToneToggle >= 500)
        {
            lastToneToggle = currentMicros;
            buzzerPinState = !buzzerPinState;
            digitalWrite(PIN_BUZZER, buzzerPinState ? HIGH : LOW);
        }
    }
    else
    {
        if (buzzerPinState)
        {
            buzzerPinState = false;
            digitalWrite(PIN_BUZZER, LOW);
        }
    }
}

// ============================================================
// LECTURA DE SENSORES (Separa la recolección de datos)
// ============================================================
void readSensors()
{
    motionDetected = (digitalRead(PIN_PIR) == HIGH);
    lightLevel = analogRead(PIN_LDR);

    // Lectura ultrasónica (HC-SR04)
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    // Timeout de 6000 us (~1 metro de alcance máx)
    unsigned long duration = pulseIn(PIN_ECHO, HIGH, 6000);
    distance = (duration == 0) ? 150.0 : (duration / 58.0);
}

// ============================================================
// LÓGICA DEL SISTEMA (Máquina de estados principal)
// ============================================================
void updateState()
{
    if (dhtError) {
        systemState = SENSOR_ERROR; // Reto opcional: error en lectura del sensor
        return;
    }

    switch (systemState) {
        case NORMAL:
            if (temperature >= TEMP_ALARM) { // Prioridad: ALARM
                systemState = ALARM;
            } else if (temperature > TEMP_VENT_ON) {
                systemState = VENTILATION;
            }
            break;

        case VENTILATION:
            if (temperature >= TEMP_ALARM) { // Prioridad: ALARM
                systemState = ALARM;
            } else if (temperature < TEMP_VENT_OFF) {
                systemState = NORMAL;
            }
            break;

        case ALARM:
            // RF5: La salida de ALARM será cuando T < 30 °C
            if (temperature < TEMP_VENT_OFF) {
                systemState = NORMAL;
            }
            break;

        case SENSOR_ERROR:
            if (!dhtError) {
                systemState = NORMAL;
            }
            break;
    }
}

// ============================================================
// LÓGICA DE LA PUERTA (Máquina de estados secundaria)
// ============================================================
void updateDoor()
{
    // RF5: Mientras exista ALARM, la puerta NO debe abrirse automáticamente.
    // También bloqueamos si hay error de sensor por seguridad.
    if (systemState == ALARM || systemState == SENSOR_ERROR) {
        doorState = CLOSED;
        return;
    }

    switch (doorState) {
        case CLOSED:
            // RF2: PIR detecta movimiento Y distancia <= 50 cm -> abrir la puerta
            if (motionDetected && distance <= DOOR_OPEN_DISTANCE) {
                doorState = OPEN;
            }
            break;

        case OPEN:
            // RF3: distancia >= 70 cm -> comienza temporizador
            if (distance >= DOOR_CLOSE_DISTANCE) {
                doorState = CLOSING;
                closeStartTime = millis(); // Guarda el momento de inicio
            }
            break;

        case CLOSING:
            // RF6: Cancelación del cierre. Si vuelve a detectarse una persona (movimiento + distancia <= 50)
            if (motionDetected && distance <= DOOR_OPEN_DISTANCE) {
                doorState = OPEN; // Se cancela el temporizador regresando a OPEN
            } 
            // RF3: Después de 5 segundos, la puerta debe cerrarse. NO es bloqueante.
            else if (millis() - closeStartTime >= DOOR_CLOSE_DELAY) {
                doorState = CLOSED;
            }
            break;
    }
}

// ============================================================
// SALIDAS (Actualización de actuadores en base a los estados)
// ============================================================
void updateOutputs()
{
    bool redOn = false;
    bool yellowOn = false;
    bool greenOn = false;

    // Configurar LEDs según RF1, RF4, RF5
    if (systemState == NORMAL) {
        greenOn = true; // RF1: LED verde ON
    } else if (systemState == VENTILATION) {
        yellowOn = true; // RF4: LED amarillo = ON
    } else if (systemState == ALARM) {
        redOn = true;    // RF5: LED rojo = ON, amarillo = ON
        yellowOn = true;
    } else if (systemState == SENSOR_ERROR) {
        redOn = true;    // Comportamiento de error: Todos encendidos
        yellowOn = true;
        greenOn = true;
    }

    digitalWrite(PIN_LED_RED, redOn ? HIGH : LOW);
    digitalWrite(PIN_LED_YELLOW, yellowOn ? HIGH : LOW);
    digitalWrite(PIN_LED_GREEN, greenOn ? HIGH : LOW);
    
    // Servomotor (0° cerrada, 90° abierta)
    static DoorState lastDoorState = CLOSED;
    if (doorState != lastDoorState) {
        doorServo.write((doorState == CLOSED) ? 0 : 90);
        lastDoorState = doorState;
    }
}

// ============================================================
// DIAGNÓSTICO
// ============================================================
void printDiagnostics()
{
    const char* sysStr = (systemState == NORMAL) ? "NORMAL" :
                         (systemState == VENTILATION) ? "VENTILATION" :
                         (systemState == ALARM) ? "ALARM" : "SENSOR_ERROR";
                         
    const char* doorStr = (doorState == CLOSED) ? "CLOSED" :
                          (doorState == OPEN) ? "OPEN" : "CLOSING";

    Serial.printf("[STATUS] Sys: %s | Door: %s | Temp: %.1f C | Dist: %.1f cm | PIR: %s\n",
                  sysStr, doorStr, temperature, distance, motionDetected ? "YES" : "NO");
}
