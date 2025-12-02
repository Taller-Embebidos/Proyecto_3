# PROPUESTA DE DISEÑO: SISTEMA EMBEBIDO DE DETECCIÓN PASIVA DE IMPACTOS DE RAYO BASADO EN ESP32

**Fecha de entrega:** 02 de diciembre de 2025
**Equipo:** CMR
**Integrantes:**
*   Carlos Elizondo A. – Director de proyecto
*   Manuel Garita B. – Auditor / Investigador
*   Rodrigo Venegas M – Líder técnico

## 1. Justificación del Proyecto y Revisión Bibliográfica
### 1.1 Contexto y Problemática Detallada
#### 1.1.1 Panorama Global de Tormentas Eléctricas
Según la NOAA (2024), ocurren más de 1.4 mil millones de descargas atmosféricas al año, y los sistemas de medición son esenciales para protección estructural e industrial. Las descargas atmosféricas representan riesgo crítico en:
*   infraestructura energética
*   telecomunicaciones
*   sistemas de automatización industrial
*   operaciones de campo

En Costa Rica, el CIGEFI reporta que se registran hasta 25 descargas por km²/año, especialmente en zonas montañosas, lo que crea la necesidad de un sistema de monitoreo local confiable y de bajo costo.

#### 1.1.2 Problema Específico de los Pararrayos Tradicionales
Los pararrayos capturan descargas pero no registran cuántas ocurren, lo cual limita:
*   mantenimiento preventivo
*   análisis de riesgo
*   aseguramiento
*   monitoreo en tiempo real

Los contadores comerciales de impacto requieren instalación profesional, alto costo y no ofrecen telemetría integrada.

#### 1.1.3 Impacto Socioeconómico
*   Fallas por descargas representan pérdidas estimadas de US$14 mil millones globalmente (IEEE Lightning Protection Study, 2023).
*   Los mantenimientos reactivos incrementan costos hasta un 250%.
*   La falta de registro dificulta reclamos a aseguradoras.

#### 1.1.4 Oportunidad Tecnológica
El aumento de plataformas IoT y sensores pasivos permite:
*   medición sin contacto
*   registro digital
*   telemetría por WiFi, LoRa o BLE
*   integración con dashboards industriales

El ESP32 permite adquisición rápida, filtrado digital y transmisión de eventos.

#### 1.1.5 Justificación del Enfoque Propuesto
El proyecto se justifica porque:
*   los sensores pasivos de campo electromagnético no requieren modificar el pararrayo
*   el ESP32 permite implementar conteo robusto, registro y telemetría
*   permite mantenimiento predictivo
*   reduce costos respecto a sistemas comerciales

### 1.2 Revisión Bibliográfica Especializada
**Referencias claves**
1.  IEC 62305-1: Protección contra rayos (2023)
2.  IEEE Std 142: Grounding of Lightning Protection Systems (2022)
3.  Rakov & Uman. Lightning: Physics and Effects (2020)
4.  López et al. “Low-Cost Sensors for Electromagnetic Surge Detection”, IEEE Sensors Journal (2021)
5.  Espressif. ESP32 Technical Reference Manual (2023)

**Gap identificado**
No existe un sistema pasivo, autoalimentado, digital y IoT-ready para conteo de descargas basado en bobina inductiva.

## 2. Descripción y Síntesis del Problema
Los pararrayos convencionales no registran impactos. El reto es:
1.  detectar la señal electromagnética inducida
2.  identificar impactos reales
3.  filtrar ruido
4.  registrar eventos y transmitirlos

## 3. Gestión de Requerimientos
### 3.1 Requerimientos Funcionales
| ID      | Requerimiento       | Descripción                                             | Prioridad |
|---------|---------------------|---------------------------------------------------------|-----------|
| RF001   | Captura inductiva   | Medir impulsos EM de alta frecuencia mediante bobina pasiva | Alta      |
| RF002   | Filtrado analógico  | Atenuar ruido y evitar saturación del ADC               | Alta      |
| RF003   | Detección digital   | Detectar pulsos ≥ 1 kA equivalente                      | Alta      |
| RF004   | Conteo              | Incrementar registro por impacto detectado              | Alta      |
| RF005   | Telemetría          | Enviar conteo vía WiFi/MQTT                             | Media     |
| RF006   | Registro local      | Guardar eventos en NVS/SD                               | Media     |

