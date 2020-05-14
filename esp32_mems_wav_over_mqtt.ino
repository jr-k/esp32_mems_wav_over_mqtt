#include <driver/i2s.h>
#include <WiFi.h>
#include <SPI.h>
#include "PubSubClient.h"

#define I2S_WS 15
#define I2S_SD 13
#define I2S_SCK 2
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE   (16000)
#define I2S_SAMPLE_BITS   (16)
#define I2S_READ_LEN      (16 * 1024)
#define RECORD_TIME       (.016) //Seconds
#define I2S_CHANNEL_NUM   (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)

#define BYTE_PER_BLOCK   (I2S_CHANNEL_NUM * I2S_SAMPLE_BITS/8)
#define BYTE_PER_SEC      (I2S_SAMPLE_RATE * BYTE_PER_BLOCK)
  
const int headerSize = 60;
bool isWIFIConnected;

int chunkNumber = 0;
long randomid = random(0,2147483647);

const int messageSize = headerSize + FLASH_RECORD_SIZE;
int lastMessagePosition = headerSize;
byte message[messageSize];
long lastReconnectAttempt = 0;

WiFiClient espClient;
PubSubClient client(espClient);
const char* mqtt_server = "localhost";
const int mqtt_port = 1883;
const char* mqtt_username = "USER";
const char* mqtt_password = "PWD";
const char* ssid = "SSID";
const char* password = "WPAKEY";
  
void setup() {
  // put your setup code here, to run once:
  //Serial.begin(115200);
  //Serial.println("Message size...");
  //Serial.println(messageSize);

  client.setServer(mqtt_server, mqtt_port);

  messageInit(false);
  i2sInit();
  xTaskCreate(i2s_adc, "i2s_adc", 1024 * 2, NULL, 1, NULL);
  delay(500);
  xTaskCreate(wifiConnect, "wifi_Connect", 4096, NULL, 0, NULL);
}

void loop() {
  // put your main code here, to run repeatedly:
}

void i2sInit(){
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = 0,
    .dma_buf_count = 64,
    .dma_buf_len = 1024,
    .use_apll = 1
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,
    .data_in_num = I2S_SD
  };

  i2s_set_pin(I2S_PORT, &pin_config);
}


