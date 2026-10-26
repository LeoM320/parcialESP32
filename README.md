ParcialESP32
============

Plantilla para el parcial de Sistemas Embebidos (UNER) en un ESP32-S3 con ESP-IDF v6.0.2 y
FreeRTOS: librerías finitas en `main/` y todo el programa a la vista en `main/main.c`.

- Consola TCP (MobaXterm, puerto 1234) y por UART con microrl.
- Página web desde `www/` con handlers en JSON (cJSON).
- Cliente TCP con protocolo UNER.
- NVS, GPIO, ADC, PWM, LED RGB y timer de hardware.

```
idf.py build
idf.py -p COM6 flash          # programa + www/
idf.py -p COM6 monitor
```

| Archivo | Para qué |
|---|---|
| `GUIA.md` | el proyecto: mapa, consola, web, NVS, UNER, recetas, problemas comunes |
| `GUIA_FREERTOS.md` | tareas, colas, mutex, event groups, ISR, patrones |
| `GUIA_C.md` | C para embebidos |
| `GUIA_UML.md` | diagramas de estados, secuencia, actividad y tareas |
| `SIMULACRO_1.md` | parcial de práctica |
| `practicas/` | ejemplos completos y probados para copiar |
