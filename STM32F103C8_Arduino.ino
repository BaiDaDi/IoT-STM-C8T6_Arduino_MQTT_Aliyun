#include <Arduino.h>
#include <DHT11.h>

/*-------------------------------------------------*/
/*                  全局变量相关定义                */
/*-------------------------------------------------*/
#define mySerial Serial3

#define WAITTIME 80  //9600,若115200，80     //串口读取的延时

#define SERIAL_RXBUF_SIZE 100  //原来100
#define Params_SIZE 200
#define JsonBuf_SIZE 500

char Params[Params_SIZE];               //传感器数据长度，要足够长
char JsonBuf[JsonBuf_SIZE];             //JSON报文总长度，要足够长
char Serial_RX_BUF[SERIAL_RXBUF_SIZE];  //串口读取缓存区

unsigned long Serial_RX_Counter = 0;  //串口读取计数符
int PostMsgId = 0;                    //记录已经post了多少条

float Box_Temperature;  //子程序读温度，存储在全局变量中使用
char Box_Temperature_String[5];
float Box_Humidity;  //子程序读湿度，存储在全局变量中使用
char Box_Humidity_String[5];
int Box_ADCRead;  //子程序读ADC，存储在全局变量中使用
char Box_ADCRead_String[5];
char Box_Switch;
char BOX_LEDSwitch = 1;  //子程序与云平台共同控制，存储在全局变量中使用，初值给100避免出现01的矛盾


/*-------------------------------------------------*/
/*                  GPIO相关定义                    */
/*-------------------------------------------------*/
#define LED_PIN PC13
#define Light_Pin PA1  //PF8--Light Sensor未解决--用PA1外置
#define WIFI_Reset PB5

/*-------------------------------------------------*/
/*              库函数结构体相关定义                 */
/*-------------------------------------------------*/
DHT11 dht11(PA5);

/*-------------------------------------------------*/
/*                  WIFI相关定义                    */
/*-------------------------------------------------*/
const char SSID[] = "666";       //路由器名称
const char PASS[] = "66669999";  //路由器密码


/*-------------------------------------------------*/
/*              MQTT-阿里云相关定义                 */
/*-------------------------------------------------*/
#define USERNAME "DEV001&i8j2A4bJx6S"
#define PASSWARD "F374C0B161F33AFEB87AB6844A554E9DE83A0E5C"
#define CLIENTID "12345|securemode=3\\,signmethod=hmacsha1\\,timestamp=1692278795711|"  //这种定义方式要双倍的转义！！！
#define ALYUNURL "i8j2A4bJx6S.iot-as-mqtt.cn-shanghai.aliyuncs.com"


/*-------------------------------------------------*/
/*              JSON报文相关定义                    */
/*-------------------------------------------------*/
//这是post上传数据使用的模板    {\"id\":\"6\",\"params\": {\"LEDSwitch\":1\,\"Humidity\":15.15}}  有换行符会失败！！！（但是在arduino中不会出现这个问题，只怕你忘了加换行符）
#define ONENET_POST_BODY_FORMAT "{\\\"id\\\":\\\"%d\\\"\\\, \\\"params\\\":%s }"
//TOPIC定义
#define TOPIC_1_POST_EVENT "/sys/i8j2A4bJx6S/DEV001/thing/event/property/post"
#define TOPIC_1_POST_EVENT_REPLY "/sys/i8j2A4bJx6S/DEV001/thing/event/property/post_reply"
#define TOPIC_1_SET_EVENT "/sys/i8j2A4bJx6S/DEV001/thing/service/property/set"

/*-------------------------------------------------*/
/*              主函数开始循环                      */
/*-------------------------------------------------*/

void setup() {

  //串口初始化
  Serial.begin(9600);
  mySerial.begin(115200);          //注意！！！AT固件可是115200！！！！
  while (Serial.read() >= 0) {}    //clear serialport
  while (mySerial.read() >= 0) {}  //clear serialport

  //GPIO初始化
  pinMode(WIFI_Reset, OUTPUT);
  digitalWrite(WIFI_Reset, HIGH);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  pinMode(Light_Pin, INPUT_ANALOG);

  //连接阿里云，用while保证连接的成功
  while (MCU_Connect_IoTServer()) { delay(2000); };
  //阿里云对topic的订阅速度做了限制，topic失败重连的频率要放低
  while (MQTT_SUB_TOPIC(100, TOPIC_1_POST_EVENT_REPLY)) { delay(2000); };
}

