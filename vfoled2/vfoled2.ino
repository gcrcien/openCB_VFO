#include <Wire.h>             // Biblioteca para comunicación I2C
#include <si5351.h>           // Biblioteca para controlar el sintetizador de frecuencia SI5351
#include <SPI.h>              // Biblioteca para comunicación SPI (no utilizada en este código)
#include <Adafruit_SSD1306.h> // Biblioteca para controlar la pantalla OLED

// Definición de las dimensiones de la pantalla OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Instancia para manejar la pantalla OLED
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire);

// Definición de pines para botones y entradas
#define BUTTON_PIN 2
#define TXPIN A3

// Variables para almacenar la frecuencia en Hz, kHz, y MHz
unsigned int hz;
unsigned int khz;
unsigned int mhz;

// Otras variables para manejo de frecuencia y modos
int interval;
int Moffset = 0;         // Offset de modo (AM, LSB, USB)
int txoffset = 0;
int TXOffset = 0;
int mode;                // Variable que almacena el modo actual (AM, LSB, USB)
int sensorValue;
int change;              // Variable de bandera para detectar cambios
int debounceT = 5;       // Tiempo de debounce para botones
int correction = 0;      // Corrección de frecuencia (si es necesario)
int compensacion = 28610;

// Variables de tiempo para actualización de audio
unsigned long lastAudioUpdate = 0;
unsigned long audioUpdateInterval = 10;

// Frecuencias y pasos de ajuste
unsigned long currentFrequency = 27695000; // Frecuencia inicial
unsigned long minFrequency = 25000000;     // Frecuencia mínima (25 MHz)
unsigned long maxFrequency = 28500000;     // Frecuencia máxima (30 MHz)
unsigned long stepSize = 1000;             // Tamaño del paso de ajuste (1 kHz)
unsigned long iFrequency = 10671000;       // Frecuencia de IF (10671 kHz)

// Variables de estado para pines de control de modo
bool SA;
bool SB;
bool TX;

// Variables de frecuencia en formato de cadena
String shz;
String skhz;
String smhz;
String fstep = "1khz";   // Cadena que representa el tamaño del paso
String frequency_string; // Cadena completa de la frecuencia para mostrar
String modeS;            // Cadena que representa el modo actual (AM, LSB, USB)
String banda;            // Banda actual basada en la frecuencia
//String TXState = "RX";
// Instancia para manejar el sintetizador de frecuencia SI5351
Si5351 si5351;

// Variables para almacenar el estado anterior de los pines SA y SB
bool previousSA;
bool previousSB;

void setup() {
  // Inicialización de la pantalla OLED
  display.begin(SSD1306_SWITCHCAPVCC,  0x3C);

  // Limpia la pantalla y muestra un píxel para confirmar que está funcionando
  display.clearDisplay();
  display.drawPixel(10, 10, SSD1306_WHITE);
  display.display();

  // Configuración de los pines como entradas con resistencias pull-up
  pinMode(4, INPUT_PULLUP);
  pinMode(3, INPUT_PULLUP);
  pinMode(6, INPUT_PULLUP);
  pinMode(7, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Configuración de la interrupción externa para el botón de ajuste de frecuencia
  attachInterrupt(digitalPinToInterrupt(3), knob_ISR1, FALLING);
  // Si se desea usar el botón adicional, se puede habilitar esta línea:
  // attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), knob_ISR2, FALLING);

  // Inicialización del SI5351 con un cristal de 8pF
  si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);

  // Configuración inicial de la frecuencia del CLK0 con la frecuencia inicial
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_6MA);
  si5351.set_freq((currentFrequency - iFrequency + Moffset - correction) * SI5351_FREQ_MULT, SI5351_CLK0);
  SA = digitalRead(6);
  SB = digitalRead(7);
  previousSA = SA;
  previousSB = SB;
  if (SA == LOW && SB == LOW) {
    modeS = "AM";   // Cambia a modo AM
    Moffset = 3000; // Establece el offset para AM
    mode = 1;
  } else if (SA == HIGH && SB == LOW) {
    modeS = "LSB";  // Cambia a modo LSB
    Moffset = 0;    // Establece el offset para LSB
    mode = 2;
  } else if (SA == LOW && SB == HIGH) {
    modeS = "USB";  // Cambia a modo USB
    Moffset = 5000; // Establece el offset para USB
    mode = 3;
  }

  // Actualización inicial de la pantalla con la frecuencia y modo actuales
  actualizar();
  delay(500); // Retardo para permitir la inicialización completa
}

// Interrupción para manejar el cambio de frecuencia mediante un encoder
void knob_ISR1() {
  static unsigned long last_interrupt_time = 0; // Tiempo del último pulso para debounce
  unsigned long interrupt_time = millis();      // Tiempo actual

  // Verificación de debounce
  if (interrupt_time - last_interrupt_time > debounceT) {
    // Leer el estado del pin 4 para determinar la dirección de giro
    bool dir = digitalRead(4);
    if (dir == true) {
      currentFrequency += stepSize; // Aumenta la frecuencia
    } else {
      currentFrequency -= stepSize; // Disminuye la frecuencia
    }

    // Asegura que la frecuencia esté dentro de los límites definidos
    if  (currentFrequency < 25000000) {
      currentFrequency = 25000000;
    }
    if  (currentFrequency > 30500000) {
      currentFrequency = 30500000;
    }

    change = true; // Marca que ha habido un cambio para actualizar la pantalla
    last_interrupt_time = interrupt_time; // Actualiza el tiempo del último pulso
  }
}