void i2s_adc_data_scale(uint8_t * d_buff, uint8_t* s_buff, uint32_t len)
{
    uint32_t j = 0;
    uint32_t dac_value = 0;
    for (int i = 0; i < len; i += 2) {
        dac_value = ((((uint16_t) (s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
        d_buff[j++] = 0;
        d_buff[j++] = dac_value * 256 / 2048;
    }
}

void messageInit(bool send) {
    wavHeader(message, FLASH_RECORD_SIZE);
    lastMessagePosition = headerSize;

    if (send && isWIFIConnected && client.connected()) {        
        //client.publish("hermes/audioServer/bureau/audioFrame", message, messageSize);
          
        /*/
        randomid = random(0,2147483647);
        char *topic = "hermes/audioServer/bureau/playBytes/p%ld";
        int len = 100;
        char bigBuf[len];
        sprintf(bigBuf, topic, randomid);
        client.publish(bigBuf, message, messageSize);
        chunkNumber++;
        //*/  
        
        //*/
        char *topic = "hermes/audioServer/bureau/playBytesStreaming/ps%ld/%d/%d";
        int len = 100;
        char bigBuf[len];
        sprintf(bigBuf, topic, randomid, chunkNumber, 0 /*isLastChunk*/);
        client.publish(bigBuf, message, messageSize);
        chunkNumber++;
        //*/   
    }
}

void i2s_adc(void *arg)
{
    int i2s_read_len = I2S_READ_LEN;
    size_t bytes_read;

    char* i2s_read_buff = (char*) calloc(i2s_read_len, sizeof(char));
    uint8_t* flash_write_buff = (uint8_t*) calloc(i2s_read_len, sizeof(char));

    i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    
    while (true) {
        //read data from I2S bus, in this case, from ADC.
        i2s_read(I2S_PORT, (void*) i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
        //save original data from I2S(ADC) into flash.
        i2s_adc_data_scale(flash_write_buff, (uint8_t*)i2s_read_buff, i2s_read_len);

        for (int i = 0; i < i2s_read_len; i++) {
             message[lastMessagePosition] = flash_write_buff[i];
             lastMessagePosition++;

             if (lastMessagePosition >= messageSize - 1) {
                 messageInit(true);
             }
        }
    }
}



void wavHeader(byte* h, int wavSize){
  int x = 0;
 

  // ======================
  // FileTypeBlocID
  h[x++] = 'R';
  h[x++] = 'I';
  h[x++] = 'F';
  h[x++] = 'F';

  // FileSize
  unsigned int fileSize = wavSize + headerSize - 8;
  h[x++] = (byte)(fileSize & 0xFF);
  h[x++] = (byte)((fileSize >> 8) & 0xFF);
  h[x++] = (byte)((fileSize >> 16) & 0xFF);
  h[x++] = (byte)((fileSize >> 24) & 0xFF);

  // FileFormatID
  h[x++] = 'W';
  h[x++] = 'A';
  h[x++] = 'V';
  h[x++] = 'E';

  // ======================
  // FormatBlockID
  h[x++] = 'f';
  h[x++] = 'm';
  h[x++] = 't';
  h[x++] = ' ';

  // BlockSize : 16
  h[x++] = 0x10;
  h[x++] = 0x00;
  h[x++] = 0x00;
  h[x++] = 0x00;

  // ----------------------
  // AudioFormat : 1 (PCM)
  h[x++] = 0x01;
  h[x++] = 0x00;

  // ChannelCount : 1
  h[x++] = 0x01;
  h[x++] = 0x00;
  
  // Frequency : 16 000 (0x3e80)
  /*
  h[x++] = 0x80;
  h[x++] = 0x3E;
  h[x++] = 0x00;
  h[x++] = 0x00;
  */
  h[x++] = (byte)(I2S_SAMPLE_RATE & 0xFF);
  h[x++] = (byte)((I2S_SAMPLE_RATE >> 8) & 0xFF);
  h[x++] = (byte)((I2S_SAMPLE_RATE >> 16) & 0xFF);
  h[x++] = (byte)((I2S_SAMPLE_RATE >> 24) & 0xFF);

  // BytePerSec : 32000 (0x7d00)
  /*
  h[x++] = 0x00;
  h[x++] = 0x7D;
  h[x++] = 0x00;
  h[x++] = 0x00;
  */
  h[x++] = (byte)(BYTE_PER_SEC & 0xFF);
  h[x++] = (byte)((BYTE_PER_SEC >> 8) & 0xFF);
  h[x++] = (byte)((BYTE_PER_SEC >> 16) & 0xFF);
  h[x++] = (byte)((BYTE_PER_SEC >> 24) & 0xFF);

  // BytePerBloc : 2
  /*
  h[x++] = 0x02;
  h[x++] = 0x00;
  */
  h[x++] = (byte)(BYTE_PER_BLOCK & 0xFF);
  h[x++] = (byte)((BYTE_PER_BLOCK >> 8) & 0xFF);

  // BitsPerSample : 16
  /*
  h[x++] = 0x10;
  h[x++] = 0x00;
  */
  h[x++] = (byte)(I2S_SAMPLE_BITS & 0xFF);
  h[x++] = (byte)((I2S_SAMPLE_BITS >> 8) & 0xFF);

  // ======================
  // TimeFormatId
  h[x++] = 't';
  h[x++] = 'i';
  h[x++] = 'm';
  h[x++] = 'e';
  h[x++] = 0x08;
  h[x++] = 0x00;
  h[x++] = 0x00;
  h[x++] = 0x00;
  h[x++] = 0x62;
  h[x++] = 0xE9;
  h[x++] = 0x07;
  h[x++] = 0x14;
  h[x++] = 0x72;
  h[x++] = 0x01;
  h[x++] = 0x00;
  h[x++] = 0x00;

  // ======================
  // DataBlockID
  h[x++] = 'd';
  h[x++] = 'a';
  h[x++] = 't';
  h[x++] = 'a';

  // DataSize
  h[x++] = (byte)(wavSize & 0xFF);
  h[x++] = (byte)((wavSize >> 8) & 0xFF);
  h[x++] = (byte)((wavSize >> 16) & 0xFF);
  h[x++] = (byte)((wavSize >> 24) & 0xFF);
}

boolean reconnect() {
  //Serial.println("try reconnect");
  
  if (client.connect("esp32-client", mqtt_username, mqtt_password)) {
    client.publish("esp32/status","START");
  }
  return client.connected();
}


void wifiConnect(void *pvParameters){
  isWIFIConnected = false;

  WiFi.begin(ssid, password);
  
  while(WiFi.status() != WL_CONNECTED){
    vTaskDelay(500);
    //Serial.print(".");
  }
  
  isWIFIConnected = true;

  while(true){
    vTaskDelay(1000);
    if (!client.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt > 5000) { // Try to reconnect.
        lastReconnectAttempt = now;
        if (reconnect()) { // Attempt to reconnect.
          lastReconnectAttempt = 0;
        }
      }
    }
  } 
}
