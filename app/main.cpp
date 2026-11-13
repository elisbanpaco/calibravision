#include "camera.h"
#include "video_view.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QImage>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <opencv2/imgproc.hpp>
#include <memory>
#include <algorithm>
#include <stdexcept>

constexpr const char* patternImages[] = {"chessboard.png", "asymmetric_circles.png", "charuco.png"};

class Window : public QMainWindow {
public:
    Window();
    VideoView* video = new VideoView;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refresh();
    void save();
    void load();
    void updatePattern(bool defaults);
    Board selectedBoard() const;
    bool discardSession();
    std::unique_ptr<Camera> camera;
    std::uint64_t sequence = 0;
    bool stopping = false, unsaved = false;
    std::uint64_t savedRevision = 0;
    QSpinBox *device = new QSpinBox, *columns = new QSpinBox, *rows = new QSpinBox;
    QDoubleSpinBox* square = new QDoubleSpinBox;
    QDoubleSpinBox* marker = new QDoubleSpinBox;
    QComboBox* patternChoice = new QComboBox;
    QComboBox* figureChoice = new QComboBox;
    QPushButton* axes = new QPushButton("Mostrar ejes: sí");
    QLabel *columnLabel = new QLabel, *rowLabel = new QLabel, *sizeLabel = new QLabel,
           *markerLabel = new QLabel("&Lado del marcador"), *boardHint = new QLabel;
    QGroupBox* settings = new QGroupBox("1. Configura el tablero");
    QGroupBox* output = new QGroupBox("3. Revisa el resultado");
    QLabel* configuration = new QLabel;
    QPushButton *start = new QPushButton("Iniciar cámara"),
                *reset = new QPushButton("Nueva sesión"),
                *capture = new QPushButton("Capturar vista"),
                *calibrateButton = new QPushButton("Calibrar cámara"),
                *saveButton = new QPushButton("Guardar calibración…"),
                *loadButton = new QPushButton("Cargar calibración…");
    QPushButton* ar = new QPushButton("Realidad aumentada: desactivada");
    QLabel *connection = new QLabel("Cámara detenida"),
           *pattern = new QLabel("Tablero sin detectar"),
           *metrics = new QLabel("Sin vídeo"),
           *samples = new QLabel("0 vistas capturadas"),
           *result = new QLabel("Todavía no hay una calibración."),
           *distance = new QLabel("Distancia al origen: —"),
           *message = new QLabel("Inicia la cámara para comenzar.");
    QProgressBar* progress = new QProgressBar;
};

