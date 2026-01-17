#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h> // Biblioteca pentru senzorul de temperatura

// ==========================================
// 1. CONFIGURARI RETEA SI CLOUD
// ==========================================
const char* ssid = "Wokwi-GUEST"; // Reteaua WiFi din simulator
const char* password = "";

// Datele de conectare la HiveMQ Cloud
const char* mqtt_server = "61815f8bf3ab49909709c305f6e44aee.s1.eu.hivemq.cloud"; 
const int mqtt_port = 8883;        // Portul pentru conexiune securizata (SSL)
const char* mqtt_user = "Student"; // Userul creat in HiveMQ
const char* mqtt_pass = "Napster10";

// Topicuri MQTT (Canalele de comunicare)
const char* topic_status = "proiect/parcare/status";  // Aici trimitem mesaje generale
const char* topic_locuri = "proiect/parcare/locuri";  // Aici trimitem nr de locuri
const char* topic_comanda = "proiect/parcare/cmd";    // Aici ASCULTAM comenzi (OPEN/CLOSE)

// Clientii pentru retea
WiFiClientSecure espClient;
PubSubClient client(espClient);

// ==========================================
// 2. CONFIGURARI HARDWARE (PINI)
// ==========================================
LiquidCrystal_I2C lcd(0x27, 16, 2); // Ecran LCD la adresa 0x27
Servo gate;                         // Servomotorul pentru bariera
DHT dht(15, DHT22);                 // Senzor temperatura pe Pin 15

// Pini Senzori Ultrasonici
const int TRIG_IN = 13; const int ECHO_IN = 12;   // Senzor Intrare
const int TRIG_OUT = 14; const int ECHO_OUT = 27; // Senzor Iesire

// Pini componente auxiliare
const int SERVO_PIN = 26;
const int PIN_VERDE = 33;       // LED Verde (Liber)
const int PIN_ROSU = 32;        // LED Rosu (Ocupat/Alarma)
const int PIN_STREET_LIGHT = 25;// Iluminat stradal
const int PIN_LDR = 34;         // Senzor de lumina (Fotorezistenta)

// ==========================================
// 3. VARIABILE DE STARE (MEMORIA SISTEMULUI)
// ==========================================
const int TOTAL_LOCURI = 4;     // Capacitatea maxima a parcarii
int freeSlots = 4;              // Locuri libere curente
int oldSlots = -1;              // Folosit pentru a nu updata LCD-ul inutil
bool isEmergency = false;       // Devine 'true' cand e incendiu
bool manualOverride = false;    // Devine 'true' cand deschidem din Dashboard

// ==========================================
// 4. FUNCTII AJUTATOARE
// ==========================================

// Functie care masoara distanta cu senzorul ultrasonic (in cm)
long getDistance(int trig, int echo) {
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10); // Trimitem impuls sonic
  digitalWrite(trig, LOW);
  long d = pulseIn(echo, HIGH, 30000); // Masuram cat dureaza ecoul
  return (d == 0) ? 999 : (d * 0.034 / 2); // Calculam distanta
}

// Functie apelata AUTOMAT cand primim un mesaj de pe HiveMQ (Dashboard)
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Convertim mesajul primit din format byte in String citibil
  String mesaj = "";
  for (int i = 0; i < length; i++) {
    mesaj += (char)payload[i];
  }
  Serial.print("Comanda primita: "); Serial.println(mesaj);

  // Verificam ce comanda am primit
  if (mesaj == "OPEN") {
    manualOverride = true; // Activam modul manual
    gate.write(0);         // Deschidem bariera fortat
    lcd.clear(); lcd.print("COMANDA REMOTE:"); 
    lcd.setCursor(0,1); lcd.print("Bariera DESCHISA");
  } 
  else if (mesaj == "CLOSE") {
    manualOverride = false; // Dezactivam modul manual
    gate.write(90);         // Inchidem bariera (revine la automat)
    oldSlots = -1;          // Fortam reimprospatarea ecranului LCD
  }
}

