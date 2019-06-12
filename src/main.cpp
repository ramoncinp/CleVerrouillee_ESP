#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

#define LED 2
#define LED_DELAY 250

#define AP_NAME "LOCK"

//Arreglos
char APssid[32] = {0}; //Arreglo para almacenar AP_SSID

//Variables
String ssid = "";
String pass = "";
String nfc = "";
String key = "";
unsigned int udpLocalPort = 2401;
unsigned long timeRef;

//Objetos
WiFiUDP udp;

//Declaracion de funciones
String getMemoryData();
String saveMemoryData(String data);
void handleUDP();
void initWiFi();

//Funcion SETUP
void setup() 
{
  //Inicializar pines
  pinMode(LED, OUTPUT);

  //Inicializar Serial
  Serial.begin(9600);

  //Inicializar EEPROM
  EEPROM.begin(512);

  //Inicializar WiFi
  initWiFi();

  //Leer memoria
  String readData = getMemoryData();
  Serial.print("Datos leidos -> ");
  Serial.println(readData);
}

//Funcion Loop
void loop() 
{
  if (millis() - timeRef > LED_DELAY)
  {
    digitalWrite(LED, !digitalRead(LED));
    timeRef = millis();
  }

  //Manejar servidor UDP
  handleUDP();

  //Delay para estabilizacion 
  yield();
}

/******* DEFINICIÓN DE FUNCIONES *******/

/** Datos a almacenar en fomato JSON
 * 
 * {
 *    "ssid":"AP_OFICINA",
 *    "pass":"B1n4r1uM",
 *    "llave":"2803269",
 *    "nfc":"29F4AD71"
 * }
 * 
 * */
String getMemoryData()
{
  //Buscar inicio de objeto JSON 
  char start = 0xFF;
  String data = "";
  int addr = 0;

  while(start != '{')
  {
    //Leer cada localidad de memoria
    start = EEPROM.read(addr++);

    //Si no se encontró.. retornar vacío
    if (addr == 512) return "";
  }

  //Agregar inicio de objeto
  data += "{";

  //Intentar leer hasta el final de objeto JSON
  char readChar;
  for (int i = addr; i < 512; i++)
  {
    //Leer localidad de memoria
    readChar = EEPROM.read(i);
    
    //Validar caracter leido
    if (readChar == 0)
    {
      //Si es nulo... retornar cadena leída
      return data;
    }
    else
    {
      //Acumular caracteres
      data += readChar;
    }
  }

  //Si no encontró el caracter nulo... llegó hasta acá
  //Retornar vacío
  return "";
}

String saveMemoryData(String data)
{
  int addr = 0;

  for (int i = 0; i < data.length(); i++)
  {
    //Escribir cada caracter
    EEPROM.write(addr++, data.charAt(i));
  }

  //Agregar final de cadena
  EEPROM.write(addr, 0);

  //Hacer commit
  EEPROM.commit();
}

/*** Servicios ***
 * 
 * Configurar dispositivo:
 * Guarda la red a la que se conecta el dispositivo
 * Valida que la llave sea valida, si no lo es,
 * no se guardan los datos
 * 
 * {
 *    "ssid":"AP_OFICINA",
 *    "pass":"B1n4r1uM",
 *    "llave":"2803269"
 * }
 * 
 * 
 * ******/

void handleUDP()
{
  //Buffer para almacenar respuesta 
  char packetBuffer[255];
  //char replyBuffer[] = "Hola hola!";

  //Esperar un paquete UDP
  int packetSize = udp.parsePacket();
  
  //Validar si existe alguno
  if (packetSize)
  {
    //Leer el paquete
    int len = udp.read(packetBuffer, 255);
    if (len > 0)
    {
      //Agregar caracter nulo
      packetBuffer[len] = 0;
      
      //Interpretar dato
      String data = String(packetBuffer);

      //Mostrar mensaje recibido 
      Serial.print("Mensaje recibido por UDP -> ");
      Serial.println(data);

      if (data.indexOf("COPACETIC") != -1)
      {
        //Es mensaje para buscar dispositivo
        //Enviar una respuesta
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        //udp.write((uint8_t *)&replyBuffer, strlen(replyBuffer));
        udp.write(APssid);
      }
      else
      {
        //Guardar informacion
        saveMemoryData(data);

        //Es un mensaje JSON
        String response = "{\"response\":\"ok\", \"message\":\"Configuraciones WiFi recibidas\"}";
        
        //Enviar una respuesta
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(response.c_str(), strlen(response.c_str()));
      }
    }

    udp.endPacket();
  }
}

void initWiFi()
{
  //Limpiar configuraciones de WiFi
  WiFi.disconnect();
  WiFi.softAPdisconnect();

  //Obtener chipID
  uint32_t chipId = ESP.getChipId();
  //Asignarlo a variable global para autenticacion
  key = String(chipId);

  //Asignar nombre de dispositivo 
  sprintf(APssid, "%s_%d", AP_NAME, chipId);

  //Iniciar softAP
  WiFi.softAP(APssid, "12345678");

  //Inicializar UDP
  udp.begin(udpLocalPort);
}