### 3.2 Requerimientos No Funcionales
| ID      | Requerimiento       | Descripción                                             | Prioridad |
|---------|---------------------|---------------------------------------------------------|-----------|
| RNF001  | Robustez            | Operación bajo lluvia, altas corrientes y ruido EM      | Alta      |
| RNF002  | Consumo             | <200 mA en operación continua                           | Media     |
| RNF003  | Tolerancia          | Inmunidad a impulsos > 20 kV en pararrayo               | Alta      |
| RNF004  | Seguridad           | Aislamiento galvánico completo                          | Alta      |

## 4. Vista Operacional del Sistema
### 4.1 Concepto de Operaciones
El sistema funciona así:
1.  Una bobina pasiva colocada alrededor del conductor del pararrayo capta el pulso EM.
2.  El pulso pasa por un filtro RC + limitador TVS.
3.  El ESP32 detecta el pulso mediante comparador o ADC.
4.  Se registra el evento.
5.  Se transmite a un servidor opcional.

## 5. Vista Funcional del Sistema
### 5.1 Descomposición Funcional

Sistema de Detección de Rayos
├── Sensor Pasivo
│ ├── Bobina inductiva
│ └── Sujeción aislante
├── Acondicionamiento analógico
│ ├── Rectificador
│ ├── Filtro RC
│ ├── Limitador TVS
├── Módulo digital
│ ├── Detección de pico
│ ├── Debounce
│ ├── Conteo
│ └── Timestamps
└── Telemetría
├── WiFi/MQTT
└── Registro interno

## 6. Arquitectura del Sistema Propuesto
### 6.1 Diagrama General
Pararrayo -----|| Inductor Pasivo ||-----[ R ]-----[ Diodo ]----[ RC ]----[ TVS ]---- GPIO/ADC ESP32
↑
Campo EM inducido

### 6.2 Componentes
**Hardware:**
*   ESP32-WROOM
*   Bobina toroidal 40–200 vueltas
*   R (100 kΩ)
*   Diodo 1N4148
*   TVS SMAJ5.0A
*   Capacitor 1 nF + 100 nF
*   Enclosure IP65

**Software:**
*   ESP-IDF
*   ADC + interrupciones
*   MQTT opcional

## 7. Análisis de Dependencias
### 7.1 Software
*   ESP-IDF
*   FreeRTOS integrado
*   MQTT (opcional)
*   NVS Flash

### 7.2 Hardware
*   TVS
*   Diodo rápido
*   Bobina de aire o ferrita
*   Regulador 5V → 3.3V

## 8. Estrategia de Integración
**Fase 1: Sensor físico**
*   diseñar bobina
*   caracterizar señal
*   validar formas de onda

**Fase 2: Analógico**
*   filtro RC
*   limitación de voltaje
*   pruebas con generador de pulsos

**Fase 3: Digital**
*   lógica de detección
*   debounce
*   timestamping

**Fase 4: Telemetría**
*   WiFi
*   MQTT o HTTP

**Fase 5: Validación en sitio**
*   pruebas reales en estructura metálica sin pararrayo
*   pruebas en pararrayo real

## 9. Planificación
| Fase         | Actividades                          | Duración |
|--------------|--------------------------------------|----------|
| Diseño       | Sensor, circuito, arquitectura       | 10 días  |
| Desarrollo   | Firmware, filtro, ADC                | 20 días  |
| Pruebas      | Inducción, ruido, calibración        | 14 días  |
| Documentación| Manual, reporte                      | 10 días  |

## 10. Conclusiones
1.  El sistema permite detección pasiva sin intervención en el pararrayo.
2.  Reduce costos frente a soluciones comerciales.
3.  Ofrece telemetría en tiempo real.
4.  La arquitectura es escalable para múltiples sensores.
