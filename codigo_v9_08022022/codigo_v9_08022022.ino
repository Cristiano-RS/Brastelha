#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include "time.h"
#include <FS.h>      //File System [ https://github.com/espressif/arduino-esp32/tree/master/libraries/FS ]
#include <SPIFFS.h>  //SPI Flash File System [ https://github.com/espressif/arduino-esp32/tree/master/libraries/SPIFFS ]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "EEPROM.h"
#include <WiFiUdp.h>

/*
  Emulação EEPROM adicionada 
*/

int data = 0;
#define EEPROM_SIZE 2

/* ---------------- In and Out Pins -----------------------*/
//Usar adc_1, pois adc_2 interfere no WiFi, usar pinos --> 33,32,35,34,39,36
#define input_ind 34
#define inputA 36  //VN
#define inputB 39  //VP
#define input_SPIFFS 35
/* ----------------- WiFi network name and password --------------------*/
const char* ssid = "Brastelha IoT";        // Brastelha IoT //Industria Brastelha
const char* password = "*Brastelha_@IoT";  //*Brastelha_@IoT //*Brastelha_@
/* ----------------- RTC --------------------*/
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800;
const int daylightOffset_sec = 0;
String conv_str_date;
String conv_str_time;
String conv_str_day;
bool timeinfo_failed = false;
//chars should be enough to contain all the datetimes
/* ------------ Interrupcao por timer ------------------*/
volatile int interruptCounter;
/* ----------- Sensor indutivo ------------------*/
unsigned int amount = 0;
int x = 0;
int double_side_switch = 0;
/* --------------- Encoder --------------------*/
long PulsesNumber = 0;
bool estadoEnc = false;
float CteWheel = 0.087266, length = 0.0;  //CteWheel = 0.036815538 para 12cm ---- CteWheel = c para 10.8cm ---- CteWheel = 0.0833394 para 9.55cm
//CteWheel = 0.087266 para 10cm de diametro
float medida_encoder_total = 0.0;
int aState;
int aLastState;
/* -------Cod_resposta_POST_JSON-------*/
int httpResponseCode;
bool flag = false;
bool setup_v = false;
bool resend = false;
bool save = false;
int counter_cycle = 0;
/* -------SPIFFS-------*/
String files[20];
int counter_index = 0;
int counter_read = 0;
bool flag_1 = false;
int ID = 0;
LiquidCrystal_I2C lcd(0x27, 16, 2);  //Inicializa o display no endereco 0x27, dimensões display 16,2 ou 20,4

hw_timer_t* timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  portEXIT_CRITICAL_ISR(&timerMux);

  aState = digitalRead(inputA);  // Reads the "current" state of the outputA
  // If the previous and the current state of the outputA are different, that means a Pulse has occured
  if (aState != aLastState) {
    // If the outputB state is different to the outputA state, that means the encoder is rotating clockwise
    if (digitalRead(inputB) != aState) {
      PulsesNumber--;
    } else {
      PulsesNumber++;
    }
    /*  
    if(PulsesNumber > 60){
      //Logica para detectar inicio da perfilacao da telha
    }
    ////Serial.println(PulsesNumber)*/
  }
  aLastState = aState;
}

/*--- ESCREVE O ARQUIVO ---*/
bool writeFile(String values, String pathFile, bool appending) {
  char* mode = "w";           //open for writing (creates file if it doesn't exist). Deletes content and overwrites the file.
  if (appending) mode = "a";  //open for appending (creates file if it doesn't exist)
  //Serial.println("- Writing file: " + pathFile);
  //Serial.println("- Values: " + values);
  SPIFFS.begin(true);
  File wFile = SPIFFS.open(pathFile, mode);
  if (!wFile) {
    //Serial.println("- Failed to write file.");
    return false;
  } else {
    wFile.println(values);
    //Serial.println("- Written!");
  }
  wFile.close();
  return true;
}