Window::Window()
{
    setWindowTitle("Camera Lab · Calibración y realidad aumentada");
    resize(1180, 840);
    setMinimumSize(900, 650);
    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(24,20,24,16);
    layout->setSpacing(16);
    auto* title = new QLabel("Calibración de cámara");
    title->setObjectName("title");
    layout->addWidget(title);
    layout->addWidget(new QLabel("Captura distintas vistas del tablero y utiliza la calibración para estimar su posición."));

    auto* splitter = new QSplitter;
    auto* preview = new QWidget;
    auto* previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(0,0,12,0);
    auto* videoHeader = new QHBoxLayout;
    videoHeader->addWidget(connection);
    videoHeader->addStretch();
    videoHeader->addWidget(pattern);
    previewLayout->addLayout(videoHeader);
    previewLayout->addWidget(video, 1);
    previewLayout->addWidget(metrics);
    previewLayout->addWidget(distance);
    splitter->addWidget(preview);

    auto* panel = new QWidget;
    panel->setMinimumWidth(310);
    auto* controls = new QVBoxLayout(panel);
    controls->setContentsMargins(0,0,0,0);
    controls->setSpacing(14);
    auto* form = new QFormLayout(settings);
    patternChoice->addItems({"Ajedrez", "Círculos asimétricos", "ChArUco"});
    patternChoice->setObjectName("patternChoice");
    form->addRow("&Patrón", patternChoice);
    device->setRange(0,99);
    device->setToolTip("Índice de cámara de OpenCV. La cámara principal suele ser 0.");
    columns->setRange(3,30);
    columns->setValue(9);
    rows->setRange(3,30);
    rows->setValue(6);
    square->setRange(0.1,1000);
    square->setDecimals(2);
    square->setValue(24.5);
    square->setSuffix(" mm");
    marker->setRange(0.1,999);
    marker->setDecimals(2);
    marker->setValue(15);
    marker->setSuffix(" mm");
    form->addRow("&Cámara", device);
    columnLabel->setBuddy(columns);
    rowLabel->setBuddy(rows);
    sizeLabel->setBuddy(square);
    markerLabel->setBuddy(marker);
    form->addRow(columnLabel, columns);
    form->addRow(rowLabel, rows);
    form->addRow(sizeLabel, square);
    form->addRow(markerLabel, marker);
    boardHint->setWordWrap(true);
    form->addRow(boardHint);
    controls->addWidget(settings);
    configuration->setWordWrap(true);
    controls->addWidget(configuration);
    auto* sessionButtons = new QHBoxLayout;
    sessionButtons->addWidget(start, 1);
    sessionButtons->addWidget(reset);
    reset->setToolTip("Detén la cámara antes de iniciar otra sesión y cambiar la configuración.");
    controls->addLayout(sessionButtons);
    loadButton->setShortcut(QKeySequence::Open);
    loadButton->setToolTip("Carga un YAML guardado. Detén la cámara antes de cargar otra calibración. Atajo: Ctrl+O.");
    controls->addWidget(loadButton);

    auto* acquisition = new QGroupBox("2. Captura y calibra");
    auto* acquisitionLayout = new QVBoxLayout(acquisition);
    auto* hint = new QLabel("Captura al menos 8 vistas. Varía la inclinación, la distancia y la zona de la imagen. Mantén fijo el enfoque de la cámara.");
    hint->setWordWrap(true);
    acquisitionLayout->addWidget(hint);
    acquisitionLayout->addWidget(samples);
    progress->setRange(0,minimumViews);
    progress->setTextVisible(false);
    progress->setAccessibleName("Progreso hacia el mínimo de ocho vistas");
    acquisitionLayout->addWidget(progress);
    capture->setShortcut(QKeySequence(Qt::Key_Space));
    capture->setToolTip("Captura el tablero visible. Atajo: Espacio.");
    calibrateButton->setShortcut(QKeySequence("Ctrl+Return"));
    calibrateButton->setToolTip("Disponible desde ocho vistas. Atajo: Ctrl+Enter.");
    calibrateButton->setObjectName("primary");
    acquisitionLayout->addWidget(capture);
    acquisitionLayout->addWidget(calibrateButton);
    controls->addWidget(acquisition);

    auto* outputLayout = new QVBoxLayout(output);
    result->setWordWrap(true);
    outputLayout->addWidget(result);
    auto* rmsHint = new QLabel("El RMS expresa el error de reproyección en píxeles. Un valor menor es mejor; no garantiza por sí solo una calibración precisa.");
    rmsHint->setWordWrap(true);
    outputLayout->addWidget(rmsHint);
    figureChoice->addItems({"Prisma rectangular", "Pirámide", "Cilindro", "Esfera", "Cono", "Toroide"});
    figureChoice->setObjectName("figureChoice");
    figureChoice->setToolTip("Cambia la figura durante el vídeo sin recalibrar la cámara.");
    auto* figureForm = new QFormLayout;
    figureForm->addRow("&Figura", figureChoice);
    outputLayout->addLayout(figureForm);
    axes->setObjectName("axes");
    axes->setCheckable(true);
    axes->setChecked(true);
    axes->setToolTip("Muestra los ejes X, Y y Z junto a la figura cuando la realidad aumentada está activada.");
    outputLayout->addWidget(axes);
    ar->setCheckable(true);
    ar->setObjectName("ar");
    ar->setToolTip("Activa o desactiva la figura elegida y los ejes opcionales sobre el patrón detectado.");
    outputLayout->addWidget(ar);
    saveButton->setShortcut(QKeySequence::Save);
    outputLayout->addWidget(saveButton);
    controls->addWidget(output);
    controls->addStretch();
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel);
    splitter->addWidget(scroll);
    splitter->setStretchFactor(0,1);
    splitter->setSizes({760,350});
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter,1);
    message->setWordWrap(true);
    message->setMinimumHeight(44);
    message->setObjectName("message");
    message->setAccessibleName("Estado de la sesión");
    layout->addWidget(message);
    setCentralWidget(root);
    statusBar()->showMessage("Espacio: capturar · Ctrl+Enter: calibrar · Ctrl+O: cargar · Ctrl+S: guardar");

    connect(start, &QPushButton::clicked, this, [this] {
        if (camera && camera->isRunning()) {
            camera->requestInterruption();
            stopping = true;
        } else {
            if (!camera) {
                try {
                    const auto board = selectedBoard();
                    board.points();
                    camera = std::make_unique<Camera>(device->value(), board);
                } catch (const std::exception& error) {
                    QMessageBox::warning(this, "Revisa el patrón", QString::fromUtf8(error.what()));
                    return;
                }
            }
            camera->setFigure(static_cast<Figure>(figureChoice->currentIndex()));
            camera->setAxes(axes->isChecked());
            stopping = false;
            camera->start();
        }
        refresh();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!discardSession()) return;
        camera.reset();
        sequence = 0;
        unsaved = false;
        savedRevision = 0;
        updatePattern(false);
        refresh();
    });
    connect(capture, &QPushButton::clicked, this, [this] { camera->captureView(); });
    connect(calibrateButton, &QPushButton::clicked, this, [this] { camera->calibrateViews(); });
    connect(ar, &QPushButton::toggled, this, [this](bool enabled) {
        if (camera) camera->setPose(enabled);
        refresh();
    });
    connect(figureChoice, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (camera) camera->setFigure(static_cast<Figure>(index));
    });
    connect(axes, &QPushButton::toggled, this, [this](bool enabled) {
        if (camera) camera->setAxes(enabled);
        axes->setText(enabled ? "Mostrar ejes: sí" : "Mostrar ejes: no");
    });
    connect(saveButton, &QPushButton::clicked, this, [this] { save(); });
    connect(loadButton, &QPushButton::clicked, this, [this] { load(); });
    connect(patternChoice, &QComboBox::currentIndexChanged, this, [this] { updatePattern(true); });
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    timer->start(16);
    updatePattern(true);
    refresh();
}

