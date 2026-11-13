# Patrones de referencia

Imágenes incluidas en el selector de patrones y en el ejecutable. La aplicación detecta los tres patrones.
Las figuras y los ejes son la visualización de realidad aumentada; estos patrones permiten estimar su posición.

| Archivo | Patrón | Configuración del detector |
| --- | --- | --- |
| [chessboard.png](chessboard.png) | Ajedrez existente | 9 × 6 esquinas interiores; 10 × 7 cuadrados. |
| [asymmetric_circles.png](asymmetric_circles.png) | Círculos asimétricos | `cv::Size(4, 11)`; 44 centros; `CALIB_CB_ASYMMETRIC_GRID`. |
| [charuco.png](charuco.png) | ChArUco | `cv::Size(5, 7)` cuadrados; 24 esquinas interiores; diccionario `DICT_5X5_100`; marcadores 0–16. |

El patrón de círculos se muestra girado respecto a sus coordenadas de detección. El tamaño `4 × 11` se comprobó sobre esta imagen; `11 × 4` no detecta el tablero completo.

ChArUco usa nominalmente cuadrados de **30 mm** y marcadores de **15 mm**. Para los círculos hay que medir las separaciones del patrón impreso; el radio no equivale a la separación entre centros. El ajedrez existente no determina por sí solo el tamaño físico de sus cuadrados.

Al imprimir, conserva la proporción y los márgenes blancos. Comprueba las medidas reales: ajustar la imagen a la página puede cambiar la escala.

## Procedencia

El ajedrez ya estaba en el proyecto. Las otras dos imágenes se descargaron sin modificaciones del repositorio oficial de OpenCV, rama `4.x`:

- Círculos: [doc/acircles_pattern.png](https://github.com/opencv/opencv/blob/4.x/doc/acircles_pattern.png).
- ChArUco: [doc/charuco_board_pattern.png](https://github.com/opencv/opencv/blob/4.x/doc/charuco_board_pattern.png).
- Documentación: [Create Calibration Pattern](https://docs.opencv.org/4.x/da/d0d/tutorial_camera_calibration_pattern.html).
- Licencia de OpenCV: [copia local](OPENCV_LICENSE) y [original](https://github.com/opencv/opencv/blob/4.x/LICENSE).

Se verificaron inicialmente con OpenCV Python 4.13. Las pruebas CTest de la aplicación comprueban los tres detectores y una vista parcialmente oculta de ChArUco usando el OpenCV de compilación. Esta comprobación sobre imágenes digitales no sustituye las pruebas con cámara e impresión físicas.
