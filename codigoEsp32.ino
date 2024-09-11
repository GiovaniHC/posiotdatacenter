#include <WiFi.h>
#include "DHT.h"
#include "time.h"
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>

//SSID da rede
char ssid[] = "SSID da rede WiFi";
//senha da rede
char pass[] = "Senha da rede WiFi";

//local onde esta o sensor
const String local = "Identificação do Data Center";

//Configurações servidor MQTT
const char* mqttServer = "IP do servidor";
const int mqttPort = 1883;
const char* mqttUser = "Usuário do broker";
const char* mqttPassword = "Senha do broker";

#define DHT22PIN 27 //Pino de dados do sensor DHT22
//Configurações do servidor ntp
#define NTP_SERVER "a.ntp.br"
#define UTC_OFFSET -10800
#define UTC_OFFSET_DST 0

WiFiClient espClient;
PubSubClient client(espClient);

const int PINO_BUZZER = 26;  // Pino D23 conectado ao i/o do módulo buzzer

/**Valores de alerta de umidade, para acionar o buzzer**/
const int minUmiPerigo = 20;
const int maxUmiPerigo = 80;
/**Valores de alerta de temperatura, para acionar o buzzer**/
const int minTempPerigo = 18;
const int maxTempPerigo = 27;

DHT dht(DHT22PIN, DHT22);
LiquidCrystal_I2C lcd(0x27, 16, 2);

//Para conseguir exibir o "°" no display
byte degree_symbol[8] = {
  0b00111,
  0b00101,
  0b00111,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

void printLocalTimeDisplay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    lcd.setCursor(0, 1);
    lcd.println("Erro de conexão ");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.println("Hora:      ");
  lcd.setCursor(11, 0);
  lcd.println(&timeinfo, "%H:%M");

  lcd.setCursor(0, 1);
  lcd.println("Data:  ");
  lcd.setCursor(6, 1);
  lcd.println(&timeinfo, "%d/%m/%Y");
}

char* printLocalTime() {
  char* dataHora = (char*)malloc(20);  // Aloca memória para a data e hora
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Falha ao obter a hora local");
    strcpy(dataHora, "0000000000000000000");
    return dataHora;
  }

  // Formata a data e hora no formato do mongodb
  strftime(dataHora, 20, "%Y-%m-%dT%H:%M:%S", &timeinfo);

  return dataHora;
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_BUZZER, OUTPUT);  // Define o PINO do buzzer como saída

  // Inicia o sensor DHT22
  dht.begin();
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, degree_symbol);
  lcd.setCursor(0, 0);
  lcd.print("Conectando ao ");
  lcd.setCursor(0, 1);
  lcd.print("WiFi...");
  WiFi.begin(ssid, pass);
  delay(1000);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(PINO_BUZZER, HIGH);  // Ligar o buzzer
    delay(50);
    digitalWrite(PINO_BUZZER, LOW);  // Ligar o buzzer
    delay(100);
    digitalWrite(PINO_BUZZER, HIGH);  // Ligar o buzzer
    delay(50);
  }
  digitalWrite(PINO_BUZZER, LOW);  // Desliga o buzzer ao ficar online
  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqttServer, mqttPort);

  //Conecta ao broker MQTT
  while (!client.connected()) {
    Serial.println("Conectando no MQTT...");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.println("Conectando      ");
    lcd.setCursor(0, 1);
    lcd.println("no MQTT...      ");
    if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
      Serial.println("Conectado");
    } else {
      Serial.print("Falha ");
      Serial.print(client.state());
      delay(2000);
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.println("Online");
  lcd.setCursor(0, 1);
  lcd.println("Atualizando o tempo...");

  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
}

void loop() {
  int contadorMqtt;
  int umi = 0;
  float temp = 0;
  /**
  Existem dois delays de 5 segundos, um para exibir a temperatura e umidade no display,
  e outro para a data e hora, como quero enviar a mensagem MQTT a cada 1 minuto coloquei
  esse for, para que após 60 segundo que é igual a 1 minuto, deixe chegar na parte do 
  código onde faz o envio da mensagem
  **/
  for (contadorMqtt = 0; contadorMqtt <= 6; contadorMqtt = contadorMqtt + 1) {
    /**valida a conexão com o broker/wi-fi,
    caso o tenha alguma falha o ESP32 é reiniciado, 
    para que tente realizar a conexão novamente
    **/
    if (!client.connected()) {
      if (client.connect("ESP32Client", mqttUser, mqttPassword)) {
        Serial.println("Conectado");
      } else {
        //caso fique sem conexão com o broker ele reinicia
        ESP.restart();
      }
    }
    umi = dht.readHumidity();
    temp = dht.readTemperature();
    //Aciona o buzzer caso os valores de temperatura ou umidade cheguem a níveis críticos
    if ((umi < minUmiPerigo) || (umi > maxUmiPerigo) || (temp < minTempPerigo) || (temp > maxTempPerigo)) {
      digitalWrite(PINO_BUZZER, HIGH);  // Ligar o buzzer
    } else {
      digitalWrite(PINO_BUZZER, LOW);  // Desligar o buzzer
    }
    lcd.setCursor(0, 0);
    lcd.print("Temp:    ");
    lcd.print(temp);
    lcd.write(0);
    lcd.print("C");
    lcd.setCursor(0, 1);
    lcd.print("Umidade:     ");
    lcd.print(umi);
    lcd.print("%");
    delay(5000);
    lcd.clear();
    printLocalTimeDisplay();
    delay(5000);
  }
  umi = dht.readHumidity();
  temp = dht.readTemperature();
  char* dataHora = printLocalTime();
  char msg[90];
  //Envia a mensagem MQTT no formato JSON, com a data e hora da leitura, a temperatura e a umidade
  snprintf(msg, 90, "{\"local\": \"%s\",\"dataHora\": \"%s\", \"temp\": %.2f, \"umi\": %d}", local, dataHora, temp, umi);
  client.publish("dca/sensores", msg);
  free(dataHora);  // Libera a memória que foi alocada
  client.loop();
}