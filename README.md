## Introducción
Este proyecto consiste en la monitorización y automatización de sistemas de climatización usando LoRaWAN. Para ello se han utilizado los siguientes componentes:
- Una placa TTGO LoRa32 v1 con un sensor SHT21 conectado a los pines SDA y SCL (conexión I2C) y un relé SRD-05VDC-SL-C conectado al pin 13. La placa TTGO contiene una antena ombidireccional conectada a un trasceptor SX1276 que funciona a 868 y 915 MHz. En este proyecto se usa la banda de 868 MHz, puesto que es libre en Europa. Además, la placa contiene un microcontrolador ESP32.
- Una modificación en el archivo lmic.c de la librería MCCI para la comunicación en Clase C de LoRaWAN. 
- Un programa en Arduino para la lectura del sensor cada T1 segundos, envío de las medidas cada T2 segundos, recepción de órdenes y encender o apagar el relé.
- Una configuración en The Things Stack usando su consola The Things Stack Community Edition. Esta configuración contiene una aplicación y un dispositivo final dentro de ella con los siguientes ajustes:
  - 868 MHz, SF9.
  - Clase C.
  - Versión 1.0.3 de LoRaWAN.
  - Modo de activación por OTAA.
- Se utiliza una integración en The Things Stack con MQTT para poder recoger los datos recibidos por el dispositivo final en un servicio de NODE-RED instalado en un contenedor de un servidor Docker.
- Un servidor Docker con dos contenedores:
  - NODE-RED para la recogida de datos del broker de The Things Stack mediante un cliente MQTT, mostrado de datos en un panel de control y toma de decisiones. Además, gestiona límites de temperatura y horarios de ocupación de la estancia que se quiere monitorizar para encender o apagar el sistema de climatización dependiendo de si está ocupada la estancia y si su temparua está fuera de los límites establecidos.
  - InluxDB para el almacenamiento de datos y de variables.