/*--- LEITURA DO ARQUIVO ---*/
String readFile(String pathFile) {
  ////Serial.println(pathFile); //lê pelo nome de arquivo
  SPIFFS.begin(true);
  File rFile = SPIFFS.open(pathFile, "r");
  String values;
  if (!rFile) {
    //Serial.println("- Failed to open file.");
  } else {
    while (rFile.available()) {
      values += rFile.readString();
    }
    //Serial.println("Dados do arquivo: ");
    //Serial.println(values);
  }
  rFile.close();
  /*
  char sz[500];
  int ID = 0;
  char buf[sizeof(sz)];
  values.toCharArray(buf, sizeof(buf));
  char *p = buf;
  char *str;

  while ((str = strtok_r(p, ",", &p)) != NULL){ // delimiter is the semicolon
    ////Serial.println(str);
    ID++;
  }
  ID = ID-1;

  //Serial.println("ID: " + String(ID));*/
  return values;
}

/*--- APAGA O ARQUIVO ---*/
bool deleteFile(String pathFile) {
  //Serial.println("- Deleting file: " + pathFile);
  SPIFFS.begin(true);
  if (!SPIFFS.remove(pathFile)) {
    //Serial.println("- Delete failed.");
    return false;
  } else {
    //Serial.println("- File deleted!");
    return true;
  }
}

/*--- RENOMEIA O ARQUIVO ---*/
/*void renameFile(String pathFileFrom, String pathFileTo) {
  //Serial.println("- Renaming file " + pathFileFrom + " to " + pathFileTo);
  SPIFFS.begin(true);
  if (!SPIFFS.rename(pathFileFrom, pathFileTo)) {
    //Serial.println("- Rename failed.");
  } else {
    //Serial.println("- File renamed!");
  }
}*/

/*--- FORMATA O FILE SYSTEM ---*/
bool formatFS() {
  //Serial.println("- Formatting file system...");
  SPIFFS.begin(true);
  if (!SPIFFS.format()) {
    //Serial.println("- Format failed.");
    return false;
  } else {
    //Serial.println("- Formatted!");
    return true;
  }
}

/*--- LISTA ARQUIVOS ---*/
void listFiles(String path) {
  ////Serial.println("- Listing files: " + path);
  counter_index = 0;
  SPIFFS.begin(true);
  File root = SPIFFS.open(path);
  if (!root) {
    //Serial.println("- Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    //Serial.println("- Not a directory: " + path);
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("- Dir: ");
      //Serial.println(file.name());
    } else {
      Serial.print("- Arquivo: ");
      //Serial.println(file.name());
      /*
      Serial.print("\tSize: ");
      //Serial.println(file.size());
      */
      files[counter_index] = file.name();
      counter_index++;
    }
    file = root.openNextFile();
  }
}

void init_wifi() {
  // Wifi Inicio
  int counter_wifi = 0;
  WiFi.begin(ssid, password);
  // Init and get the time
  while (WiFi.status() != WL_CONNECTED) {  //Check for the connection

    counter_wifi++;
    delay(200);
    if (counter_wifi < 1) {
      ////Serial.println("Conectando WiFi");
      if (counter_wifi < 101) {
        //Serial.print(".");// ---> colocar no Display
      } else {
        //Serial.println("Reiniciando dispositivo");
        ESP.restart();
      }
    }
  }
  float tempo_total = counter_wifi / 10.0;
  ////Serial.println("Conectado, tempo total: " + String(tempo_total) + " segundos.");
  ////Serial.println("WiFi ok");
}