// Functie pentru conectarea initiala la WiFi
void setup_wifi() {
  Serial.print("Conectare la WiFi...");
  WiFi.begin(ssid, password);
  // Asteptam pana cand routerul ne da IP
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Conectat!");
}

// Functie care reconecteaza la HiveMQ daca pica netul
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectare HiveMQ...");
    // Generam un ID unic random ca sa nu ne respinga serverul
    String clientId = "ESP32-Parcare-" + String(random(0xffff), HEX);
    
    // Incercam conectarea cu user si parola
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" OK!");
      client.publish(topic_status, "ONLINE - SYSTEM READY"); // Anuntam ca suntem vii
      
      // Ne abonam sa ascultam comenzi pe topicul 'cmd'
      client.subscribe(topic_comanda);
      Serial.println("Ascult comenzi pe topicul CMD...");
    } else {
      Serial.print("Eroare rc="); Serial.print(client.state());
      delay(5000); // Mai incercam peste 5 secunde
    }
  }
}

// ==========================================
// 5. SETUP (RULEAZA O SINGURA DATA LA PORNIRE)
// ==========================================
void setup() {
  Serial.begin(115200); // Pornim consola seriala pentru debug
  
  // Configuram directia pinilor (Intrare/Iesire)
  pinMode(TRIG_IN, OUTPUT); pinMode(ECHO_IN, INPUT);
  pinMode(TRIG_OUT, OUTPUT); pinMode(ECHO_OUT, INPUT);
  pinMode(PIN_VERDE, OUTPUT); pinMode(PIN_ROSU, OUTPUT);
  pinMode(PIN_STREET_LIGHT, OUTPUT);
  pinMode(PIN_LDR, INPUT);

  // Initializam ecranul LCD si Servo
  Wire.begin(21, 22); 
  lcd.init(); lcd.backlight();
  gate.attach(SERVO_PIN); gate.write(90); // Bariera porneste inchisa (90 grade)
  dht.begin(); // Pornim senzorul de temperatura

  // Conectare la Internet si Cloud
  setup_wifi();
  espClient.setInsecure(); // Truc pentru a ignora verificarea certificatului SSL (doar pt simulare)
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback); // Spunem sistemului ce functie sa apeleze cand vine un mesaj
  
  lcd.setCursor(0,0); lcd.print("Sistem Pornit");
  delay(2000);
}

