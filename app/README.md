# Camera Lab

Interfaz de escritorio en C++17, Qt 6 Widgets, OpenGL 3.3 y OpenCV 4.5 o posterior.
El `main.cpp` original permanece intacto.

## Archivos principales

La aplicación funciona de forma independiente del `main.cpp` de la raíz. Su lógica se distribuye así:

| Archivo | Responsabilidad |
| --- | --- |
| `main.cpp` | Interfaz, estados de los controles y diálogos de carga/guardado. |
| `camera.cpp` / `camera.h` | Captura de vídeo, detección del tablero, vistas de la sesión y ejecución de la calibración fuera de la interfaz. |
| `vision.cpp` / `vision.h` | Cálculo de calibración, estimación de pose, dibujo de realidad aumentada y lectura/escritura del YAML. |
| `video_view.cpp` / `video_view.h` | Presentación del vídeo mediante una textura OpenGL, en lugar de `imshow`. |
| `tests/vision_test.cpp` | Pruebas de calibración, persistencia, validación y pose. |
| `CMakeLists.txt` | Compilación de la aplicación y las pruebas. |

## Compilar y ejecutar

Dependencias en Fedora:

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel opencv-devel
```

Desde la raíz del proyecto:

```bash
cmake -S app -B app/build -DCMAKE_BUILD_TYPE=Release
cmake --build app/build -j
ctest --test-dir app/build --output-on-failure
./app/build/camera-lab
```

## Uso

1. Configura el índice de cámara, las esquinas interiores y el lado del cuadrado en milímetros. Los valores iniciales son los del programa original: cámara 0, tablero 9 × 6 y 24.5 mm.
2. Inicia la cámara. Con el tablero completo y enfocado, pulsa **Capturar vista** o **Espacio**. Cambia la inclinación, distancia y posición entre capturas. Mantén fijo el enfoque/zoom del objetivo.
3. Con al menos ocho vistas distintas, pulsa **Calibrar cámara** o **Ctrl+Enter**. Ocho es un mínimo de la aplicación, no una garantía de precisión; usa más vistas bien distribuidas.
4. Revisa el RMS en píxeles. Activa la realidad aumentada para ver el volumen, los ejes y la distancia desde la cámara al origen del tablero, en mm. El botón muestra **activada** en azul, **desactivada** o **en pausa** al detener el vídeo. El dibujo aparece cuando se detecta el tablero.
5. Guarda con **Guardar calibración** o **Ctrl+S**. El YAML conserva `cameraMatrix`, `distCoeffs` y `RMS`, y agrega resolución, dimensiones del tablero, tamaño físico, número de vistas e índice de cámara. El archivo se escribe de forma atómica.

### Reutilizar una calibración

Con la cámara detenida, pulsa **Cargar calibración…** o **Ctrl+O** y selecciona el YAML. Se restauran automáticamente las dimensiones y el tamaño físico del tablero. El resultado aparece guardado; las vistas usadas para calcularlo no se recuperan.

Si el archivo incluye un índice de cámara, debe coincidir con el seleccionado. Usa la **misma cámara física y el mismo enfoque/zoom**: el índice no identifica de forma inequívoca al dispositivo y la aplicación no puede verificar esas condiciones.

Inicia la cámara. Se solicita la resolución guardada y se comprueba la resolución real del primer fotograma. Si no coincide, la sesión se detiene y la realidad aumentada queda deshabilitada. Si coincide, puedes activar la realidad aumentada sin volver a capturar ni calibrar.

Los archivos anteriores de Camera Lab sin `cameraIndex` son compatibles si contienen el resto de los metadatos. Los YAML del programa original que solo tienen matrices y RMS se rechazan porque faltan resolución y configuración del tablero. Un archivo inválido no reemplaza la sesión actual.

Puedes guardar las calibraciones locales en `app/calibraciones/` y cargarlas desde allí. El diálogo permite elegir otra ubicación; la carpeta no se crea automáticamente.

**Detener cámara** conserva las capturas y el resultado en memoria. **Nueva sesión** permite cambiar la configuración y pide confirmación antes de descartar capturas. Agregar una captura invalida la calibración anterior y requiere recalibrar. Los archivos guardados se conservan.

Al iniciar, el formulario de configuración se sustituye por un resumen para dejar visibles los controles de captura y resultados. El panel permite desplazamiento en ventanas pequeñas.

## Diseño y límites

- Captura/detección en un hilo; calibración en una tarea independiente; widgets y renderizado en el hilo de interfaz.
- La interfaz consulta el último fotograma cada 16 ms sin acumular una cola. El indicador muestra FPS de captura/procesamiento, no FPS de pantalla ni latencia de extremo a extremo.
- El vídeo conserva la relación de aspecto. Se reutiliza una textura OpenGL; el shader interpreta BGR directamente, sin convertir cada imagen a RGB en CPU. Existe una transferencia CPU → GPU por fotograma mostrado.
- Las capturas con desplazamiento RMS de esquinas inferior al 1.5 % de la diagonal respecto a alguna vista previa se rechazan. Es una heurística contra duplicados, no una evaluación completa de cobertura o calidad.
- La calibración usa el modelo pinhole de OpenCV. Una lente fisheye requiere otro modelo. Una resolución distinta exige una nueva sesión.
- Se guardan y cargan parámetros y metadatos, no imágenes ni la sesión de capturas. La carga es explícita y valida matrices, distorsión, RMS y metadatos; admite archivos de hasta 1 MB.
- Se necesita una sesión gráfica con OpenGL 3.3. La adquisición usa el backend de cámara de OpenCV; una lectura bloqueada por el controlador puede retrasar la detención o el cierre.

## Verificación

CTest recupera una cámara conocida desde vistas sintéticas y comprueba validación, error de reproyección, guardado/carga, rechazo de archivos inválidos, restauración de sesión, pose y escala en milímetros. No sustituye la validación con cámara y tablero reales.

Prueba gráfica sin abrir la cámara:

```bash
./app/build/camera-lab --smoke-test /tmp/camera-lab.png
```

Muestra una imagen de prueba, verifica el contexto OpenGL, la orientación y los canales de color, guarda una captura y termina.

## Archivos locales y Git

`app/.gitignore` excluye `build/`, las variantes `build-*/`, `calibraciones/` y los archivos `calibration_results.yml` y `calibration_results.yaml` dentro de `app/`. Las calibraciones permanecen en disco, pero no se incluyen en Git; conserva una copia si necesitas respaldarlas.
