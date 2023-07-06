/*******************************************************************************
 * Autor: Pablo González Morcillo
 * Versión: 1
 * Fecha: 4/7/2023
 *******************************************************************************/

// Librerías.
#include <lmic.h>
#include <hal/hal.h>
#include <SHT21.h>

// Definiciones.
#define pin_rele 13

#ifdef COMPILE_REGRESSION_TEST
  #define FILLMEIN 0
#else
  #warning "You must replace the values marked FILLMEIN with real values from the TTN control panel!"
  #define FILLMEIN (#dont edit this, edit the lines that use FILLMEIN)
#endif

// Objeto para el sensor SHT21.
SHT21 sht; 

// Variables de medida del sensor.
float temperatura = 0.0; 	// Variable para la temperatura.
float humedad = 0.0; // Variable para la humedad.

// Variable que indica si el relé está o no activado.
bool rele = false;

// Clave APPEUI en formato de cadena de caracteres hexadecimales en Little Endian.
static const u1_t PROGMEM APPEUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
void os_getArtEui(u1_t* buf) {
  memcpy_P(buf, APPEUI, 8);
}

// Clave DEVEUI en formato de cadena de caracteres hexadecimales en Little Endian.
static const u1_t PROGMEM DEVEUI[8] = { 0x7C, 0xDE, 0x05, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getDevEui(u1_t* buf) {
  memcpy_P(buf, DEVEUI, 8);
}


// Clave APPKEY en formato de cadena de caracteres hexadecimales en Big Endian.
static const u1_t PROGMEM APPKEY[16] = { 0x9D, 0x45, 0xD5, 0x96, 0x76, 0x75, 0xF3, 0xB7, 0x48, 0x3E, 0xB7, 0x7D, 0xF3, 0x8C, 0xA2, 0x0E };
void os_getDevKey(u1_t* buf) {
  memcpy_P(buf, APPKEY, 16);
}

// Esta estructura permite convertir automáticamente un valor 'Float' en Bytes y viceversa.
union u_tag {
  uint8_t b[4];
  float fval;
} u;

// Otras variables
static uint8_t mydata[128]; // Variable que almacena los datos que se envían a TTN.
int indice = 0; // Índice para el array de datos de la variable "mydata";

bool alerta = false; // Variable que se pone a 'True' si el sensor lee valores negativos.
bool enviar_error = false; // Si se leen valores negativos, esta variable se pone a 'True'.
static uint8_t mensaje_de_error[] = "ERROR"; // Si la variable 'enviar_error' está activa, se enviará estos datos en lugar de la variable "mydata".

static osjob_t sendjob; // Permite enviar datos por LoRaWAN.

// Intervalo de transmisiones
const unsigned TX_INTERVAL = 60;

// Intervalor de lectura del sensor.
const unsigned SHT_INTERVAL = 20;

// Variables para el control del temporizador de envío.
unsigned long ultima_captura_sensor = 0;
unsigned long nueva_captura_sensor = 0;
unsigned long diferencia_sensor = 0;

// Variables para la lectura de los datos que se reciben.
String myString = "";
char caracter = 'c';

// Mapa de pines para el transceptor LoRaWAN
const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 14,
  .dio = { 26, 33, 32 },
};