void loop() {

  //   digitalWrite(LED_PIN, HIGH);  // turn the LED on (HIGH is the voltage level)
  //   delay(1000);                      // wait for a second
  //   digitalWrite(LED_PIN, LOW);   // turn the LED off by making the voltage LOW
  //   delay(1000);                      // wait for a second
  //

  DHT11();

  Box_ADCRead = analogRead(Light_Pin);
  //Serial.println(Box_ADCRead);

  MQTT_PUB_DATA();
  MQTT_Callback();

  delay(2000);
}



void DHT11() {

  float humidity = dht11.readHumidity();
  Box_Humidity = humidity;  //子函数读数据，数据进全局变量使用

  float temperature = dht11.readTemperature();
  Box_Temperature = temperature;  //子函数读数据，数据进全局变量使用

  if (temperature != -1 && humidity != -1) {
    Serial.print("Temp: ");
    Serial.print(temperature);
    Serial.println(" C");

    Serial.print("Humi: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.println("Error reading data");
  }

  // Wait for 2 seconds before the next reading.
  delay(2000);
}

/*-------------------------------------------------*/
/*函数名：WiFi复位 OK                                */
/*参  数：timeout：超时时间（100ms的倍数）             */
/*返回值：0：正确   其他：错误                         */
/*-------------------------------------------------*/
char WiFi_Reset(int timeout) {

  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  digitalWrite(WIFI_Reset, LOW);
  delay(500);
  digitalWrite(WIFI_Reset, HIGH);

  while (timeout--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);

    if (strstr(Serial_RX_BUF, "#######"))  //如果接收到reaady#########表示成功
    {
      break;  //主动跳出while循环
    }
  }

  if (timeout <= 0) return 1;  //如果timeout<=0，返回1

  return 0;
}

/*-------------------------------------------------*/
/*函数名：WiFi发送设置指令                         */
/*参  数：cmd：指令                                */
/*参  数：timeout：超时时间（100ms的倍数）         */
/*返回值：0：正确   其他：错误                     */
/*-------------------------------------------------*/
char WiFi_SendCmd(char *cmd, int timeout) {

  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  mySerial.println(cmd);  //发送指令注意发送\n

  while (timeout--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "OK"))  //如果接收到OK表示指令成功
      break;                          //主动跳出while循环
  }

  if (timeout <= 0) return 1;  //如果timeout<=0，说明超时时间到了，也没能收到OK，返回错误

  return 0;  //反之，表示正确，说明收到OK，通过break主动跳出while
}

/*-------------------------------------------------*/
/*函数名：WiFi加入路由器指令    OK                    */
/*参  数：timeout：超时时间（1s的倍数）               */
/*返回值：0：正确   其他：错误                        */
/*-------------------------------------------------*/
char WiFi_JoinAP(int timeout) {

  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  mySerial.print("AT+CWJAP=\"");  //发送指令
  mySerial.print(SSID);           //用户名
  mySerial.print("\",\"");        //发送指令
  mySerial.print(PASS);           //口令
  mySerial.print("\"");           //发送指令
  mySerial.println();             //注意发送\n

  while (timeout--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "WIFI GOT IP"))  //如果接收到WIFI GOT IP表示成功
      break;                                   //主动跳出while循环
  }

  if (timeout <= 0) return 1;  //超时时间到了，也没能收到，返回1

  return 0;  //正确，返回0
}