void print_init_information() {

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    timeinfo_failed = true;
    //return;
  }
  ////Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  ////Serial.println();
  if (setup_v == true && timeinfo_failed == false) {
    lcd.setCursor(0, 0);
    lcd.print("WiFi Conectado!");
    delay(1000);
    lcd.clear();
    delay(500);
    lcd.setCursor(0, 0);
    lcd.print(&timeinfo, "%a %d/%m/%y");
    lcd.setCursor(0, 1);
    lcd.print(&timeinfo, "Horario %H:%M:%S");
    delay(2500);
    lcd.clear();
    delay(500);
    lcd.setCursor(0, 0);
    lcd.print("   Aguardando");
    lcd.setCursor(0, 1);
    lcd.print("    Producao");
    setup_v = false;
    ////Serial.println(hour());

    char timeStringBuff[9];  //chars should be enough to contain all the datetimes
    strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%y", &timeinfo);
    //print like "const char*"
    conv_str_date = String(timeStringBuff);  // convert to String for future use conditions, example if statement below
    //Serial.println(timeStringBuff);

    char timedayStringBuff[3];  //chars should be enough to contain all the datetimes
    strftime(timedayStringBuff, sizeof(timedayStringBuff), "%d", &timeinfo);
    //print like "const char*"
    conv_str_day = String(timedayStringBuff);  // convert to String for future use conditions, example if statement below
    //Serial.println(timedayStringBuff);
    
    //-----converter variável abaixo String to int quando sobrar tempo ----- reduz linhas e statements 
    if (conv_str_day == "01") {
      data = 1;
      //Serial.println("Valor 1 de data");
    }
    if (conv_str_day == "02") {
      data = 2;
      //Serial.println("Valor 2 de data");
    }
    if (conv_str_day == "03") {
      data = 3;
      //Serial.println("Valor 3 de data");
    }
    if (conv_str_day == "04") {
      data = 4;
      //Serial.println("Valor 4 de data");
    }
    if (conv_str_day == "05") {
      data = 5;
      //Serial.println("Valor 5 de data");
    }
    if (conv_str_day == "06") {
      data = 6;
      //Serial.println("Valor 6 de data");
    }
    if (conv_str_day == "07") {
      data = 7;
      //Serial.println("Valor 7 de data");
    }
    if (conv_str_day == "08") {
      data = 8;
      //Serial.println("Valor 8 de data");
    }
    if (conv_str_day == "09") {
      data = 9;
      //Serial.println("Valor 9 de data");
    }
    if (conv_str_day == "10") {
      data = 10;
      //Serial.println("Valor 10 de data");
    }
    if (conv_str_day == "11") {
      data = 11;
      //Serial.println("Valor 11 de data");
    }
    if (conv_str_day == "12") {
      data = 12;
      //Serial.println("Valor 12 de data");
    }
    if (conv_str_day == "13") {
      data = 13;
      //Serial.println("Valor 13 de data");
    }
    if (conv_str_day == "14") {
      data = 14;
      //Serial.println("Valor 14 de data");
    }
    if (conv_str_day == "15") {
      data = 15;
      //Serial.println("Valor 15 de data");
    }
    if (conv_str_day == "16") {
      data = 16;
      //Serial.println("Valor 16 de data");
    }
    if (conv_str_day == "17") {
      data = 17;
      //Serial.println("Valor 17 de data");
    }
    if (conv_str_day == "18") {
      data = 18;
      //Serial.println("Valor 18 de data");
    }
    if (conv_str_day == "19") {
      data = 19;
      //Serial.println("Valor 19 de data");
    }
    if (conv_str_day == "20") {
      data = 20;
      //Serial.println("Valor 20 de data");
    }
    if (conv_str_day == "21") {
      data = 21;
      //Serial.println("Valor 21 de data");
    }
    if (conv_str_day == "22") {
      data = 22;
      //Serial.println("Valor 22 de data");
    }
    if (conv_str_day == "23") {
      data = 23;
      //Serial.println("Valor 23 de data");
    }
    if (conv_str_day == "24") {
      data = 24;
      //Serial.println("Valor 24 de data");
    }
    if (conv_str_day == "25") {
      data = 25;
      //Serial.println("Valor 25 de data");
    }
    if (conv_str_day == "26") {
      data = 26;
      //Serial.println("Valor 26 de data");
    }
    if (conv_str_day == "27") {
      data = 27;
      //Serial.println("Valor 27 de data");
    }
    if (conv_str_day == "28") {
      data = 28;
      //Serial.println("Valor 28 de data");
    }
    if (conv_str_day == "29") {
      data = 29;
      //Serial.println("Valor 29 de data");
    }
    if (conv_str_day == "30") {
      data = 30;
      //Serial.println("Valor 30 de data");
    }
    if (conv_str_day == "31") {
      data = 31;
      //Serial.println("Valor 31 de data");
    }
    ////Serial.println("Valor invalido!");
  }

  if (timeinfo_failed == true) {

    conv_str_date = "sem_data";
    //Serial.println(conv_str_date);  //debug
    lcd.clear();
    delay(500);
    lcd.setCursor(0, 0);
    lcd.print("Falha NTP!");
    delay(1000);
    lcd.clear();
    delay(500);
    lcd.setCursor(0, 0);
    lcd.print("   Aguardando");
    lcd.setCursor(0, 1);
    lcd.print("    Producao");
    setup_v = false;
  }
}

