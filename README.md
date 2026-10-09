# Simulador Cardíaco Quirúrgico 3D 🫀

Una plataforma interactiva de simulación médica y fisiológica desarrollada en C++ y OpenGL. Este proyecto permite visualizar un modelo 3D del corazón humano con efectos gráficos avanzados, simulando su comportamiento bajo diferentes patologías y mostrando información de signos vitales en tiempo real.

![Screenshot del Simulador](https://via.placeholder.com/800x450.png?text=Coloca+aqui+una+captura+de+tu+simulador)
*(Nota: Reemplaza este enlace con una captura de pantalla real de tu proyecto)*

## ✨ Características Principales

* **Visualización 3D Avanzada:** Renderizado en tiempo real con texturas PBR (Color, Normales, Roughness) y soporte para visualización de malla (wireframe) y modo Rayos X.
* **Simulación de Patologías:** 
  * Ritmo Sinusal Normal (75 BPM)
  * Taquicardia Ventricular (>100 BPM)
  * Bradicardia Sinusal (<60 BPM)
  * Arritmias Cardíacas (con efectos visuales de "glitch")
* **Efectos Visuales (Post-Procesado HDR):** Bloom, Aberración Cromática, Viñeta, Grano de película, líneas de escáner y efectos de polvo ambiental simulados en GPU.
* **Partículas y Flujo Sanguíneo:** Animación de partículas de sangre tanto en el interior del corazón como en los vasos sanguíneos (Aorta, Vena Cava, Arteria Pulmonar).
* **HUD Médico (Monitor UCI):** Interfaz superpuesta que muestra un Electrocardiograma (ECG) en tiempo real con barrido, frecuencia cardíaca, presión arterial, SpO2 y gasto cardíaco.
* **Cámara Interactiva:** Sistema de cámara orbital suavizada (Lerp) con presets médicos (Vista General, Enfoque Apex, Surco Coronario, etc.) y cinemáticas automáticas.

## 🛠️ Tecnologías y Librerías Utilizadas

* **Lenguaje:** C++
* **Gráficos:** OpenGL (con shaders GLSL embebidos y externos)
* **Ventanas y Contexto:** GLFW
* **Extensiones OpenGL:** GLEW
* **Matemáticas:** GLM (OpenGL Mathematics)
* **Interfaz de Usuario:** [Dear ImGui](https://github.com/ocornut/imgui) (con estilo médico personalizado)
* **Carga de Imágenes:** `stb_image.h`

## 🚀 Requisitos e Instalación

El proyecto está configurado para compilarse en **Visual Studio** en Windows (arquitectura x64).

1. Clona este repositorio:
   ```bash
   git clone [https://github.com/Leandro-Zamora-C/SimuladorCorazon3D.git](https://github.com/Leandro-Zamora-C/SimuladorCorazon3D.git)