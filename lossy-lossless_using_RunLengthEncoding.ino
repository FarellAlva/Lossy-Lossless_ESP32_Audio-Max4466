#include <WiFi.h>
#include <WebServer.h>


const char* ssid     = "AcerN58";     
const char* password = "farellpunya";  


#define MIC_PIN 34       // Pin Analog
#define SAMPLE_RATE 8000 // 8 kHz 
#define DURATION 2       // 2 Detik
#define NUM_SAMPLES (SAMPLE_RATE * DURATION)

// --- KONFIGURASI REDAM NOISE ---
#define NOISE_THRESHOLD 400 // Jika suara di bawah nilai ini, akan dianggap hening (0)


int16_t bufferOriginal[NUM_SAMPLES]; // (Lossless/RLE)
int16_t bufferLossy[NUM_SAMPLES];    //  (Lossy 8-bit)

WebServer server(80);



//  Efek Lossy (Memangkas kualitas 16-bit -> 8-bit)
void generateLossyAudio() {
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int16_t original = bufferOriginal[i];
    
    // Geser 8 bit 
    int16_t compressed = (original >> 8); 
    
    // DEKOMPRESI, Kembalikan posisi bit 
    bufferLossy[i] = (compressed << 8); 
  }
}

//  Hitung Ukuran RLE (Untuk Laporan Teks)
int calculateRLESize() {
  int size = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int16_t val = bufferOriginal[i];
    int run = 1;
    // Cek angka kembar berurutan
    while ((i + 1 < NUM_SAMPLES) && (bufferOriginal[i+1] == val) && (run < 255)) {
      run++; i++;
    }
    size += 3; // 3 byte per paket RLE
  }
  return size;
}