void setup() {

  delay(4000);  //Delay needed before calling the WiFi.begin
  lcd.init();
  lcd.setBacklight(0x1);  //inicio plotagem display
  lcd.setCursor(0, 0);
  lcd.print("Brastelha IoT");
  lcd.setCursor(0, 1);
  lcd.print("Bom trabalho!");  // fim plotagem display
  Serial.begin(115200);
  delay(3000);
  //encoder
  pinMode(inputA, INPUT_PULLUP);
  pinMode(inputB, INPUT_PULLUP);
  //sensorindutivo
  pinMode(input_ind, INPUT);
  //dados_spiffs
  pinMode(input_SPIFFS, INPUT_PULLDOWN);
  //interrup
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 100, true);
  timerAlarmEnable(timer);
  //pin_out_ //pinMode(35, OUTPUT);//DEFINE O PINO COMO SAiDA
  //digitalWrite(35, LOW);//pino saida de acionamento INICIA DESLIGADO
  init_wifi();
  ////Serial.println("WiFi ok");
  lcd.clear();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setup_v = true;
  print_init_information();
  // begin Test EEPROMClass (Initialise)
  //Serial.println("Testing EEPROMClass");

  /*int conv_atoi = atoi(timedayStringBuff);
  EEPROM.write(0, byte(conv_atoi));
  //Serial.println(conv_atoi);*/

  if (!EEPROM.begin(EEPROM_SIZE)) {
    //Serial.println("failed to initialise EEPROM");
    delay(500);
    ESP.restart();
  }
  for (int i = 0; i < EEPROM_SIZE; i++) {
    Serial.print(EEPROM.read(i));
    Serial.print(" ");
  }

  // init logic state production between EEPROM and Data
  // Read EEPROM and compare date day value from date hold on obtained datestring
  //Serial.println("Quant. gravada: " + String(EEPROM.read(1)));
  String data_mem = String(EEPROM.read(0));
  if (data_mem == String(data)) {
    amount = byte(EEPROM.read(1));
    //Serial.println("Data EEPROM encontrada!");  //debug
  } else {
    if (conv_str_date != "sem_data") {
      //int conv_atoi = atoi(timeStringBuff);
      EEPROM.write(0, byte(data));
      //Serial.println("Quantidade zerada!");  //debug
      EEPROM.write(1, byte(0));
      EEPROM.commit();
    }
  }
  //Serial.println("Data gravada: " + String(EEPROM.read(0)));
  //Serial.println("Finalização Setup");  //debug
}

