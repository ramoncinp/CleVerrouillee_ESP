#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define LED 2
#define LOCK_PIN 4
#define TAG_STA 5
#define LED_DELAY_DISCONNECTED 150
#define LED_DELAY_CONNECTED 500

#define AP_NAME "LOCK"

//Arreglos
char APssid[32] = {0}; //Arreglo para almacenar AP_SSID

//Variables
bool unlockPinFlg = false;
String ssid = "";
String pass = "";
String nfc = "";
String llave = "";
uint32_t chipId = 0;
unsigned int udpLocalPort = 2401;
unsigned int ledDelay = LED_DELAY_DISCONNECTED; 
unsigned long timeRef;

//Objetos
WiFiUDP udp;

//Declaracion de funciones
char hexToASCII(char hexa);
int handleRequest(String request);
String getJSONConfig();
String getMemoryData();
String readNFCUid();
void handlePin();
void handleUDP();
void handleWifi();
void initWiFi();
void prepareMemoryData();
void readNFCTag();
void saveMemoryData(String data);
void setMemoryData(String data);
unsigned char calcCheckSum(unsigned char *pack, int packSize);

//Funcion SETUP
void setup() 
{
  //Inicializar pines
  pinMode(LED, OUTPUT);
  pinMode(LOCK_PIN, OUTPUT);
  pinMode(TAG_STA, INPUT);
  digitalWrite(LOCK_PIN, LOW);

  //Inicializar Serial
  Serial.begin(9600);

  //Inicializar EEPROM
  EEPROM.begin(512);

  //Leer memoria
  String readData = getMemoryData();
  if (readData != "")
  {
    setMemoryData(readData);
  }

  //Inicializar WiFi
  initWiFi();
}

//Funcion Loop
void loop() 
{
  if (millis() - timeRef > ledDelay)
  {
    digitalWrite(LED, !digitalRead(LED));
    timeRef = millis();
  }

  //Manejar servidor UDP
  handleUDP();

  //Manejar conexión WiFi
  handleWifi();

  //Manejar estado del pin
  handlePin();

  //Monitorear presencia de tag NFC
  readNFCTag();

  //Delay para estabilizacion 
  yield();
}


/******* DEFINICIÓN DE FUNCIONES *******/

/*
  hexToASCII()
  Convierte un caracter hexadecimal en un caracter ASCII
  Parametros: hexa, valor a convertir
  Salida: hexa, valor convertido a ASCII
*/
char hexToASCII(char hexa)
{
  hexa &= 0x0F;
  hexa += '0';
  if (hexa > '9')hexa += 7;
  return hexa;
}

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
 * Definir datos
 * {
 *    "key":"set_config",
 *    "llave":"2803269",
 *    "data":{
 *        "ssid":"",
 *        "pass":"",
 *        "nfc";"",
 *        "llave":""
 *    }
 * }
 * 
 * Abrir cerradura
 * {
 *    "key":"unlock",
 *    "llave":"2803269"
 * }
 * 
 * Obtener datos
 * {
 *    "key":"get_config",
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
        return 3;
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

      if (key == "set_config")
      {
        String mSsid = root["data"]["ssid"];
        String mPass = root["data"]["pass"];
        String mNfc = root["data"]["nfc"];
        String mLlave = root["data"]["llave"];
        ssid = mSsid;
        pass = mPass;
        nfc = mNfc;
        llave = mLlave;
      }
      else if (key == "config_wifi")
      {
        String mSsid = root["data"]["ssid"];
        String mPass = root["data"]["pass"];
        ssid = mSsid;
        pass = mPass;
      }  
      else if (key == "get_config")
      {
        //Indicar que se debe retornar la información del dispositivo
        return 2;
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

/** Datos a retornar en fomato JSON
 * 
 * {
 *    "response":"ok",
 *    "data":{
 *        "ssid":"AP_OFICINA",
 *        "pass":"B1n4r1uM",
 *        "llave":"2803269",
 *        "nfc":"29F4AD71"    
 *    }
 * }
 * 
 * */
String getJSONConfig()
{
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  //Definir respuesta
  root["response"] = "ok";
  
  JsonObject& data = root.createNestedObject("data");
  data["ssid"] = ssid;
  data["pass"] = pass;
  data["llave"] = llave;
  data["nfc"] = nfc;

  //Convertir objeto JSON a String
  String output;
  root.printTo(output);

  return output;
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

String readNFCUid()
{
  String result = "";
  unsigned char preambNFC[4] = {0xBA, 0x02, 0x01, 0xB9};
  
  //Enviar comando para leer página seleccionada
  Serial.write(preambNFC, 4);
  
  //Esperar respuesta
  while (!Serial.available())
  {
    delay(5);
  }
  delay(50);
  
  //Mostrar respuesta
  int idx = 0;
  int responseLen = 0;
  int uidIdxLimit = 0;

  while (Serial.available())
  {
    char mByte = Serial.read();
    if (idx == 1)
    {
      responseLen = mByte;
      if (responseLen == 8)
      {
        uidIdxLimit = 7;
      }
      else
      {
        uidIdxLimit = 10;
      }
    }
    
    if (idx == 3 && mByte != 0x00)
    {
      //Si hubo una falla
      break;
    }
  
    if (idx >= 4 && idx <= uidIdxLimit) 
    {
      //Concatenar los bytes de datos de la pagina
      result += hexToASCII((char) mByte >> 4);
      result += hexToASCII((char) mByte);
    }
    
    //Incrementar indice
    idx++;
  }
  
  //Hacer un SerialFlush
  delay(50);
  while (Serial.available())
  {
    Serial.read();
  }
  
  //Retornar el uid
  return result;
}

unsigned char calcCheckSum(unsigned char *pack, int packSize)
{
  unsigned CheckSum = 0;
  for (int i = 0; i < (packSize - 1); i++)
  {
    CheckSum ^= pack[i];
  }
  return CheckSum;
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
        udp.endPacket();
      }
      else
      {
        //Evaluar request
        int result = handleRequest(data);

        String response = "";
        if (result == 0)
        {
          response = "{\"response\":\"ok\", \"message\":\"Configuraciones recibidas\"}";
        }
        else if (result == -1)
        {
          response = "{\"response\":\"error\", \"message\":\"Error al procesar informacion\"}";
        }
        else if (result == 1)
        {
          response = "{\"response\":\"error\", \"message\":\"Error de autenticacion\"}";
        }
        else if (result == 2)
        {
          response = getJSONConfig();
        }
        else if (result == 3)
        {
          //Activar cerradura
          unlockPinFlg = true;

          //Definir respuesta
          response = "{\"response\":\"ok\", \"message\":\"Cerradura activada\"}";
        }

        //Mostrar respuesta en Serial
        Serial.print("Enviando respuesta ->");
        Serial.println(response);

        //Enviar respuesta
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(response.c_str(), strlen(response.c_str()));
        udp.endPacket();
      }
    }
  }
}