Board Window::selectedBoard() const
{
    return {columns->value(), rows->value(), static_cast<float>(square->value()),
            static_cast<Pattern>(patternChoice->currentIndex()), static_cast<float>(marker->value())};
}

void Window::updatePattern(bool defaults)
{
    const auto type = static_cast<Pattern>(patternChoice->currentIndex());
    const bool charuco = type == Pattern::Charuco;
    const bool circles = type == Pattern::AsymmetricCircles;
    if (defaults) {
        columns->setValue(charuco ? 5 : circles ? 4 : 9);
        rows->setValue(charuco ? 7 : circles ? 11 : 6);
        square->setValue(charuco ? 30 : circles ? 20 : 24.5);
        marker->setValue(15);
    }
    columnLabel->setText(charuco ? "&Columnas de cuadrados" : circles ? "&Columnas de centros" : "&Columnas interiores");
    rowLabel->setText(charuco ? "&Filas de cuadrados" : circles ? "&Filas de centros" : "&Filas interiores");
    sizeLabel->setText(circles ? "&Paso base" : "&Lado del cuadrado");
    marker->setVisible(charuco);
    markerLabel->setVisible(charuco);
    boardHint->setText(charuco ?
        "Cuenta cuadrados. Diccionario fijo: DICT_5X5_100, IDs desde 0. Se requieren 6 esquinas no colineales; puede verse parcialmente." : circles ?
        "Referencia girada: 4 × 11 centros. Paso base = mitad de la separación entre centros de una misma fila del modelo. Mide el impreso." :
        "Cuenta las esquinas interiores, no los cuadrados. Mide el lado real del cuadrado.");
    const QImage image = QImage(QString(":/patterns/%1").arg(patternImages[patternChoice->currentIndex()]))
        .convertToFormat(QImage::Format_RGB888);
    if (image.isNull()) {
        video->setFrame({});
        message->setText("No se pudo cargar la referencia del patrón.");
        return;
    }
    cv::Mat rgb(image.height(), image.width(), CV_8UC3,
                const_cast<uchar*>(image.constBits()), image.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    video->setFrame(bgr);
    video->setAccessibleName("Referencia del patrón " + patternChoice->currentText() + " y vídeo de la cámara");
}

void Window::refresh()
{
    const auto state = camera ? camera->snapshot() : CameraState{};
    const bool running = camera && camera->isRunning();
    const bool live = running && state.connected && !stopping;
    const bool calibrated = state.calibration.valid();
    if (state.sequence != sequence) {
        video->setFrame(state.frame);
        sequence = state.sequence;
    }
    unsaved = calibrated && state.calibrationRevision != savedRevision;
    settings->setVisible(!camera);
    configuration->setVisible(camera != nullptr);
    configuration->setText(QString("Cámara %1 · %2\n%3 × %4 · %5 mm%6\nPara cambiar: detén la cámara y elige Nueva sesión.")
        .arg(device->value()).arg(patternChoice->currentText()).arg(columns->value()).arg(rows->value())
        .arg(camera ? camera->board.squareMm : square->value(),0,'f',2)
        .arg(patternChoice->currentIndex() == 2 ? QString(" · marcador %1 mm").arg(marker->value(),0,'f',2) : ""));
    output->setVisible(camera != nullptr);
    reset->setEnabled(camera && !running);
    loadButton->setEnabled(!running);
    start->setEnabled(!stopping || !running);
    start->setText(running ? (stopping ? "Deteniendo…" : "Detener cámara") : "Iniciar cámara");
    capture->setEnabled(live && state.pattern && !state.calibrating);
    calibrateButton->setEnabled(live && state.samples >= minimumViews && !state.calibrating);
    calibrateButton->setText(state.calibrating ? "Calibrando…" : "Calibrar cámara");
    saveButton->setEnabled(calibrated);
    figureChoice->setEnabled(calibrated);
    axes->setEnabled(calibrated);
    ar->setEnabled(live && calibrated);
    if (!calibrated) {
        const QSignalBlocker blocker(ar);
        ar->setChecked(false);
    }
    ar->setText(!ar->isChecked() ? "Realidad aumentada: desactivada" :
                live ? "Realidad aumentada: activada" : "Realidad aumentada: en pausa");
    connection->setText(live ? "Cámara conectada" : running ? "Cámara en transición…" : "Cámara detenida");
    pattern->setText(live && state.pattern ? QString("Patrón detectado · %1 puntos").arg(state.detectedPoints) :
        live && state.detectedPoints > 0 ? "Puntos insuficientes o colineales" : "Patrón sin detectar");
    samples->setText(QString("%1 vistas capturadas · mínimo %2").arg(state.samples).arg(minimumViews));
    progress->setValue(std::min(state.samples, minimumViews));
    metrics->setText(state.frame.empty() ? "Referencia: " + patternChoice->currentText() + " · Inicia la cámara para detectar el patrón" :
        QString("%1 × %2 px · %3 FPS de procesamiento%4")
            .arg(state.frame.cols).arg(state.frame.rows).arg(state.fps,0,'f',1)
            .arg(live ? "" : " · Última imagen; vídeo detenido"));
    distance->setText(state.distanceMm > 0 ?
        QString("Distancia al origen: %1 mm").arg(state.distanceMm,0,'f',1) : "Distancia al origen: —");
    result->setText(calibrated ? QString("RMS: %1 px\n%2 vistas · %3 × %4 px%5")
        .arg(state.calibration.rms,0,'f',4).arg(state.calibration.samples)
        .arg(state.calibration.imageSize.width).arg(state.calibration.imageSize.height)
        .arg(unsaved ? "\nResultado pendiente de guardar." : "\nResultado guardado.") :
        "Todavía no hay una calibración.");
    message->setText(video->error.isEmpty() ? state.message : video->error);
}

bool Window::discardSession()
{
    if (!camera || camera->snapshot().samples == 0)
        return true;
    return QMessageBox::question(this, "Descartar sesión",
        unsaved ? "Hay un resultado sin guardar. ¿Descartar la sesión y sus capturas?" :
                  "Las capturas de esta sesión se perderán. Los archivos guardados se conservan. ¿Continuar?",
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void Window::closeEvent(QCloseEvent* event)
{
    if (camera && camera->snapshot().calibrating) {
        QMessageBox::information(this, "Calibración en curso", "Espera a que termine la calibración antes de cerrar.");
        event->ignore();
        return;
    }
    if (!discardSession()) { event->ignore(); return; }
    camera.reset();
    event->accept();
}

void Window::save()
{
    const auto state = camera->snapshot();
    if (!state.calibration.valid()) return;
    const QString path = QFileDialog::getSaveFileName(this, "Guardar calibración",
        "calibration_results.yml", "Calibración OpenCV (*.yml *.yaml)");
    if (path.isEmpty()) return;
    try {
        const auto& c = state.calibration;
        const auto data = writeCalibration(c, camera->board, device->value());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size()) ||
            !file.commit())
            throw std::runtime_error(file.errorString().toStdString());
        savedRevision = state.calibrationRevision;
        unsaved = false;
        statusBar()->showMessage("Calibración guardada en " + path);
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "No se pudo guardar", QString::fromUtf8(error.what()));
    }
    refresh();
}

