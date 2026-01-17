import paho.mqtt.client as mqtt
import csv
import datetime
import time
import ssl

# --- CONFIGURARE HIVEMQ (Aceleasi ca in ESP32) ---
BROKER = "61815f8bf3ab49909709c305f6e44aee.s1.eu.hivemq.cloud"
PORT = 8883
USER = "Student"
PASS = "Napster10"

# Fisierul unde salvam datele (Baza de Date)
DB_FILE = "parcare_history.csv"

# --- CREAREA FISIERULUI SI A CAPULUI DE TABEL ---
# Daca fisierul nu exista, il cream si scriem prima linie (coloanele)
try:
    with open(DB_FILE, 'x', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["ID", "Data_Ora", "Topic", "Mesaj", "Eveniment"])
        print(f"[INFO] Am creat baza de date: {DB_FILE}")
except FileExistsError:
    print(f"[INFO] Baza de date {DB_FILE} exista deja. Voi adauga date noi.")

# --- FUNCTII MQTT ---

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("[CONNECTED] Conectat cu succes la HiveMQ Cloud!")
        # Ne abonam la TOATE mesajele din proiect (# inseamna tot)
        client.subscribe("proiect/parcare/#")
    else:
        print(f"[ERROR] Conexiune esuata. Cod eroare: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode("utf-8")
        topic = msg.topic
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        print(f"[{timestamp}] {topic}: {payload}")
        
        # Interpretam evenimentul pentru a fi mai clar in tabel
        eveniment = "Status Update"
        if "Parking Full" in payload:
            eveniment = "PARCARE PLINA"
        elif "Empty spaces" in payload:
            eveniment = "Schimbare Locuri"
        elif "ALERTA" in payload:
            eveniment = "INCENDIU/URGENTA"
        elif "OPEN" in payload or "CLOSE" in payload:
            eveniment = "Comanda Remote"

        # SALVAM IN FISIER (Insert into Database)
        with open(DB_FILE, 'a', newline='') as f:
            writer = csv.writer(f)
            # Scriem un rand nou
            writer.writerow([time.time(), timestamp, topic, payload, eveniment])
            
    except Exception as e:
        print(f"[ERROR] Nu am putut salva datele: {e}")

# --- SETUP CLIENT ---
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.username_pw_set(USER, PASS)

# HiveMQ Cloud cere criptare SSL/TLS
client.tls_set(cert_reqs=ssl.CERT_NONE, tls_version=ssl.PROTOCOL_TLS)

client.on_connect = on_connect
client.on_message = on_message

print("--- PORNIRE CLIENT BAZA DE DATE ---")
print("Astept date de la parcare...")

try:
    client.connect(BROKER, PORT, 60)
    client.loop_forever() # Ruleaza la infinit pana il opresti tu
except KeyboardInterrupt:
    print("\n[STOP] Oprire script.")