void save_SPIFFS() {

  //Serial.println("Inicia save_SPIFFS");  //debug
  //Save in file SPIFFS
  save = false;
  read_SPIFFS();
  ////Serial.println("\nEscreve novo arquivo");
  //Serial.println("Voltando a save_SPIFFS");  //debug

  if (counter_index > 0) {

    //Serial.println("Counter_index é maior que 0, então há arquivos!");  //debug
    //Serial.println("Percorrendo loop for");                             //debug

    for (int i = 0; i < counter_index; i++) {

      //Serial.println("Leitura For: " + files[i]);
      //Serial.println();
      if (("/" + conv_str_date) == files[i]) {

        writeFile(/*String((ID/3)+1) + "," + */ String(amount) + "," + String(length) + ",", "/" + conv_str_date, true);  //inserir ID, pois pode haver dados repetidos
        Serial.print("escreve na linha de arquivo existente: ");
        //Serial.println(files[i]);
      }
      if (("/" + conv_str_date) != files[i] && i == (counter_index - 1)) {
        /* 1 */ writeFile(/*(String((ID/3)+1) + "," + */ String(amount) + "," + String(length) + ",", "/" + conv_str_date, false);
        Serial.print("cria novo arquivo");
      }
    }
  } else {
    //Serial.println("Counter_index NAO é maior que 0, não existem arquivos!");  //debug
    writeFile(/*String((ID/3)+1) + "," + */ String(amount) + "," + String(length) + ",", "/" + conv_str_date, false);
  }
  delay(50);
  resend = true;
  //Serial.println("Finalização save_SPIFFS");  //debug
}

void read_SPIFFS() {

  //Serial.println("Inicio read_SPIFFS");  //debug
  //This function makes a read from a content files
  //Serial.println("Listando Arquivos: ");  //\n
  listFiles("/");
  delay(1000);
  //Serial.println("Lendo Arquivos");  //debug
  for (int i = 0; i < counter_index; i++) {
    //Serial.println("Arquivo lido: " + files[i] + " Indice: " + (i + 1) + "/" + counter_index);  //debug
    readFile(files[i]);                                                                         //read file by name (String) in current index
  }
  //Serial.println("Finalização read_SPIFFS");  //debug
}

void send_plot_info() {

  //Serial.println("Inicio send_plot_info");  //debug

  //unsigned long time = millis();
  lcd.clear();
  lcd.setBacklight(0x1);  //inicio plotagem display
  lcd.setCursor(0, 0);
  lcd.print("Un: " + String(amount) + " T: " + String(length / 100.00, 2) + " m");  //"Nº Telhas"
  lcd.setCursor(0, 1);
  lcd.print("Total: " + String(medida_encoder_total / 100.00, 2));  //Fim plotagem display
  /*
  Serial.print("Un: " + String(amount) + " " + String(length/100.00,2) + " m");
  //Serial.println("Total: " + String(medida_encoder_total/100.00,2));
  */
  if (WiFi.status() == WL_CONNECTED) {  //Check WiFi connection status
    if (amount != 0) {

      HTTPClient http;
      http.begin("https://brastelha.d3t.com.br/api/set-oee");  //Specify destination for HTTP request
      http.addHeader("Content-Type", "application/json");      //Specify content-type header

      httpResponseCode = http.POST("{\n    \"machine_id\": 20,\n    \"amount\": " + String(amount) + ",\n    \"size\": " + String(length / 100.00, 2) + "\n}");
      //Send the actual POST request - machine: 19 = PER0012
      //machine: 5 = PER0002
      //machine: 4 = PER0001
      //machine: 20 = PER0013
      if (httpResponseCode > 0) {
        //String response = http.getString();   //Get the response to the request
        //Serial.println(httpResponseCode);
        //Print return code   ////Serial.println(response);    //Print request answer
        if (httpResponseCode == 200) {
          //Recebimento confirmado pelo sevidor
          lcd.clear();
          lcd.setBacklight(0x1);  //inicio plotagem display
          lcd.setCursor(0, 0);
          lcd.print("Dados enviados!");  //"Nº Telhas"

          delay(500);
          lcd.clear();
          lcd.setBacklight(0x1);  //inicio plotagem display
          lcd.setCursor(0, 0);
          lcd.print("Un: " + String(amount) + " T: " + String(length / 100.00, 2) + " m");  //"Nº Telhas"
          lcd.setCursor(0, 1);
          lcd.print("Total: " + String(medida_encoder_total / 100.00, 2));  //Fim plotagem display
          lcd.setCursor(15, 1);
          lcd.print(".");
          resend = false;
          save = false;
          counter_cycle = 0;
        } else {
          //Recebimento nao confirmado
          save = true;
          lcd.clear();
          lcd.setBacklight(0x1);
          lcd.setCursor(0, 0);
          lcd.print("Cod: " + String(httpResponseCode));
          counter_cycle++;
        }
      } else {
        //Serial.print("Error on sending POST: ");
        ////Serial.println(httpResponseCode);
        //vai para protocolo de reenvio
        save = true;
        lcd.clear();
        lcd.setBacklight(0x1);
        lcd.setCursor(0, 0);
        lcd.print("Erro: " + String(httpResponseCode));
        delay(500);
        counter_cycle++;
        lcd.clear();
        lcd.setBacklight(0x1);  //inicio plotagem display
        lcd.setCursor(0, 0);
        lcd.print("Un: " + String(amount) + " T: " + String(length / 100.00, 2) + " m");  //"Nº Telhas"
        lcd.setCursor(0, 1);
        lcd.print("Total: " + String(medida_encoder_total / 100.00, 2));  //Fim plotagem display
        lcd.setCursor(15, 1);
        lcd.print(":");
      }
      http.end();  //Free resources
    }
  } else {
    //Serial.println("Error in WiFi connection");  // implement a reconnect protocol
    lcd.clear();
    lcd.setBacklight(0x1);
    lcd.setCursor(0, 0);
    lcd.print("Problema no WiFi");
    save = true;
    counter_cycle++;
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    // Init and get the time
    while (WiFi.status() != WL_CONNECTED) {  //Check for the connection
      delay(200);
      //Serial.println("Reconectando WiFi");  // ---> colocar no Display
    }
    //Serial.println("WiFi ok");
  }
  //length = 0.0;
  //Serial.println("Finalização send_plot_info");  //debug
}