void Window::load()
{
    const QString path = QFileDialog::getOpenFileName(this, "Cargar calibración",
        QString(), "Calibración OpenCV (*.yml *.yaml)");
    if (path.isEmpty()) return;
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error(file.errorString().toStdString());
        if (file.size() > 1024*1024)
            throw std::runtime_error("El archivo supera 1 MB.");
        const auto bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error(file.errorString().toStdString());
        const auto loaded = readCalibration(bytes.toStdString());
        if (loaded.cameraIndex >= 0 && loaded.cameraIndex != device->value())
            throw std::runtime_error(QString("El archivo indica la cámara %1. Inicia una nueva sesión y selecciona ese índice antes de cargarlo.")
                .arg(loaded.cameraIndex).toStdString());
        auto replacement = std::make_unique<Camera>(device->value(), loaded.board, loaded.calibration);
        if (!discardSession()) return;
        camera = std::move(replacement);
        camera->setFigure(static_cast<Figure>(figureChoice->currentIndex()));
        camera->setAxes(axes->isChecked());
        {
            const QSignalBlocker blocker(patternChoice);
            patternChoice->setCurrentIndex(static_cast<int>(loaded.board.pattern));
        }
        columns->setValue(loaded.board.columns);
        rows->setValue(loaded.board.rows);
        square->setValue(loaded.board.squareMm);
        marker->setValue(loaded.board.markerMm);
        sequence = 0;
        stopping = false;
        savedRevision = camera->snapshot().calibrationRevision;
        updatePattern(false);
        {
            const QSignalBlocker blocker(ar);
            ar->setChecked(false);
        }
        statusBar()->showMessage("Calibración cargada desde " + path);
        refresh();
    } catch (const std::exception& error) {
        QMessageBox::critical(this, "No se pudo cargar", QString::fromUtf8(error.what()));
    }
}