/*-------------------------------------------------*/
/*函数名：，mqtt连接                              */
//参考七步上网的最后三步，拆分后顺利上网
/*参  数：timeout： 超时时间（100ms的倍数）        */
/*返回值：0：正确  其他：错误                      */
/*-------------------------------------------------*/
char MQTT_Connect_NET(int timeout) {

  int Count_Time = 0;

  //-------------------------------------------------------------------------------------------分段1
  Count_Time = timeout;
  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  //   Serial.print("AT+MQTTUSERCFG=0,1,\"");  //发送指令
  //   Serial.print("\",\"");
  //   Serial.print(USERNAME);
  //   Serial.print("\",\"");
  //   Serial.print(PASSWARD);
  //   Serial.print("\",0,0,\"");
  //   Serial.println();

  mySerial.print("AT+MQTTUSERCFG=0,1,\"");  //发送指令
  mySerial.print("\",\"");
  mySerial.print(USERNAME);
  mySerial.print("\",\"");
  mySerial.print(PASSWARD);
  mySerial.print("\",0,0,\"");
  mySerial.println();  //注意发送\n

  while (Count_Time--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "OK"))
      break;
  }

  if (Count_Time <= 0) {
    Serial.println("第五步连接失败");  //串口输出信息
    //Serial.println(Serial_RX_BUF);     //串口输出信息
    return 5;  //如果timeout<=0，返回1
  }

  //---------------------------------------------------------------------------分段进行USER CONFIG
  Count_Time = timeout;
  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  //   Serial.print("AT+MQTTCLIENTID=0,\"");
  //   Serial.print(CLIENTID);
  //   Serial.print("\"");
  //   Serial.println();

  mySerial.print("AT+MQTTCLIENTID=0,\"");  //发送指令
  mySerial.print(CLIENTID);
  mySerial.print("\"");
  mySerial.println();  //注意发送\n

  while (Count_Time--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "OK"))  //如果接收到ok表示成功
      break;                          //主动跳出while循环
  }

  if (Count_Time <= 0) {
    Serial.println("第六步连接失败");  //串口输出信息
    //Serial.println(Serial_RX_BUF);     //串口输出信息
    return 5;  //如果timeout<=0，返回1
  }

  //---------------------------------------------------------------------------分段进行IoT连接
  Count_Time = timeout;
  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  //   Serial.print("AT+MQTTCONN=0,\"");  //发送指令
  //   Serial.print(ALYUNURL);
  //   Serial.print("\",1883,1");
  //   Serial.println();

  mySerial.print("AT+MQTTCONN=0,\"");  //发送指令
  mySerial.print(ALYUNURL);
  mySerial.print("\",1883,1");
  mySerial.println();  //注意发送\n

  while (Count_Time--) {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "OK"))  //如果接收到ok表示成功
      break;                          //主动跳出while循环
  }

  if (Count_Time <= 0) {
    Serial.println("第七步连接失败");  //串口输出信息
    //Serial.println(Serial_RX_BUF);     //串口输出信息
    return 5;  //如果timeout<=0，返回1
  }

  //--------------------------------------------------------------------------------------------------分段结束
  return 0;  //走到这里自然正确，返回0
}

/*-------------------------------------------------*/
/*函数名：连接阿里云                                */
/*参  数：无                                       */
/*返回值：0：正确   其他：错误                       */
/*-------------------------------------------------*/
char MCU_Connect_IoTServer(void) {

  //----1
  Serial.println("########################################");
  Serial.println("准备复位ESP8266");
  if (WiFi_Reset(50))  //复位，100ms超时单位，总计5s超时时间
  {
    Serial.println("ESP8266复位失败，准备重启");  //返回非0值，进入if
    return 1;                                     //返回1
  } else Serial.println("ESP8266复位成功");

  //----2
  Serial.println("########################################");
  Serial.println("准备设置STA模式");
  if (WiFi_SendCmd("AT+CWMODE=1", 100))  //设置STA模式，100ms超时单位，总计5s超时时间
  {
    Serial.println("设置STA模式失败，准备重启");  //返回非0值，进入if
    return 2;                                     //返回2
  } else Serial.println("设置STA模式成功");

  //----3
  Serial.println("########################################");
  Serial.println("准备连接时域和SNTP服务器");
  if (WiFi_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp1.aliyun.com\"", 50))  //准备连接域地址，100ms超时单位，总计5s超时时间
  {
    Serial.println("时域和SNTP服务器连接失败，准备重启");  //返回非0值，进入if
    return 3;                                              //返回3
  } else Serial.println("时域和SNTP服务器连接成功");

  //---4
  Serial.println("########################################");
  Serial.println("准备连接WIFI");
  if (WiFi_JoinAP(200))  //连接WIFI,0.1s超时单位，总计10s超时时间
  {
    Serial.println("连接WIFI失败，准备重启");  //返回非0值，进入if
    return 4;                                  //返回4
  } else Serial.println("连接WIFI成功");

  //---5
  Serial.println("########################################");
  Serial.println("准备连接阿里云");
  if (MQTT_Connect_NET(100))  //连接阿里云，100ms超时单位，总计10s超时时间
  {
    Serial.println("连接阿里云失败，准备重启");  //返回非0值，进入if
    return 5;                                    //返回10
  } else Serial.println("连接阿里云成功");

  return 0;  //正确返回0
}

/*-------------------------------------------------*/
/*函数名：订阅TOPIC    OK                    */
/*参  数：timeout：超时时间（1s的倍数）               */
/*返回值：0：正确   其他：错误                        
*/
/*-------------------------------------------------*/
char MQTT_SUB_TOPIC(int timeout, char *topic) {

  Serial_RX_Counter = 0;                        //WiFi接收数据量变量清零
  memset(Serial_RX_BUF, 0, SERIAL_RXBUF_SIZE);  //清空WiFi接收缓冲区

  Serial.println("########################################");
  Serial.println("准备订阅Topic：");

  Serial.print("AT+MQTTSUB=0,\"");  //发送指令
  Serial.print(topic);
  Serial.print("\",1");
  Serial.println();

  mySerial.print("AT+MQTTSUB=0,\"");  //发送指令
  mySerial.print(topic);
  mySerial.print("\",1");
  mySerial.println();  //注意发送\n

  while (timeout--)  //等待超时时间到0
  {
    while (mySerial.available()) {
      Serial_RX_BUF[Serial_RX_Counter++] = (char)mySerial.read();
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }
    delay(100);
    if (strstr(Serial_RX_BUF, "OK")) {
      Serial.println("订阅Topic成功");
      Serial.println("########################################");
      break;
    }
  }

  if (timeout <= 0) {
    Serial.println("订阅Topic失败");
    return 6;  //超时时间到了，也没能，返回1
  }

  return 0;  //正确，返回0
}