// Función que permite mostrar por el monitor serie una cadena de Bytes hexadecimales.
void printHex2(unsigned v) {
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

// Función que inicializa las variables del envío de datos.
void inicializarDatos(){
  Serial.println("Reinicio del vector");
  for(int i = 0; i < sizeof(mydata); i++){
    mydata[i] = 0;
  }
  indice = 0;
}

/*
  Función que se encarga de leer los sensores y enviar un paquete al iniciar la placa para obtener 
  las claves de sesión MAC y recibir la orden de encender o apagar el relé.
*/
void enviarPaqueteInicial(){
  temperatura = sht.getTemperature(); // Leer temperatura del sensor.
  humedad = sht.getHumidity(); // Leer humedad del sensor.

  // Bucle de control para medidas erróneas.
  while(temperatura < -10 || humedad < 0){
    // Mostrar medidas.
    Serial.println("ERROR EN LA MEDIDA: ");
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.print("ºC - ");
    Serial.print("Humedad: ");
    Serial.print(humedad);
    Serial.println("%");

    // Enviar alerta a TTN solo una vez.
    if(!alerta){
      enviar_error = true;
      do_send(&sendjob);
      alerta = !alerta;
    }
    delay(5000);
    
    // Nueva lectura del sensor.
    temperatura = sht.getTemperature();
    humedad = sht.getHumidity();
  }

  alerta != alerta;

  // Convertimos las variables float a bytes y las introducimos en el payload.
  u.fval = temperatura;
  for(int i = 0; i < 4; i++){
    mydata[indice + i] = u.b[i];
  }
  indice += 4;

  u.fval = humedad;
  for(int i = 0; i < 4; i++){
    mydata[indice + i] = u.b[i];
  }
  indice += 4;

  // Si el relé está encendido, introducimos un 1, sino un 0.
  if(rele){
    mydata[indice] = 1;
  }else{
    mydata[indice] = 0;
  }

  // Mostramos los datos del sensor
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.print("\t Humedad: ");
  Serial.println(humedad);

  // Enviamos datos.
  do_send(&sendjob);
}

// Función que maneja un temporizador de lectura del sensor y envío de datos a TTN.
void manejarDatos(){
  // Instantánea de tiempo.
  nueva_captura_sensor = millis();

  // Obtenemos la diiferencia del temporizador.
  if(nueva_captura_sensor < ultima_captura_sensor){ // Controlamos si el temporizador ha reiniciado el tiempo.
    diferencia_sensor = (ULONG_MAX - ultima_captura_sensor) + nueva_captura_sensor + 1;
  }else{
    diferencia_sensor = nueva_captura_sensor - ultima_captura_sensor;
  }

  // Si el temporizador ha llegado al tiempo establecido.
  if(diferencia_sensor > SHT_INTERVAL*1000){
    // Obtenemos los datos de temperatura y humedad del sensor.
    temperatura = sht.getTemperature();
    humedad = sht.getHumidity();

    // Control de lectura de valores erróneos.
    while(temperatura < -10 || humedad < 0){
      // Mostrar medidas.
      Serial.println("ERROR EN LA MEDIDA: ");
      Serial.print("Temperatura: ");
      Serial.print(temperatura);
      Serial.print("ºC - ");
      Serial.print("Humedad: ");
      Serial.print(humedad);
      Serial.println("%");

      // Enviar alerta a TTN solo una vez.
      if(!alerta){
        enviar_error = true;
        do_send(&sendjob);
        alerta = !alerta;
      }
      delay(5000);
      
      // Nueva lectura del sensor.
      temperatura = sht.getTemperature();
      humedad = sht.getHumidity();
    }

    alerta != alerta;

    // Convertimos las variables float a bytes y las introducimos en el payload.
    u.fval = temperatura;
    for(int i = 0; i < 4; i++){
      mydata[indice + i] = u.b[i];
    }
    indice += 4;

    u.fval = humedad;
    for(int i = 0; i < 4; i++){
      mydata[indice + i] = u.b[i];
    }
    indice += 4;

    // Si el relé está encendido, introducimos un 1, sino un 0.
    if(rele){
      mydata[indice] = 1;
    }else{
      mydata[indice] = 0;
    }

    indice ++;

    // Mostramos los datos.
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.print("\t Humedad: ");
    Serial.println(humedad);

    // Reiniciamos el temporizador.
    ultima_captura_sensor = nueva_captura_sensor;

    // Controlamos el envío.
    if(indice == 9*(TX_INTERVAL/SHT_INTERVAL)){ // Se realizan envíos cada 15 minutos, se recogen datos cada 5 minutos.
      do_send(&sendjob);
    }
  }
}

// Conjunto de eventos de la librería MCCI.
void onEvent(ev_t ev) {
  switch (ev) {
    case EV_SCAN_TIMEOUT:
      Serial.println(F("EV_SCAN_TIMEOUT"));
      break;
    case EV_BEACON_FOUND:
      Serial.println(F("EV_BEACON_FOUND"));
      break;
    case EV_BEACON_MISSED:
      Serial.println(F("EV_BEACON_MISSED"));
      break;
    case EV_BEACON_TRACKED:
      Serial.println(F("EV_BEACON_TRACKED"));
      break;
    case EV_JOINING:
      Serial.println(F("EV_JOINING"));
      break;
    case EV_JOINED:
      // Mostramos las claves de sesión.
      Serial.println(F(" --- EV_JOINED --- "));
      {
        u4_t netid = 0;
        devaddr_t devaddr = 0;
        u1_t nwkKey[16];
        u1_t artKey[16];
        LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
        Serial.print("netid: ");
        Serial.println(netid, DEC);
        Serial.print("devaddr: ");
        Serial.println(devaddr, HEX);
        Serial.print("AppSKey: ");
        for (size_t i = 0; i < sizeof(artKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(artKey[i]);
        }
        Serial.println("");
        Serial.print("NwkSKey: ");
        for (size_t i = 0; i < sizeof(nwkKey); ++i) {
          if (i != 0)
            Serial.print("-");
          printHex2(nwkKey[i]);
        }
        Serial.println();
      }
      LMIC_setLinkCheckMode(0);
      break;
    case EV_JOIN_FAILED:
      Serial.println(F("EV_JOIN_FAILED"));
      break;
    case EV_REJOIN_FAILED:
      Serial.println(F("EV_REJOIN_FAILED"));
      break;
    case EV_TXCOMPLETE:
      Serial.println(" --- EV_TXCOMPLETE --- ");

      setupClassCRx(); // Función para funcionamiento en Clase C.

      if (LMIC.txrxFlags & TXRX_ACK) {
        Serial.println(F("ACK Recibido"));
      }

      if (LMIC.dataLen) {
        Serial.print("Mensaje downlink recibido: ");

        // Recorremos el campo de datos de la trama LoRa.
        for (int i = 0; i < LMIC.dataLen; i++) {
          caracter = LMIC.frame[LMIC.dataBeg + i];
          myString += caracter;
        }

        // Mostramos mensaje recibido.
        Serial.println(myString);

        // Encendemos o apagamos el relé.
        if(myString == "ON"){
          rele = true;
          digitalWrite(pin_rele, HIGH);
        }else if(myString == "OFF"){
          rele = false;
          digitalWrite(pin_rele, LOW);
        }
        myString = "";
      }

      break;
    case EV_LOST_TSYNC:
      Serial.println(F("EV_LOST_TSYNC"));
      break;
    case EV_RESET:
      Serial.println(F("EV_RESET"));
      break;
    case EV_RXCOMPLETE:
      Serial.println (" --- EV_RXCOMPLETE --- ");
      Serial.print("Mensaje downlink recibido: ");

      // Recorremos el campo de datos de la trama LoRa.
      for (int i = 0; i < LMIC.dataLen; i++) {
        caracter = LMIC.frame[LMIC.dataBeg + i];
        myString += caracter;
      }

      // Mostramos mensaje recibido.
      Serial.println(myString);

      // Encendemos o apagamos el relé.
      if(myString == "ON"){
        rele = true;
        digitalWrite(pin_rele, HIGH);
      }else if(myString == "OFF"){
        rele = false;
        digitalWrite(pin_rele, LOW);
      }
      myString = "";

      break;
    case EV_LINK_DEAD:
      Serial.println(F("EV_LINK_DEAD"));
      break;
    case EV_LINK_ALIVE:
      Serial.println(F("EV_LINK_ALIVE"));
      break;
    case EV_TXSTART:
      Serial.println(" --- EV_TXSTART --- ");
      break;
    case EV_TXCANCELED:
      Serial.println(F("EV_TXCANCELED"));
      break;
    case EV_RXSTART:
      break;
    case EV_JOIN_TXCOMPLETE:
      Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
      delay(30000); // Espera de 30 segundos.
      enviarPaqueteInicial(); // Volvemos a enviar datos iniciales.
      break;
    default:
      Serial.print(F("Unknown event: "));
      Serial.println((unsigned)ev);
      break;
  }
}

void do_send(osjob_t* j) {
  // Comprobamos si hay algún envío o recepción pendiente.
  if (LMIC.opmode & OP_TXRXPEND) {
    Serial.println(F("OP_TXRXPEND, not sending"));
    ESP.restart(); // Reiniciamos la placa para volver a poder enviar los datos.
  } else {
    // Preparación del envío a TTN.
    if(enviar_error){ // Si hay un error, enviamos el mensaje de error.
      LMIC_setTxData2(1, mensaje_de_error, sizeof(mensaje_de_error)-1, 0);
      Serial.println(F("Error Packet queued"));
    }else{ // Si no hay error, enviamos los datos correspondientes.
      LMIC_setTxData2(1, mydata, indice, 0);
      Serial.println(F("Packet queued"));
      inicializarDatos();
    }
  }
}

void setup() {

  Wire.begin(); // Iniciamos conexión I2C.
  Serial.begin(115200); // Iniciamos monitor serie.

  Serial.println(F("Starting"));

  #ifdef VCC_ENABLE
    // For Pinoccio Scout boards
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
  #endif
  
  // Iniciamos pin del relé.
  pinMode(pin_rele, OUTPUT);
  

  // Reiniciamos el vector y el índice de datos.
  inicializarDatos();

  // Iniciamos el transceiver.
  os_init();
  // Reiniciamos la sesión MAC.
  LMIC_reset();

  // Definimos el Spread Factor de recepción.
  LMIC.dn2Freq = DR_SF9;
  
  // Definimos el Spread Factor de envío y la potencia de transmisión.
  LMIC_setDrTxpow(DR_SF7, 6);

  enviarPaqueteInicial();
}

void loop() {
  manejarDatos();

  os_runloop_once();
}