int main(int argc, char* argv[])
{
    QSurfaceFormat format;
    format.setVersion(3,3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    app.setApplicationName("Camera Lab");
    app.setStyle("Fusion");
    app.setStyleSheet(
        "QWidget {font-family: 'DejaVu Sans'; font-size: 13px; color: #17283d;}"
        "QMainWindow, QScrollArea, QScrollArea > QWidget > QWidget {background: #f3f6fa;}"
        "QLabel#title {font-size: 26px; font-weight: 700;}"
        "QGroupBox {background: white; border: 1px solid #cbd5e1; border-radius: 8px;"
        "margin-top: 12px; padding: 16px 12px 12px;}"
        "QGroupBox::title {subcontrol-origin: margin; left: 12px; padding: 0 5px; font-weight: 600;}"
        "QPushButton {min-height: 38px; padding: 0 12px; border: 1px solid #94a3b8;"
        "border-radius: 6px; background: white; font-weight: 600;}"
        "QPushButton:hover {background: #e5edf7;}"
        "QPushButton:pressed {background: #cbdcf2;}"
        "QPushButton#primary {background: #1559b7; color: white; border-color: #1559b7;}"
        "QPushButton#primary:hover {background: #10458e;}"
        "QPushButton#ar:checked, QPushButton#axes:checked {background: #1559b7; color: white; border: 2px solid #10458e;}"
        "QPushButton#ar:checked:hover, QPushButton#axes:checked:hover {background: #10458e;}"
        "QPushButton#ar:checked:disabled, QPushButton#axes:checked:disabled {background: #e9eef5; color: #64748b; border-color: #94a3b8;}"
        "QPushButton:disabled, QPushButton#primary:disabled {background: #e9eef5; color: #64748b; border-color: #cbd5e1;}"
        "QPushButton:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {border: 2px solid #1559b7;}"
        "QSpinBox, QDoubleSpinBox {min-height: 30px; background: white; border: 1px solid #94a3b8; border-radius: 4px;}"
        "QComboBox {min-height: 34px; background: white; border: 1px solid #94a3b8; border-radius: 4px;}"
        "QLabel#message {background: #e5edf7; border-radius: 6px; padding: 10px 14px;}"
        "QProgressBar {border: 0; background: #e2e8f0; border-radius: 3px; max-height: 6px;}"
        "QProgressBar::chunk {background: #1559b7; border-radius: 3px;}");
    Window window;
    window.show();
    if (app.arguments().contains("--smoke-test")) {
        for (const char* name : patternImages)
            if (QImage(QString(":/patterns/%1").arg(name)).isNull()) return 1;
        cv::Mat image(480,640,CV_8UC3,cv::Scalar(30,20,10));
        cv::rectangle(image, {0,0,320,240}, {0,0,255}, cv::FILLED);
        cv::rectangle(image, {320,240,320,240}, {0,255,0}, cv::FILLED);
        window.video->setFrame(image);
        QTimer::singleShot(1500, &window, [&] {
            const QImage rendered = window.video->grabFramebuffer();
            bool valid = window.video->isValid() && window.video->error.isEmpty() && !rendered.isNull();
            if (valid) {
                QSize fitted(640,480);
                fitted.scale(rendered.size(), Qt::KeepAspectRatio);
                const int x = (rendered.width()-fitted.width())/2;
                const int y = (rendered.height()-fitted.height())/2;
                const QColor red = rendered.pixelColor(x+fitted.width()/4, y+fitted.height()/4);
                const QColor green = rendered.pixelColor(x+3*fitted.width()/4, y+3*fitted.height()/4);
                valid = red.red() > 240 && red.green() < 15 && red.blue() < 15 &&
                        green.green() > 240 && green.red() < 15 && green.blue() < 15;
            }
            const int option = app.arguments().indexOf("--smoke-test");
            if (option+1 < app.arguments().size())
                valid = window.grab().save(app.arguments().at(option+1)) && valid;
            app.exit(valid ? 0 : 1);
        });
    }
    return app.exec();
}
