# Guía de ensamblaje — SIX_Q Mini Greenhouse

> **Prototipo experimental — no apto para producir alimentos para consumo.** SIX_Q está destinado a la experimentación supervisada. No utilices esta versión para cultivar alimentos destinados al consumo humano. Antes de cualquier uso alimentario, deben evaluarse los materiales en contacto con el agua, el sustrato y las plantas.

**Acceso por red:** úsalo únicamente en una red local privada de confianza y con acceso restringido. El panel no tiene autenticación de usuarios y no debe exponerse directamente a Internet. Lee las [notas de acceso por red y seguridad](PANEL_Y_FUNCIONES.md#acceso-por-red-y-seguridad) antes de habilitar el acceso por red.

Al elegir los materiales, intenté que todo fuera absurdamente económico y fácil de conseguir, para que hubiera la menor cantidad posible de excusas para no reproducirlo, modificarlo y mejorarlo.  
  
## Hardware
  
- Arduino UNO Q  
- Arduino 4 Relay Shield   
- Sensor AHT20  
- Nicla Vision
- Conversor LM2596 DC-DC  
- AC/DC ADAPTER, ALIOV (INPUT:AC100V-240V,50/60Hz, OUTPUT:DC12V=10A,120W), MODEL NO.: ALT-1210.  
- Tira LED COB, TOPAI 12V, blanca fría 6000K 16.4ft, Dotless IP65 impermeable. Alta densidad 480LEDs/M, CRI>90 4000Lm, cortable, ancho flexible de 0.315 in, autoadhesivo. Se usaron aproximadamente 120cm.   
- Bomba de agua de inmersión, 5V.  
- Pulsador NKK BB-25 e interruptor NKK A-12.   
- 2 juegos de conectores plásticos Molex - Set Completo de 2 - (6 Circuitos) con 14-20 AWG, Conector de Cable - 2.13mm D, Bloqueo con Llave, MLX™  

  

  
  
- Maderas pequeñas, principalmente: 6 de 300 x 20 x 5 mm, 4 de 200 x 20 x 5 mm, 2 de 175 x 20 x 5 mm.   
- 2 caños de aluminio de 10 mm de diámetro y 175 mm de largo para la base y el armazón de la sección inferior.   
- 2 caños de aluminio de 8 mm de diámetro y 180 mm de largo para la base y el armazón de la sección inferior.    
- 4 caños de aluminio de 10 mm de diámetro y 240 mm de largo para el soporte de iluminación y riego de la sección intermedia.
- 1 caño de aluminio de 10 mm de diámetro y 300 mm de largo para distribuir el agua de riego en la sección intermedia, con pequeñas perforaciones cada 50 mm en su parte media.
- Manguera de PVC de 1200 mm de largo y 7mm de diámetro  
- Tornillos de 30 y 40 mm de largo   
- Tacos Fischer número 8 y 6   
- 2 Tornillos, arandela y tuercas de 3mm de diámetro.   
- Bolsas de nylon transparente de 45 x 67 cm  
- 10 Bolsas de nylon de residuos con cintas de colores (para poder extraer de ellas dichas cintas)   
- Tornillos pequeños para fijar electrónica de M2x8, M2.5x6   
- Preparado comercial de tierra para horticultura de 2.5 kg.   
- Bidón de 6 litros de agua potable.   
  
  
  
  
  
  
## Software
  
- Arduino App Lab, para actualizar firmware, cargar código en Arduino UNO Q  
- Arduino IDE, para actualizar firmware de Nicla Vision  
- Arduino Web Editor, para cargar código en Nicla Vision  
- Arduino Cloud Agent, para macOS
- Autodesk Fusion, para generar modelos y archivos para impresión en 3D  
  
  
  
Vamos a dividir el montaje del proyecto en tres partes: sector inferior (armazón y sustrato), sector medio (iluminación y riego) y sector superior (electrónica).   

![052.webp](images/052.webp)
  
  
  
  
## Sección inferior 

En esta sección se encuentran la estructura principal que funciona de andamiaje y la bolsa que contiene el sustrato de cultivo.   
  
![025.jpg](images/025.jpg)
![001.jpg](images/001.jpg)
  
  
  
  
Como puede verse en las imágenes, la base está compuesta por:   
  
- 4 piezas de madera que funcionan como pilares, de 300 × 20 × 5 mm (archivo para impresión 3D: [Pilar_x4.stl](design/Pilar_x4.stl)). Cada una tiene un orificio de 3 mm en la parte superior y otro en la inferior, además de tres pares de ranuras de entre 1 y 2 mm de ancho para sujetar cordones, cintas de nylon o precintos. Las ranuras inferiores sujetan la bolsa de sustrato; las del medio, el sistema de luz y riego; y las superiores, la electrónica.  
  
![054.png](images/054.png)
  
  
  
- 2 piezas de madera de 300 × 20 × 5 mm, con dos ranuras laterales de 10 × 20 mm para poder fijarlas de forma transversal (archivo para impresión 3D: [Barra_lateral.stl](design/Barra_lateral.stl)).

![015.png](images/015.png)
  
- 4 piezas de madera de 200 × 20 × 5 mm, con orificios de 3 mm en ambos extremos y dos pequeños cortes con un ángulo de 45 grados en uno de sus extremos (archivo para impresión 3D: [Base_x4.stl](design/Base_x4.stl)).
  
  
![043.png](images/043.png)
  
  
- Tornillos de 30 y 40 mm de largo, con sus correspondientes tacos Fischer n.º 6 y n.º 8 respectivamente.   
- 2 tubos de aluminio de 10 mm de diámetro y 175 mm de longitud, más 2 tubos de aluminio de 8 mm de diámetro y 180 mm de longitud.  
- 2 tornillos con tuercas y arandelas, de 3 mm de diámetro.  
  
La idea es crear una estructura que pueda plegarse y ajustarse a lo largo de su eje horizontal.  
  
![020.jpg](images/020.jpg)
![030.jpg](images/030.jpg)
  
  
  
Debajo de la bolsa del sustrato hay una tira de nailon que ayuda a conservar la longitud total de la estructura y mantiene ambos extremos bajo tensión.  
  
![006.jpg](images/006.jpg)
![008.jpg](images/008.jpg)
  
  
Para contener el sustrato utilicé una bolsa de nylon transparente de aproximadamente 45 cm de largo y 67 cm de alto, junto con una cinta plástica de otra bolsa de residuos de 55 × 65 cm.   
Se coloca la cinta alrededor del borde superior de la bolsa transparente, por la parte exterior. Luego se enrolla el borde hacia afuera y hacia abajo, junto con la cinta, hasta que el contenedor alcance una altura aproximada de 30 cm.  
  
  
![055.jpg](images/055.jpg)
![003.jpg](images/003.jpg)
![013.jpg](images/013.jpg)
![059.jpg](images/059.jpg)
  
  
Luego, se posiciona la bolsa formando un rectángulo y respetando su eje mayor. Con una tijera, teniendo mucho cuidado de no cortar la cinta de color, se realiza una abertura en cada una de las cuatro esquinas para sacar la cinta.   
  
![009.jpg](images/009.jpg)
  
![016.jpg](images/016.jpg)
![032.jpg](images/032.jpg)
  
Mediante esos cuatro cortes en puntos distribuidos uniformemente se logra una estructura rectangular. A través de esas aberturas se pasan las cintas, que permiten fijar la bolsa a las ranuras inferiores de los pilares laterales.   
  
![014.jpg](images/014.jpg)
  
![021.jpg](images/021.jpg)
  
![028.jpg](images/028.jpg)
![039.jpg](images/039.jpg)
![040.jpg](images/040.jpg)
  
  
![042.jpg](images/042.jpg)
  
  
  
  
  
  
Luego, para tensar esa estructura se colocan las barras laterales de madera, adelante y atrás.   
  
![041.jpg](images/041.jpg)
![004.jpg](images/004.jpg)
  
  
  
  
  
  
  
El sustrato de cultivo está formado por 2.5 kg de preparado comercial de tierra para horticultura. Se debe colocar, para protección de las plantas como si fuese un cantero, una línea más alta por el eje mayor de la bolsa y menos altura a los costados (para que se acumule el exceso de agua fuera de la planta).   
  
  
—————————————-///————————————  
  
  
  
  
## Sección intermedia

En esta sección se encuentra el sistema de iluminación y riego, también fijado al andamiaje principal mediante cintas de nylon.   

![044.jpg](images/044.jpg)
![022.jpg](images/022.jpg)
  
  
Esta sección está compuesta por:   
- 2 tablas de madera de 175 x 20 x 5 mm, cada una, con cuatro orificios de 3 mm y uno de 10mm de diámetro (para el sistema de riego). Trabajando desde los extremos hacia el centro, se coloca un orificio a 10 mm del extremo y a 5 mm del borde superior. El segundo orificio se coloca a 20 mm del extremo y a 5 mm del borde superior. El mismo patrón se repite en ambos extremos. En uno de los lados, a 20 mm del último orificio, se deja un agujero de 10mm para el riego (archivo para impresión 3D: [Soporte_luz_y_riego_x2.stl](design/Soporte_luz_y_riego_x2.stl)).
  
  
![017.png](images/017.png)
  
  
- 4 caños de aluminio de 10 mm de diámetro y 240 mm de largo, cuatro tacos Fischer n.º 8 y ocho tornillos de 40 mm de longitud.  
- La línea de riego está fabricada con un tubo de PVC de 7 mm de diámetro y un caño de aluminio de 10 mm de diámetro y 300 mm de largo, con pequeñas ranuras cada 50 mm en su parte media para distribuir el agua. Contiene una pequeña abrazadera con un tornillo largo que permite indicar y ajustar la dirección del agua de riego.    
  
![011.jpg](images/011.jpg)
  
  
  
- 4  cintas de nylon, una en cada esquina, para asegurar esta sección a los pilares laterales principales, siguiendo el mismo principio utilizado para sujetar la bolsa del sustrato.  

![056.jpg](images/056.jpg)
![036.jpg](images/036.jpg)
  
Una vez montada la estructura, se añade una tira LED impermeable de 1170 mm de longitud en el borde inferior. La tira LED se fija con pequeños precintos plásticos.   
  
![047.jpg](images/047.jpg)
![034.jpg](images/034.jpg)
  
Los extremos de la tira LED deben quedar correctamente aislados, por ejemplo, con cinta aisladora. Luego se retira el papel protector del adhesivo y se coloca la tira orientada hacia abajo, en dirección al sustrato de cultivo.  
  
  
  
  
  
  
  
  
  
  
  
  
———————-////————————————  
  
  
## Sección superior - Electrónica
Esta sección contiene los principales componentes electrónicos del sistema.  
  
Utilicé una Arduino UNO Q, una Arduino 4 Relays Shield y un sensor de temperatura y humedad AHT20 conectado mediante I²C a través del puerto Qwiic.  
  
![027.jpg](images/027.jpg)
![061.jpg](images/061.jpg)
  
También utilicé un interruptor NKK A-12 y un pulsador NKK BB-25. He tenido buena experiencia con esta marca: describiría sus pulsadores como robustos, precisos y minimalistas. En sistemas embebidos suelo elegir pulsadores de doble circuito para que un mismo evento mecánico pueda “avisar” a dos placas de un determinado estado.   
  
![057.jpg](images/057.jpg)
  
Usé una Arduino Nicla Vision. Los controles físicos se montaron sobre una placa perforada para prototipos.  
  
![002.jpg](images/002.jpg)
![018.jpg](images/018.jpg)
![058.jpg](images/058.jpg)
![007.jpg](images/007.jpg)
  
  
  
Al intentar unir los distintos módulos electrónicos, encontré algunos problemas al tratar de imprimir en 3D una pieza de soporte personalizada. Para la entrega del proyecto, como prototipado rápido, utilicé una lámina perforada de cartón sueco de 2 mm de espesor y uní las diferentes secciones mediante tornillos. Algunas partes, como el convertidor DC-DC LM2596, se fijaron con precintos plásticos.
Posteriormente, realicé un soporte para las piezas de electrónica con las medidas correctas, disponible en dos espesores diferentes: 2 mm y 2.5 mm (archivos para impresión 3D: [Soporte_electronica_2mm.stl](design/Soporte_electronica_2mm.stl) y [Soporte_electronica_2.5mm.stl](design/Soporte_electronica_2.5mm.stl)).


![049.jpg](images/049.jpg)
![012.jpg](images/012.jpg)
![035.jpg](images/035.jpg)
![038.jpg](images/038.jpg)
![046.jpg](images/046.jpg)
![026.jpg](images/026.jpg)
![023.jpg](images/023.jpg)
![045.jpg](images/045.jpg)
  
![050.jpg](images/050.jpg)
![005.jpg](images/005.jpg)

Actualmente, ya se cuenta con una segunda edición del soporte impreso en 3D para montar la electrónica, disponible en espesores de 2 mm y 2.5 mm.

![072.jpg](images/072.jpg)
  
Todo el conjunto electrónico queda suspendido mediante cuatro cintas de nylon, pero más largas, como las que ya hemos usado anteriormente para el resto de las estructuras.  
  
  
![062.jpg](images/062.jpg)
  
## Sistema de riego
Este sistema merece un apartado propio. Por seguridad, el contenedor de agua se encuentra por fuera del sistema. Debe contener una cantidad moderada de agua para que, si la bomba queda encendida por error, el exceso de riego no comprometa la vitalidad del cultivo.   
  
El sistema de riego utiliza una pequeña bomba de agua sumergible de 5 V, colocada dentro de un bidón comercial estándar de 6 L (luego se recortó y redujo su capacidad a menos de la mitad).

![024.jpg](images/024.jpg)
![053.jpg](images/053.jpg)

El agua se transporta desde el depósito hasta la zona de cultivo mediante un tubo de PVC de 7 mm de diámetro. Este se conecta al caño de aluminio de 10 mm de diámetro y 300 mm de largo, montado en el soporte de iluminación de la sección intermedia, cuyas perforaciones en la parte media distribuyen el agua de riego.   
  
  
Ese caño tiene un tornillo, el cual señala la dirección de las ranuras.   
  
![010.jpg](images/010.jpg)
  
  
Durante las primeras pruebas configuré cada ciclo de riego con una duración de 30 segundos. Sin embargo, una vez conectada la bomba definitiva, esa duración resultó excesiva: en aproximadamente 30 segundos, la bomba suministró más de 700 mL de agua, una cantidad demasiado elevada para el tamaño del contenedor de cultivo. Tuve que añadir más sustrato para compensar el exceso de agua; esa cantidad adicional está incluida en los 2.5 kg finales.   

Por esa razón, reduje la duración de cada ciclo de riego de 30 a 5 segundos. En estas pruebas, el sustrato recibió 700 mL el primer día, un riego de 5 segundos a los 6 días y, después, un riego de 5 segundos cada 3 días en promedio.   
  
Con la configuración actual, cada solicitud de riego aceptada funciona como un evento discreto:  
  
1 solicitud de RIEGO  
        ↓  
   Bomba encendida  
        ↓  
     5 segundos  
        ↓  
    Bomba apagada  
  
Tanto el pulsador físico de RIEGO como el panel web solicitan el mismo ciclo de riego.  
  
El pulsador no funciona como un control de «mantener presionado para regar». Una pulsación breve es suficiente para solicitar un ciclo completo, y soltar el botón no detiene la bomba antes de que se cumpla el intervalo de 5 segundos.  
  
Para evitar el riego excesivo accidental, el MCU también rechaza nuevas solicitudes de RIEGO mientras ya existe un ciclo activo. Esto impide que pulsaciones repetidas reinicien o prolonguen el ciclo en curso.  
  
En el prototipo, el sistema está limitado a cuatro ciclos de riego aceptados por día local, con reinicio del contador a las 00:00. El cupo es compartido por el pulsador físico, NOW y PROGRAM.  
  
Esto significa:  
  
1 ciclo = 5 segundos  
Máximo = 4 ciclos  
Tiempo máximo de funcionamiento de la bomba = 20 segundos / 24 h  
  
El volumen exacto suministrado durante esos 5 segundos depende de la bomba, la longitud del tubo, el nivel del depósito y la altura de instalación. Por lo tanto, el tiempo de riego debe calibrarse nuevamente si se modifica cualquiera de esos elementos.   
Actualmente, se estima que un ciclo de 5 segundos suministra 120 mL.   
  
Lo importante es que el riego se trata como un evento limitado y repetible, en lugar de dejar la bomba continuamente bajo control manual.  
  
  
  
Esto también sigue el principio general de control de SIX_Q:  
  
Pulsador físico / Panel web  
             ↓  
      Solicitud de RIEGO  
             ↓  
          UNO Q  
             ↓  
    Evaluación de reglas  
             ↓  
          Relé R2  
             ↓  
    Bomba de agua de 5 V  
  
  
La interfaz solicita la acción, pero la UNO Q sigue siendo responsable de decidir cuándo puede funcionar la bomba y cuándo debe detenerse.  



## Control manual y programado

El modo PROGRAM para riego e iluminación está implementado y operativo.

- Riego PROGRAM: un pulso de 5 segundos en horarios locales fijos. La selección espera al próximo horario; no inicia un intervalo desde ese momento.
- Riego NOW: solicita un pulso puntual sin cancelar ni desplazar PROGRAM, siempre sujeto al cupo compartido. Las solicitudes durante un pulso activo se rechazan; los turnos programados bloqueados se omiten, sin acumularse.
- Riego OFF: cancela PROGRAM y deja terminar un pulso ya aceptado.
- Luz PROGRAM: ventanas 06:00–12:00, 06:00–14:00, 06:00–18:00 o 06:00–00:00 (18 horas) según la hora local configurada en la aplicación de UNO Q.
- Luz NOW ON/OFF: cancela PROGRAM, también desde el control físico. El encendido manual puede mantenerse las 24 horas, sin cupo diario.

Horarios locales de riego (pulsos de 5 segundos, sujetos al cupo diario):

| Programa | Horarios |
|---|---|
| 4h | 00:00 · 04:00 · 08:00 · 12:00 · 16:00 · 20:00 |
| 6h | 00:00 · 06:00 · 12:00 · 18:00 |
| 8h | 00:00 · 08:00 · 16:00 |
| 12h | 00:00 · 12:00 |
| 24h | 12:00 |

El programa de 4h ofrece seis turnos, pero el cupo de esta configuración permite cuatro riegos aceptados en total. NOW también consume cupo. Los turnos bloqueados se omiten; no se acumulan. El contador se reinicia a medianoche aunque un pulso siga activo: ese pulso pertenece al día en que fue aceptado. Las correcciones del reloj hacia atrás no repiten turnos ni vuelven a conceder cupo. Se admite la latencia normal dentro del primer minuto del horario; no se recuperan turnos anteriores.

Arduino decide cuándo activar los relés. Los programas continúan con el navegador cerrado; reiniciar el MCU reinicia los programas y el cupo. Las reglas completas están en [Panel y funciones](PANEL_Y_FUNCIONES.md#reglas-de-program-y-now).

La aplicación de UNO Q utiliza `America/Montevideo` como zona horaria local. Para otra ubicación, se puede configurar la variable de entorno `SIXQ_TIMEZONE` con una zona IANA. El reloj del dashboard, la fecha de la última captura y las ventanas de luz usan esa misma zona, independientemente del navegador o del huso predeterminado del contenedor. Los registros internos conservan UTC. El programa de riego de 24h se ejecuta a las 12:00 del mediodía y comparte el cupo diario. El horario se elige por configuración; la temperatura no habilita ni bloquea el riego.

## Configuración de Arduino App Lab

Para la aplicación de la UNO Q usé Arduino App Lab 0.10.0 para macOS. Primero configuré la conexión a la red Wi-Fi local mediante SSH desde el Mac. Luego cargué la aplicación y la activé en modo DEFAULT.

Seleccioné los Bricks WebUI - HTML y Database - SQL, y añadí la librería SparkFun Qwiic Humidity AHT20 1.0.3 para el sensor de temperatura y humedad.

![071.jpg](images/071.jpg)

Para la función de IA intenté crear un Brick personalizado, pero finalmente opté por integrar el código Python (`.py`) en la aplicación principal.

**Archivos de configuración:** cada placa tiene su propio archivo de claves. El proyecto de la Nicla Vision, que se carga desde Arduino Web Editor, utiliza `secrets.h` junto con el sketch `.ino` y los archivos `.cpp` y `.h` de la cámara.

En la aplicación de la UNO Q, dentro de la carpeta `python` de Arduino App Lab, copia `secrets_auth.example.py` con el nombre `secrets_auth.py`. Configura `AUTH_SHARED_KEY` con el mismo valor que utilizaste en el archivo `secrets.h` de la Nicla Vision.

Si vas a utilizar el análisis con IA, completa también `OPENAI_API_KEY`; de lo contrario, déjala como una cadena vacía (`OPENAI_API_KEY = ""`). El archivo `secrets_auth.py` debe existir para que la aplicación arranque, aunque no utilices IA. No publiques tus claves en GitHub.

El nombre `secrets_auth.py` evita un conflicto con el módulo estándar `secrets` de Python. Al intentar usar el nombre `secrets.py`, Arduino App Lab mostró un error e impidió la carga. Por eso es importante conservar el nombre indicado.