// Función para manejar la lógica de actualización de la frecuencia y pantalla
void actualizar() {
  change = false; // Resetea la bandera de cambio
  fstring();      // Convierte la frecuencia actual en una cadena formateada
  banda = getband(currentFrequency); // Obtiene la banda basada en la frecuencia actual

  // Ajusta la frecuencia del SI5351 en base a la frecuencia actual y el offset
  si5351.set_freq((currentFrequency - iFrequency + Moffset - correction + txoffset) * SI5351_FREQ_MULT, SI5351_CLK0);

  // Actualiza la pantalla con la frecuencia, el paso, y el modo actuales
  display.clearDisplay();
  display.setTextSize(2); // Texto en tamaño 2X
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 10);
  display.println(frequency_string);
  display.setTextSize(1);
  display.setCursor(20, 30);
  display.println(fstep);
  display.setTextSize(2);
  display.setCursor(20, 40);
  display.println(modeS);
  //display.setTextSize(2);
  //display.setCursor(40, 40);
  //display.println(TXState);
  display.display();
  delay(50); // Pequeño retardo para debounce
}

// Función para obtener el siguiente tamaño de paso
unsigned long getNextStepSize(unsigned long currentStepSize) {
  if (currentStepSize == 10) {
    return 100;
  } else if (currentStepSize == 100) {
    return 1000;
  } else if (currentStepSize == 1000) {
    return 10000;
  } else {
    return 10;
  }
}

// Función para convertir la frecuencia actual en una cadena formateada
void fstring() {
  mhz = currentFrequency / 1000000;               // Obtiene la parte en MHz
  khz = (currentFrequency % 1000000) / 1000;      // Obtiene la parte en kHz
  hz = currentFrequency - ((mhz * 1000000) + (khz * 1000)); // Obtiene la parte en Hz

  // Formatea la parte en MHz para la pantalla
  smhz = String(mhz);
  if (mhz < 10 && mhz >= 1) {
    smhz = " " + String(mhz);
  }

  // Formatea la parte en Hz para la pantalla
  if (hz >= 100) {
    shz = "." + String(hz);
  } else if (hz < 100 && hz > 10) {
    shz = ".0" + String(hz);
  } else if (hz < 10 && hz >= 1) {
    shz = ".00" + String(hz);
  } else if (hz == 0) {
    shz = ".000";
  }

  // Formatea la parte en kHz para la pantalla
  if (khz >= 100) {
    skhz = "." + String(khz);
  } else if (khz < 100 && khz > 10) {
    skhz = ".0" + String(khz);
  } else if (khz < 10 && khz >= 1) {
    skhz = ".00" + String(khz);
  } else if (khz == 0) {
    skhz = ".000";
  }

  // Combina MHz, kHz y Hz en una sola cadena para la pantalla
  frequency_string = smhz + skhz + shz;
}

// Función para determinar la banda en base a la frecuencia
String getband(unsigned long currentFrequency1) {
  if (currentFrequency1 >= 1800000 && currentFrequency1 <= 2000000) {
    return "160m "; // Banda de 160 metros
  } else if (currentFrequency1 >= 3500000 && currentFrequency1 <= 4000000) {
    return "80m "; // Banda de 80 metros
  } else if (currentFrequency1 >= 7000000 && currentFrequency1 <= 7300000) {
    return "40m "; // Banda de 40 metros
  } else if (currentFrequency1 >= 14000000 && currentFrequency1 <= 14350000) {
    return "20m "; // Banda de 20 metros
  } else if (currentFrequency1 >= 21000000 && currentFrequency1 <= 21450000) {
    return "15m "; // Banda de 15 metros
  } else if (currentFrequency1 >= 26900000 && currentFrequency1 <= 27500000) {
    return "CB "; // Banda de CB
  } else if (currentFrequency1 >= 28000000 && currentFrequency1 <= 29700000) {
    return "10m "; // Banda de 10 metros
  } else {
    return "N/A "; // No se encuentra en ninguna banda específica
  }
}

void loop() {
  // Obtiene el tiempo actual
  unsigned long currentMillis = millis();

  // Si hubo un cambio en la frecuencia o el modo, actualiza la pantalla
  if (change) {
    actualizar();
    delay(20); // Retardo para estabilidad
  }

  // Maneja la actualización de audio cada intervalo definido
  if (currentMillis - lastAudioUpdate > audioUpdateInterval) {
    // Lee el estado de los pines SA y SB para determinar el modo
    SA = digitalRead(6);
    SB = digitalRead(7);

    // Si hay un cambio en los pines SA o SB, cambia el modo
    if (SA != previousSA || SB != previousSB) {
      if (SA == LOW && SB == LOW) {
        modeS = "AM";   // Cambia a modo AM
        Moffset = 3000; // Establece el offset para AM
        mode = 1;
      } else if (SA == HIGH && SB == LOW) {
        modeS = "LSB";  // Cambia a modo LSB
        Moffset = 0;    // Establece el offset para LSB
        mode = 2;
      } else if (SA == LOW && SB == HIGH) {
        modeS = "USB";  // Cambia a modo USB
        Moffset = 5000; // Establece el offset para USB
        mode = 3;
      }

      // Actualiza la pantalla con el nuevo modo
      actualizar();
      previousSA = SA;
      previousSB = SB;
    }

    lastAudioUpdate = currentMillis; // Actualiza el tiempo de la última actualización de audio
  }

  // Si el botón de cambio de tamaño de paso es presionado
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    stepSize = getNextStepSize(stepSize); // Cambia el tamaño del paso

    // Actualiza la cadena de texto que representa el tamaño del paso
    if (stepSize == 10) {
      fstep = "10Hz";
    } else if (stepSize == 100) {
      fstep = "100Hz";
    } else if (stepSize == 1000) {
      fstep = "1KHz";
    } else if (stepSize == 10000) {
      fstep = "10KHz";
    }
    actualizar(); // Actualiza la pantalla
  }
}