void loop() {

  if (digitalRead(input_ind) == HIGH and flag == false) { 

    //Lógica abaixo serve para eliminar o ruido causado pela contatora do hidraulico
    while (digitalRead(input_ind) == HIGH) {
    x++;
    delay(10);
    //Serial.println(x);
    }

    if (x > 10) {
      double_side_switch++;
      flag = true;

      length = (PulsesNumber * CteWheel) / 2;  //divide por dois pq a amount de pulsos e dobrada com dois canais
      
      if(double_side_switch == 2){

      double_side_switch = 0;
      amount++;
      PulsesNumber = 0;

      medida_encoder_total += length;
      EEPROM.write(1, byte(amount));
      EEPROM.commit();  //save write
      //currentInterrupt = interruptCounter;
      send_plot_info();
      //--- //Serial.println(byte(EEPROM.read(1)));
      //readUShort(address));
      //float tempo = (tempo_init - millis())/1000;
      x = 0;
      }
    } else {
    }
  }
  if (digitalRead(input_ind) == LOW and flag == true) {
    flag = false;
    //PulsesNumber = 0;
  }
  if (save == true) {
    save_SPIFFS();
    /* 
    if(resend == true && counter_cycle < 1)
       send_plot_info();
    */
  }
  if (digitalRead(input_SPIFFS) == HIGH and counter_read < 1) {
    flag_1 = true;
    read_SPIFFS();
    counter_read++;
  }
  if (digitalRead(input_SPIFFS) == LOW and flag_1 == true) {
    flag_1 = false;
  }
  if (Serial.available()) {          // Verificar se há caracteres disponíveis
    char caractere = Serial.read();  // Armazena caractere lido
    Serial.print(caractere);
    if (isDigit(caractere)) {  // Verificar se o caractere ASCII é um dígito entre 0 e 9
                               //zera dados de quantidade produzida da eeprom
      if (caractere == '0') {
        //Serial.println("Quantidade na memória RAM: " + String(amount));
        //Serial.println("Zerando RAM...");
        //Serial.println("RAM igual a = " + String(amount));
        amount = 0;
        //Serial.println("Zerando quantidade armazenada na EEPROM");
        EEPROM.write(1, byte(amount));
        EEPROM.commit();
      }
    } else {
      //Serial.println("Comando não reconhecido!");
    }
  }
}