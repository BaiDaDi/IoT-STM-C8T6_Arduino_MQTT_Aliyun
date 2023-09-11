# STM_Arduino_ESP_MQTT_Aliyun
一种基于ESP8266和MQTT连接阿里云的STM32F103C8T6的arduino的解决方案

总体设计是一个非常逻辑的思路

Step1：阿里云注册，创建产品，得到三元组  
Step2：三元组计算密码（阿里云自带工具，sha1）  
Step3：MQTT.fx测试三元组的可用，topic和JSON的格式  
Step4：ESP8266，AT指令，七步上网，参考TXT  
Step5：MCU与ESP通讯，关键在于转义字符，这里测了很多次。其次是程序架构  
    串口使用arduino的ReadString函数，效果还行但是第一个流只能读一般  
    架构上Pub与callback分离，pub在循环中发送，曾经使用Timer，调用失败  
    pub和callback放在loop里面，循环着读，也能凑合用吧  
    另外callback的分析没用ArduinoJSON库，使用了一个取巧的办法，一个demo也就凑合吧  
