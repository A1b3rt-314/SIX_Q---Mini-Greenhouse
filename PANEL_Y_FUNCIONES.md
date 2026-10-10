## ¿Cómo funciona el sistema?

> **Prototipo experimental — no apto para producir alimentos para consumo.** SIX_Q está destinado a la experimentación supervisada. No utilices esta versión para cultivar alimentos destinados al consumo humano. Antes de cualquier uso alimentario, deben evaluarse los materiales en contacto con el agua, el sustrato y las plantas.

SIX_Q es un sistema experimental de cultivo automatizado diseñado con funcionamiento local como prioridad. Las funciones esenciales —riego, iluminación, sensores, panel web y captura de imágenes— pueden operar dentro de la red local. La inteligencia artificial se utiliza únicamente como herramienta opcional de observación.

La Arduino UNO Q se divide en dos capas:

- El microcontrolador gestiona los controles físicos, los relés, el sensor ambiental y la comunicación con la Nicla Vision.
- El microprocesador, que ejecuta Linux, aloja el panel web local, almacena los datos, coordina las capturas y solicita análisis mediante inteligencia artificial.


El sensor AHT20 se conecta mediante I²C Qwiic y proporciona mediciones periódicas de temperatura y humedad.
El sistema dispone de dos controles físicos:

- El pulsador de RIEGO solicita un ciclo de aproximadamente cinco segundos. Una nueva solicitud no prolonga un ciclo que ya está en ejecución.
- El interruptor de LUZ representa el estado solicitado de la iluminación.


Los controles físicos y el panel web funcionan de la misma manera: solicitan una acción, pero es la UNO Q la que decide si activa el relé correspondiente. El navegador nunca controla directamente la bomba ni la tira LED.
La bomba de inmersión y la tira LED impermeable se conectan mediante la Arduino 4 Relays Shield. En el prototipo, el riego está limitado a un máximo de cuatro ciclos aceptados, manuales y programados en conjunto, por día local, con reinicio del contador a las 00:00.


## Sistema de visión

La Nicla Vision funciona como sensor visual. La UNO Q utiliza I²C para enviar comandos, estados y señales de control. Los datos de imagen, que son mucho más grandes, se transmiten mediante TCP a través de la red Wi‑Fi local.
Al pulsar CAPTURE NOW, la UNO Q autentica la Nicla e inicia la secuencia de captura. En la implementación actual, el proceso completo demora aproximadamente un minuto y cuarenta segundos y está limitado a tres capturas por hora.

La cámara captura la escena como 20 secciones de 320 × 320 píxeles, distribuidas en cinco columnas y cuatro filas. La UNO Q reconstruye esas secciones en un lienzo final de 1600 × 1200 píxeles.
V7 utiliza las posiciones horizontales x = 0, 320, 640, 960, 1280 y las posiciones verticales y = 0, 320, 640, 880. La última fila comienza en y = 880, produciendo una superposición vertical de 80 píxeles con la fila anterior. Esto permite cubrir exactamente la altura final y facilita el ensamblaje del mosaico.
La columna ubicada en x = 640 utiliza un perfil reforzado. En esos casos se captura una ventana más amplia del sensor y luego se recorta al tamaño final de 320 × 320 píxeles.

Durante la preparación inicial, la cámara realiza un período de estabilización, descarta cuadros de calentamiento y ajusta sus controles automáticos. Luego esos controles quedan bloqueados para mantener constantes las condiciones de captura. En los tiles reforzados se descarta además un cuadro adicional antes de aceptar la imagen.
Cada tile aceptado se transmite mediante TCP hacia la UNO Q, donde se reconstruye el mosaico y se genera la imagen PNG que se muestra en el panel.

## Problemas históricos del mosaico

Durante las primeras versiones comenzaron a aparecer franjas y bloques de color dentro de algunas imágenes. Al principio el problema era ocasional, pero aumentó con el tiempo de funcionamiento.


![066.jpg](images/066.jpg)
![067.jpg](images/067.jpg)



Se probaron las versiones V5, V5.1 y V6, y se analizaron imágenes, registros y resultados de captura. La evidencia mostró que las anomalías se concentraban especialmente en la tercera columna del mosaico, asociada a x = 640, aunque no fue posible demostrar una causa física única. El comportamiento apuntaba a una posible interacción entre la configuración de la ventana del sensor, la sincronización de captura y la recepción DCMI/DMA.