// ==========================================
// 6. LOOP (RULEAZA LA INFINIT)
// ==========================================
void loop() {
  // Verificam conexiunea la internet
  if (!client.connected()) reconnect();
  client.loop(); // Functie vitala: proceseaza pachetele MQTT (inclusiv comenzile primite)

  // --- A. SISTEM DE URGENTA (INCENDIU) ---
  float t = dht.readTemperature(); // Citim temperatura
  
  if (t > 45) { // Daca e canicula/foc (> 45 grade)
    if (!isEmergency) {
      isEmergency = true; // Activam starea de urgenta
      client.publish(topic_status, "ALERTA: INCENDIU! Bariera Deschisa!");
    }
    
    // Ridicam bariera permanent pentru evacuare
    gate.write(0); 
    
    // Afisam ALERTA pe ecran
    lcd.clear(); 
    lcd.print("! FOC - ALARMA !");
    lcd.setCursor(0,1); lcd.print("Temp: " + String(t,1) + "C");
    
    // Facem LED-urile sa clipeasca (efect de sirena vizuala)
    digitalWrite(PIN_VERDE, HIGH); digitalWrite(PIN_ROSU, HIGH);
    delay(200);
    digitalWrite(PIN_VERDE, LOW); digitalWrite(PIN_ROSU, LOW);
    delay(200);
    
    return; // OPRIM TOT RESTUL CODULUI (nu mai verificam masini, ramanem in alerta)
  } else {
    // Daca temperatura revine la normal
    if (isEmergency) {
      isEmergency = false; // Dezactivam urgenta
      gate.write(90);      // Inchidem bariera la loc
      oldSlots = -1;       // Resetam ecranul
      client.publish(topic_status, "Incendiu stins. Revenire la normal.");
    }
  }

  // --- B. CONTROL MANUAL DIN DASHBOARD ---
  if (manualOverride) {
    // Daca am primit comanda "OPEN", ignoram senzorii si doar clipim LED-ul verde
    // Semn ca sistemul este in mod "Manual"
    digitalWrite(PIN_VERDE, !digitalRead(PIN_VERDE)); // Blink lent
    delay(500);
    return; // Nu trecem mai departe la logica automata
  }

  // --- C. LOGICA AUTOMATA (SMART PARKING) ---
  
  // 1. Control Iluminat Stradal (Fotorezistenta)
  // Daca e intuneric (< 2000), aprindem becul
  if (analogRead(PIN_LDR) < 2000) digitalWrite(PIN_STREET_LIGHT, HIGH);
  else digitalWrite(PIN_STREET_LIGHT, LOW);

  // 2. Senzor INTRARE
  long dIn = getDistance(TRIG_IN, ECHO_IN);
  
  // Daca vedem o masina SI mai avem locuri libere
  if (dIn < 10 && freeSlots > 0) {
    lcd.clear(); lcd.print("Intrare...");
    
    // Deschidem bariera si schimbam LED-urile
    digitalWrite(PIN_VERDE, LOW); digitalWrite(PIN_ROSU, HIGH);
    gate.write(0); // 0 grade = Deschis
    freeSlots--;   // Scadem un loc
    
    // Trimitem noul status in Cloud
    if (freeSlots == 0) client.publish(topic_locuri, "Parking Full");
    else { 
      String m = "Empty spaces " + String(freeSlots); 
      client.publish(topic_locuri, m.c_str()); 
    }
    
    // PROTECTIE (Safety Loop):
    // Cat timp masina e inca sub bariera (< 20cm), NU inchidem!
    // Asteptam aici pana masina pleaca.
    while(getDistance(TRIG_IN, ECHO_IN) < 20) delay(100);
    
    // Dupa ce a plecat, asteptam 1 secunda si inchidem
    delay(1000); 
    gate.write(90); // 90 grade = Inchis
    oldSlots = -1;  // Cerem update la ecran
  }

  // 3. Senzor IESIRE
  long dOut = getDistance(TRIG_OUT, ECHO_OUT);
  
  // Daca vedem o masina la iesire SI parcarea nu e goala (logic nu ar trebui sa fie)
  if (dOut < 10 && freeSlots < TOTAL_LOCURI) {
    lcd.clear(); lcd.print("Iesire...");
    
    digitalWrite(PIN_VERDE, LOW); digitalWrite(PIN_ROSU, HIGH);
    gate.write(0); // Deschidem
    freeSlots++;   // Eliberam un loc
    
    // Trimitem status in Cloud
    String m = "Empty spaces " + String(freeSlots); 
    client.publish(topic_locuri, m.c_str());

    // PROTECTIE (Safety Loop) la iesire
    while(getDistance(TRIG_OUT, ECHO_OUT) < 20) delay(100);
    
    delay(1000); 
    gate.write(90); // Inchidem
    oldSlots = -1;
  }

  // 4. Actualizare Ecran LCD (Idle Mode)
  // Facem update doar daca s-a schimbat numarul de locuri (sa nu palpaie ecranul)
  if (freeSlots != oldSlots) {
    lcd.clear();
    if (freeSlots == 0) {
      lcd.print(" PARCARE PLINA ");
      digitalWrite(PIN_VERDE, LOW); digitalWrite(PIN_ROSU, HIGH);
    } else {
      lcd.print(" Smart Parking ");
      digitalWrite(PIN_VERDE, HIGH); digitalWrite(PIN_ROSU, LOW);
    }
    // Afisam locurile pe randul 2 si temperatura curenta
    lcd.setCursor(0,1); lcd.print("Locuri: "); lcd.print(freeSlots);
    lcd.print(" T:"); lcd.print((int)t); lcd.print("C");
    oldSlots = freeSlots;
  }
  
  delay(100); // O mica pauza pentru stabilitate
}
