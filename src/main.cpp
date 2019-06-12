#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define LED 2
#define LED_DELAY 250

#define AP_NAME "LOCK"

//Arreglos
char APssid[32] = {0}; //Arreglo para almacenar AP_SSID

//Variables
String ssid = "";
String pass = "";
String nfc = "";
String llave = "";
uint32_t chipId = 0;
unsigned int udpLocalPort = 2401;
unsigned long timeRef;

//Objetos
WiFiUDP udp;

//Declaracion de funciones
int handleRequest(String request);
String getMemoryData();
String saveMemoryData(String data);
void handleUDP();
void initWiFi();
void prepareMemoryData();
void setMemoryData(String data);

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
  if (readData != "")
  {
    setMemoryData(readData);
  }
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

/*** Servicios ***
 * 
 * Configurar dispositivo:
 * Guarda la red a la que se conecta el dispositivo
 * Valida que la llave sea valida, si no lo es,
 * no se guardan los datos
 * {
 *    "key":"config_wifi",
 *    "llave":"2803269",
 *    "data":{
 *        "ssid":"AP_OFICINA",
 *        "pass":"B1n4r1uM"
 *    }
 * }
 * 
 * Definir llave
 * {
 *    "key":"set_llave",
 *    "llave":"2803269",
 *    "data":{
 *        "llave":"AAAA000000A00",
 *    }
 * }
 * 
 * Definir tag NFC
 * {
 *    "key":"set_nfc",
 *    "llave":"2803269",
 *    "data":{
 *        "nfc":"29F4AD71",
 *    }
 * }
 * 
 * Abrir cerradura
 * {
 *    "key":"unlock",
 *    "device_id":"2803269",
 *    "llave":"2803269"
 * }
 * 
 * Borrar memoria
 * {
 *    "key":"erase",
 *    "llave":"2803269"
 * }
 * 
 * ******/

int handleRequest(String request)
{
  //Intentar hacer JSONParse
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(request);

  //Validar conversión
  if (root.success())
  {
    //Validar llave
    String inLlave = root["llave"];
    if (llave != inLlave)
    {
      //Error de autenticacion
      return 1;
    } 

    //Validar que exista "key"
    String key = root["key"];
    if (!key)
    {
      return -1;
    }

    //Evaluar key
    if (key == "unlock" || key == "erase")
    {
      if (key == "unlock")
      {
        //Nada aun :)
      }
    }
    else
    {
      //Obtener "data"
      String data = root["data"];
      if (!data)
      {
        return -1;
      }

      if (key == "set_llave")
      {
        String mLlave = root["data"]["llave"];
        llave = mLlave;
      }
      else if (key == "set_nfc")
      {
        String mNfc = root["data"]["nfc"];
        nfc = mNfc;
      }
      else if (key == "config_wifi")
      {
        String mSsid = root["data"]["ssid"];
        String mPass = root["data"]["pass"];
        ssid = mSsid;
        pass = mPass;
      }  

      //Guardar cambios
      prepareMemoryData();  
    }
  }
  else
  {
    return -1;
  }

  //Éxito
  return 0;
}

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
      Serial.print("Datos leidos -> ");
      Serial.println(data);

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
        //Enviar respuesta
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(APssid);
      }
      else
      {
        //Evaluar request
        int result = handleRequest(data);

        String response = "";
        if (result == 0)
        {
          response = "{\"response\":\"ok\", \"message\":\"Configuraciones WiFi recibidas\"}";
        }
        else if (result == -1)
        {
          response = "{\"response\":\"error\", \"message\":\"Error al procesar informacion\"}";
        }
        else if (result == 1)
        {
          response = "{\"response\":\"error\", \"message\":\"Error de autenticacion\"}";
        }

        //Enviar respuesta
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(response.c_str(), strlen(response.c_str()));
      }
    }

    //Cerrar paquete
    udp.endPacket();
  }
}

void initWiFi()
{
  //Limpiar configuraciones de WiFi
  WiFi.disconnect();
  WiFi.softAPdisconnect();

  //Obtener chipID
  chipId = ESP.getChipId();

  //Asignarlo a variable global para autenticacion
  llave = String(chipId);

  //Asignar nombre de dispositivo 
  sprintf(APssid, "%s_%d", AP_NAME, chipId);

  //Iniciar softAP
  WiFi.softAP(APssid, "12345678");

  //Inicializar UDP
  udp.begin(udpLocalPort);
}

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
void prepareMemoryData()
{
  //Crear objeto JSON
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();  

  //Agregar datos a objeto JSON
  root["ssid"] = ssid;
  root["pass"] = pass;
  root["llave"] = llave;
  root["nfc"] = nfc;

  //Convertir objeto JSON a String
  String output;
  root.prettyPrintTo(output);

  Serial.print("Almacenar en memoria -> ");
  Serial.println(output);

  //Guardar en memoria
  saveMemoryData(output);
}

void setMemoryData(String data)
{
  //Intentar hacer JSONParse
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(data);

  //Obtener datos
  String mSsid = root["ssid"];
  String mPass = root["pass"];
  String mNfc = root["nfc"];
  String mLlave = root["llave"];

  //Asignar a variables globales
  ssid = mSsid;
  pass = mPass;
  nfc = mNfc;
  llave = mLlave;
}