La versión V7 modificó el protocolo para estabilizar el sensor una sola vez al inicio, bloquear los controles automáticos, utilizar una captura reforzada en la columna problemática y validar el estado de la transferencia antes de aceptar cada tile. También se retiraron del campo de visión de la cámara algunos elementos del sistema de riego e iluminación que podían afectar la imagen por sus reflejos o interferir con el enfoque.

En las pruebas realizadas con V7 no volvió a observarse el patrón de corrupción que motivó la investigación. Esto debe entenderse como un resultado experimental del protocolo utilizado y no como una demostración definitiva de la causa original.


## Panel web local

La UNO Q aloja un panel web accesible desde cualquier dispositivo conectado a la misma red local.

### Acceso por red y seguridad

Este prototipo presupone una red local privada de confianza, con acceso restringido a usuarios y dispositivos de confianza. Restringir el acceso a la red forma parte de la protección de la instalación; estar en una red local no autentica por sí solo a un usuario.

La aplicación no implementa autenticación de usuarios del panel ni permisos de acceso diferenciados. Cualquier cliente que pueda acceder al panel puede solicitar riego, iluminación, capturas y análisis con IA. El MCU sigue aplicando sus reglas de actuación, pero esas reglas no identifican al usuario. Las solicitudes de IA utilizan la clave de API configurada en la instalación y pueden generar cargos.

No expongas el panel ni el puerto TCP de la cámara directamente a Internet, mediante redirección de puertos del router o túneles públicos. El panel utiliza HTTP y el canal de imágenes utiliza TCP sin cifrar. La autenticación de Nicla por I²C no autentica a los usuarios del panel ni proporciona cifrado o autenticación criptográfica al canal TCP de imágenes. Esta versión no está destinada a redes públicas o compartidas con usuarios no confiables.

### Controles del panel

Desde el panel es posible:

- consultar temperatura y humedad;
- ver el estado de RIEGO y LUZ;
- solicitar un ciclo de riego o seleccionar su programa;
- encender la luz o seleccionar un programa;
- solicitar una nueva captura;
- consultar la última imagen;
- solicitar un análisis mediante inteligencia artificial.

![065.jpg](images/065.jpg)

El modo PROGRAM para riego e iluminación está implementado y operativo.

### Reglas de PROGRAM y NOW

- Arduino controla los relés, horarios y cupos. Python transmite la hora local configurada en la aplicación de UNO Q; el navegador no ejecuta horarios.
- Riego PROGRAM: un pulso de 5 segundos en los horarios locales fijos indicados abajo. Seleccionar o volver a seleccionar un programa espera al próximo horario y no repite eventos pasados.
- Riego NOW: solicita un pulso sin cancelar ni desplazar el programa. Cada pulso aceptado, manual o programado, consume uno de los 4 cupos por día local, con reinicio a las 00:00. Las pulsaciones durante un pulso activo se rechazan, sin acumular solicitudes.
- Un turno programado bloqueado por cupo o por un pulso activo se omite. OFF cancela el programa de riego, pero permite terminar un pulso ya aceptado.
- Luz PROGRAM: ventanas diarias 06:00–12:00, 06:00–14:00, 06:00–18:00 o 06:00–00:00 (18 horas), según la hora local configurada en la aplicación de UNO Q. La luz se ajusta a la ventana al seleccionar el programa.
- Luz NOW ON/OFF cancela PROGRAM, también desde el control físico. ON puede mantenerse indefinidamente; no tiene cupo diario.
- El dashboard recupera los modos desde Arduino y muestra el consumo de riego. Cerrar el navegador no detiene los programas.
- Los programas y el cupo se mantienen en RAM: reiniciar el MCU los reinicia. La fecha y hora locales se sincronizan desde Linux; hasta recibirlas no se aceptan programas de riego ni luz.

Algunas automatizaciones más avanzadas permanecen como trabajo futuro.

Horarios locales de riego (pulsos de 5 segundos, sujetos al cupo diario):

| Programa | Horarios |
|---|---|
| 4h | 00:00 · 04:00 · 08:00 · 12:00 · 16:00 · 20:00 |
| 6h | 00:00 · 06:00 · 12:00 · 18:00 |
| 8h | 00:00 · 08:00 · 16:00 |
| 12h | 00:00 · 12:00 |
| 24h | 12:00 |