void sendWAV(int16_t* dataBuffer) {
  byte header[44];
  int totalDataLen = NUM_SAMPLES * 2;
  int fileSize = totalDataLen + 36;
  int sampleRate = SAMPLE_RATE;
  
  // Header WAV Standar
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = (byte)(fileSize & 0xFF); header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF); header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0; header[22] = 1; header[23] = 0;
  header[24] = (byte)(sampleRate & 0xFF); header[25] = (byte)((sampleRate >> 8) & 0xFF);
  header[26] = (byte)((sampleRate >> 16) & 0xFF); header[27] = (byte)((sampleRate >> 24) & 0xFF);
  header[28] = (byte)((sampleRate * 2) & 0xFF); header[29] = (byte)(((sampleRate * 2) >> 8) & 0xFF);
  header[30] = (byte)(((sampleRate * 2) >> 16) & 0xFF); header[31] = (byte)(((sampleRate * 2) >> 24) & 0xFF);
  header[32] = 2; header[33] = 0; header[34] = 16; header[35] = 0;
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (byte)(totalDataLen & 0xFF); header[41] = (byte)((totalDataLen >> 8) & 0xFF);
  header[42] = (byte)((totalDataLen >> 16) & 0xFF); header[43] = (byte)((totalDataLen >> 24) & 0xFF);

  String filename = "audio.wav";
  server.sendHeader("Content-Type", "audio/wav");
  server.sendHeader("Content-Disposition", "inline; filename=\"" + filename + "\"");
  server.sendHeader("Connection", "close");
  
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: audio/wav");
  client.println("Connection: close");
  client.println("Content-Length: " + String(totalDataLen + 44));
  client.println();
  client.write(header, 44);
  client.write((byte*)dataBuffer, totalDataLen);
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Audio</title>
  <style>
    body { font-family: sans-serif; background: #1a1a1a; color: #fff; text-align: center; padding: 20px; }
    h2 { color: #f1c40f; }
    .box { background: #333; padding: 20px; margin: 15px auto; border-radius: 10px; max-width: 400px; }
    button { padding: 15px 30px; font-size: 18px; background: #e74c3c; color: white; border: none; border-radius: 5px; cursor: pointer; margin-bottom: 20px; }
    button:active { transform: scale(0.95); }
    audio { width: 100%; margin-top: 10px; }
    label { font-weight: bold; color: #3498db; display: block; margin-bottom: 5px; }
    .stats { font-size: 0.9em; color: #aaa; text-align: left; font-family: monospace; border-top: 1px solid #555; padding-top: 10px; margin-top: 10px;}
  </style>
</head>
<body>
  <h2> Audio Compression Test (2 Detik)</h2>
  
  
  <button onclick="record()">🔴 REKAM 2 DETIK</button>
  <div id="status">Status: Siap...</div>

  <div class="box" id="resultBox" style="display:none;">
    <label>1. AUDIO LOSSLESS (Original)</label>
    <small>Kualitas Penuh (Mewakili RLE)</small>
    <audio id="playerLossless" controls></audio>
    <br><br>
    
    <label>2. AUDIO LOSSY (8-bit)</label>
    <small>Kualitas Turun (Kresek/Noise)</small>
    <audio id="playerLossy" controls></audio>
    
    <div class="stats" id="statsReport"></div>
  </div>

<script>
function record() {
  document.getElementById("status").innerHTML = "🎙️ Merekam 2 detik... (Bicara!)";
  
  fetch('/record').then(response => response.text()).then(data => {
    document.getElementById("status").innerHTML = "✅ Selesai! Silakan Play dibawah.";
    document.getElementById("resultBox").style.display = "block";
    
    // Update Sumber Audio (+timestamp agar refresh)
    let t = new Date().getTime();
    document.getElementById("playerLossless").src = "/audio_original?t=" + t;
    document.getElementById("playerLossy").src = "/audio_lossy?t=" + t;
    
    // Tampilkan Data Laporan
    document.getElementById("statsReport").innerHTML = data;
  });
}
</script>
</body>
</html>
)rawliteral";

// --- URL HANDLERS ---
void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleRecord() {
  Serial.println("Merekam 2 Detik...");
  
  // 1. REKAM DATA
  unsigned long interval = 1000000 / SAMPLE_RATE;
  unsigned long prev = micros();
  
  for(int i=0; i<NUM_SAMPLES; i++) {
    while(micros() - prev < interval);
    prev = micros();
    
    // BACA PIN 34
    // Normalisasi: (Raw - 1900) * Gain
    // Sesuaikan 1900 jika grafik terlalu atas/bawah
    int raw = analogRead(MIC_PIN); 
    
    // --- REDAM NOISE (Noise Gate) ---
    int16_t sample = (raw - 1900) * 10;
    
    // Jika nilainya kecil (cuma noise/desis), paksa jadi 0
    if (abs(sample) < NOISE_THRESHOLD) {
      sample = 0;
    }
    
    bufferOriginal[i] = sample; 
  }

  // 2. PROSES DATA
  generateLossyAudio(); // Bikin versi kualitas rendah
  
  // 3. HITUNG STATISTIK (Untuk Laporan di Web)
  int sizeOri = NUM_SAMPLES * 2;
  int sizeRLE = calculateRLESize();
  int sizeLossy = NUM_SAMPLES * 1; // 8-bit = 1 byte per sampel

  String report = "<b>LAPORAN STATISTIK:</b><br>";
  report += "Durasi: " + String(DURATION) + " Detik<br>";
  report += "Ukuran Asli: " + String(sizeOri) + " bytes<br>";
  report += "Ukuran Lossy (8-bit): " + String(sizeLossy) + " bytes (Hemat 50%)<br>";
  report += "Ukuran RLE: " + String(sizeRLE) + " bytes<br>";
  
  if(sizeRLE > sizeOri) report += "<span style='color:red'>-> Status RLE: GAGAL (File Membesar)</span><br>";
  else report += "<span style='color:green'>-> Status RLE: BERHASIL</span><br>";

  server.send(200, "text/html", report);
}

void handleAudioOriginal() {
  sendWAV(bufferOriginal);
}

void handleAudioLossy() {
  sendWAV(bufferLossy);
}

void setup() {
  Serial.begin(115200);
  
  // Setting Pin 34 sebagai Input
  pinMode(MIC_PIN, INPUT);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected!");
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());
  
  server.on("/", handleRoot);
  server.on("/record", handleRecord);
  server.on("/audio_original", handleAudioOriginal);
  server.on("/audio_lossy", handleAudioLossy);
  server.begin();
}

void loop() {
  server.handleClient();
}