void handleWifi()
{
  static unsigned long timeRef;

  if (millis() - timeRef > 3000)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      ledDelay = LED_DELAY_CONNECTED;
    }
    else
    {
      ledDelay = LED_DELAY_DISCONNECTED;
    }
    timeRef = millis();
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
  if (llave == "") llave = String(chipId);

  //Asignar nombre de dispositivo 
  sprintf(APssid, "%s_%d", AP_NAME, chipId);

  //Iniciar softAP
  WiFi.softAP(APssid, "12345678");

  //Inicializar UDP
  udp.begin(udpLocalPort);

  //Conectarse a la red
  if (ssid != "" && pass != "")
  {
    Serial.println("Conectandose a la red WiFi...");
    Serial.print("SSID -> ");
    Serial.println(ssid);
    Serial.print("PASS -> ");
    Serial.println(pass);
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
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
  root.printTo(output);

  Serial.print("Almacenar en memoria -> ");
  Serial.println(output);

  //Guardar en memoria
  saveMemoryData(output);
}

void readNFCTag()
{
  //Preparar variable que obtiene datos del tag
  String readData = "";
  bool valid = true;
  
  //Si no hay un tag presente, retornar
  if (digitalRead(TAG_STA) == HIGH)
  {
    //No hay tag presente
    return;
  }

  //Obtener id
  readData = readNFCUid();
  if (readData != "")
  {
    Serial.print("Tag leido -> ");
    Serial.println(readData);

    if (readData == nfc)
    {
      //Abrir cerradura
      unlockPinFlg = true;
    }
  }
  
  //Esperar a que el tag deje de estar presente
  while (digitalRead(TAG_STA) == LOW)
  {
    delay(1);
  }
}

void saveMemoryData(String data)
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

void handlePin()
{
  static unsigned long timeRef;

  //Si se pidió activar pero esta desactivado
  if (unlockPinFlg)
  {
    if (digitalRead(LOCK_PIN) == LOW)
    {
      //Activar
      Serial.print("Activar!");
      digitalWrite(LOCK_PIN, HIGH);
      timeRef = millis();
    }
    else
    {
      //Desactivar
      if (millis() - timeRef > 2000)
      {
        digitalWrite(LOCK_PIN, LOW);  
        unlockPinFlg = false;
      }
    }
  }
}