El programa de 4h ofrece seis turnos, pero el cupo de esta configuración permite cuatro riegos aceptados en total. NOW también consume cupo. Los turnos bloqueados se omiten; no se acumulan. El contador se reinicia a medianoche aunque un pulso siga activo: ese pulso pertenece al día en que fue aceptado. Las correcciones del reloj hacia atrás no repiten turnos ni vuelven a conceder cupo. Se admite la latencia normal dentro del primer minuto del horario; no se recuperan turnos anteriores.

La aplicación de UNO Q utiliza `America/Montevideo` como zona horaria local. Para otra ubicación, se puede configurar la variable de entorno `SIXQ_TIMEZONE` con una zona IANA. El reloj del dashboard, la fecha de la última captura y las ventanas de luz usan esa misma zona, independientemente del navegador o del huso predeterminado del contenedor. Los registros internos conservan UTC. El programa de riego de 24h se ejecuta a las 12:00 del mediodía y comparte el cupo diario. El horario se elige por configuración; la temperatura no habilita ni bloquea el riego.

## Análisis mediante inteligencia artificial

El análisis con inteligencia artificial es opcional y solo se ejecuta cuando el usuario pulsa ANALYZE WITH AI.
La imagen se envía a la API y el resultado se devuelve en un formato estructurado que describe:

- si hay plantas visibles;
- cantidad de plantas detectadas;
- estado general de la planta de mayor tamaño;
- etapa de desarrollo;
- tipo de sustrato visible.

La evaluación del estado de salud fue ajustada utilizando imágenes de referencia de *Lactuca sativa*, incluyendo plantas con poco riego y ausencia de iluminación LED.

![069.jpg](images/069.jpg)
![070.jpg](images/070.jpg)

El análisis considera conjuntamente:

- color y variación de tono;
- forma y desarrollo de las hojas;
- extensión y coloración del tallo;
- desarrollo del brote;
- retracción o pérdida de turgencia;
- deformaciones;
- presencia de tonos marrones o signos de necrosis.

Un cambio de color aislado no se interpreta automáticamente como enfermedad, ya que puede depender de la intensidad y del tipo de iluminación. Para clasificar una planta como dañada deben aparecer también alteraciones morfológicas compatibles. Cuando hay varias plantas visibles, el sistema informa la cantidad detectada y concentra la evaluación detallada en la planta de mayor tamaño o mayor desarrollo visible.

El resultado se muestra como un resumen superpuesto en el panel, sobre la imagen capturada.
El modelo no controla la bomba, la iluminación ni ningún otro actuador. La inteligencia artificial funciona únicamente como una capa adicional de observación e interpretación.


## Requisitos y costos aproximados

Para utilizar el análisis mediante IA se necesita:

- una cuenta de desarrollador;
- una clave de API;
- crédito disponible o facturación habilitada;
- conexión a Internet;
- una imagen capturada correctamente.


La clave no debe incluirse directamente en el código público. En este prototipo se guarda en un archivo local de secretos excluido mediante `.gitignore`.

Configuración utilizada durante las pruebas:

- Modelo: gpt-6-astra
- API: Responses API
- Razonamiento: high
- Entrada: imagen PNG codificada en Base64
- Salida: JSON validado mediante esquema
- Timeout: 120 segundos


El costo y el tiempo dependen del modelo, el tamaño de la imagen y la cantidad de tokens. En las pruebas realizadas, el análisis demoró aproximadamente entre 4 y 6 segundos con razonamiento bajo y entre 6 y 10 segundos con razonamiento alto. El costo observado fue cercano a USD 0,03–0,04 por solicitud.
No existe consumo automático en segundo plano: si el usuario no pulsa ANALYZE WITH AI, no se realiza ninguna solicitud.


## Limitaciones

SIX_Q debe considerarse un prototipo experimental de automatización, sensores, visión artificial y monitoreo asistido por IA. No debe utilizarse todavía para producir alimentos destinados al consumo humano.
Antes de utilizarlo con cultivos comestibles deben evaluarse los materiales que entran en contacto con el agua, el sustrato y el entorno de cultivo, incluyendo plásticos, tubos, adhesivos y elementos estructurales.

El objetivo de esta versión es demostrar que una estructura económica y reproducible puede combinar:
sensores ambientales + controles locales + riego + iluminación + visión artificial + panel web + análisis opcional mediante inteligencia artificial.

Todavía existen aspectos por mejorar, pero esa es parte central del proyecto: construir, probar, observar qué falla y desarrollar una versión mejor.