/*-------------------------------------------------*/
/*函数名：post：设备属性上报    OK                    */
/*参  数：timeout：超时时间（1s的倍数）               */
/*返回值：0：正确   其他：错误                        */
/*-------------------------------------------------*/
void MQTT_PUB_DATA() {

  char str1[25];
  memset(Params, 0, Params_SIZE);
  memset(JsonBuf, 0, JsonBuf_SIZE);
  while (mySerial.read() >= 0) {}  //清除串口缓存

  //把要上传的数据写在param里
  /*
    在Arduino IDE中，使用sprintf是对float无效的，可以使用dtostrf，强制将float转化为char，再用strcat()函数来实现对字符串的拼接。
    格式如下：

    char* dtostrf(double _val,signed char _width, unsigned char prec, char* _s)
    _val:要转换的float或者double值。
    _width:转换后整数部分长度。
    _prec：转换后小数部分长度。
    _s:保存到该char数组中。
  */
  dtostrf(Box_Humidity, 2, 2, Box_Humidity_String);        //将float转化为char字符串
  dtostrf(Box_Temperature, 2, 2, Box_Temperature_String);  //将float转化为char字符串
  sprintf(str1, " \\\, \\\"PPM\\\": %d", Box_ADCRead);

  sprintf(Params, "{ \\\"Humidity\\\": ");  //\\\"LEDSwitch\\\": %d \\\,    BOX_LEDSwitch
  strcat(Params, Box_Humidity_String);
  strcat(Params, " \\\, \\\"Temperature\\\": ");
  strcat(Params, Box_Temperature_String);
  strcat(Params, str1);
  strcat(Params, "}");

  PostMsgId += 1;
  //拼装json报文
  sprintf(JsonBuf, ONENET_POST_BODY_FORMAT, PostMsgId, Params);

  Serial.println("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
  Serial.println("准备上传设备属性：");

  Serial.print("AT+MQTTPUB=0,\"");
  Serial.print(TOPIC_1_POST_EVENT);
  Serial.print("\",\"");
  Serial.print(JsonBuf);
  Serial.print("\",1,0");
  Serial.println();
  Serial.println("--------------------------------------------------------------------------------");

  mySerial.print("AT+MQTTPUB=0,\"");
  mySerial.print(TOPIC_1_POST_EVENT);
  mySerial.print("\",\"");
  mySerial.print(JsonBuf);
  mySerial.print("\",1,0");
  mySerial.println();  //注意发送\n
}

/*-------------------------------------------------*/
/*函数名：回调函数                                  */
/*在轮询中监控串口的数据                            */
/*-------------------------------------------------*/
void MQTT_Callback() {

  char timeout = 200;
  String Serial_RX_String = "";

  while (timeout--) {
    while (mySerial.available()) {
      Serial_RX_String.concat(mySerial.readString());  //字符串叠加，虽然采用string，第一次读到的东西还是不可控的，好在会连发三条回复，后面的都是准的
      //Serial_RX_String = mySerial.readStringUntil('\n');
      delayMicroseconds(WAITTIME);  //拖时间，读完整的buffer
    }

    delay(100);
    //Serial_RX_String.trim();

    if (Serial_RX_String.indexOf(TOPIC_1_SET_EVENT) != -1) {

      if (Serial_RX_String.indexOf("\"LEDSwitch\":1") != -1) {
        Serial.println(">666");
        digitalWrite(LED_PIN, LOW);  // turn the LED on (HIGH is the voltage level)
      }

      if (Serial_RX_String.indexOf("\"LEDSwitch\":0") != -1) {
        Serial.println(">555");
        digitalWrite(LED_PIN, HIGH);  // turn the LED on (HIGH is the voltage level)
      }

      Serial.println("下传设备属性来了！！！:");
      Serial.println(Serial_RX_String);
      Serial.println("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");

      break;
    }

    if (Serial_RX_String.indexOf(TOPIC_1_POST_EVENT_REPLY) != -1) {

      Serial.println("上传设备属性成功:");
      Serial.println(Serial_RX_String);
      Serial.println("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");

      break;
    }
  }
}
