#include "camera.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <chrono>
#include <cmath>
#include <future>
#include <stdexcept>

Camera::Camera(int device, Board board, Calibration calibration) : board(board), device(device)
{
    if (calibration.valid()) {
        imageSize = calibration.imageSize;
        state.calibration = std::move(calibration);
        state.calibrationRevision = 1;
        state.message = "Calibración cargada. Usa la misma cámara y enfoque/zoom; inicia el vídeo para comprobar la resolución.";
    }
}

Camera::~Camera()
{
    requestInterruption();
    wait();
}

CameraState Camera::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}

void Camera::run()
{
    using Clock = std::chrono::steady_clock;
    std::future<Calibration> pending;
    auto message = [this](const QString& text) {
        std::lock_guard<std::mutex> lock(mutex);
        state.message = text;
    };
    auto finishCalibration = [&] {
        try {
            auto result = pending.get();
            std::lock_guard<std::mutex> lock(mutex);
            state.calibration = std::move(result);
            ++state.calibrationRevision;
            state.message = "Calibración lista. Revisa el RMS y guarda el resultado.";
        } catch (const std::exception& error) {
            message(QString("No se pudo calibrar: %1").arg(error.what()));
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.calibrating = false;
    };
    captureRequested = false;
    calibrationRequested = false;
    try {
        board.points();
        message("Abriendo cámara…");
        cv::VideoCapture capture(device);
        if (!capture.isOpened())
            throw std::runtime_error("No se pudo abrir la cámara. Revisa el índice y los permisos.");
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        if (imageSize.area() > 0) {
            capture.set(cv::CAP_PROP_FRAME_WIDTH, imageSize.width);
            capture.set(cv::CAP_PROP_FRAME_HEIGHT, imageSize.height);
        }
        message(snapshot().calibration.valid() ?
            "Calibración disponible. Usa la misma cámara y enfoque/zoom. Activa la realidad aumentada para ver la pose." :
            "Muestra el tablero completo y captura distintas posiciones e inclinaciones.");
        auto lastRate = Clock::now();
        int frames = 0;
        while (!isInterruptionRequested()) {
            if (pending.valid() && pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                finishCalibration();
            cv::Mat frame, gray;
            if (!capture.read(frame) || frame.empty())
                throw std::runtime_error("La cámara dejó de enviar imágenes. Revisa la conexión y reintenta.");
            if (imageSize.area() == 0)
                imageSize = frame.size();
            if (frame.size() != imageSize)
                throw std::runtime_error("La resolución cambió. Inicia una nueva sesión para recalibrar.");
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            std::vector<cv::Point2f> corners;
            const bool found = cv::findChessboardCorners(gray, {board.columns, board.rows}, corners,
                cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
            if (found)
                cv::cornerSubPix(gray, corners, {11,11}, {-1,-1},
                    {cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.1});
            if (captureRequested.exchange(false) && !pending.valid()) {
                bool duplicate = false;
                if (found) {
                    // shortcut: solo filtra desplazamiento de esquinas, evaluar cobertura para uso metrológico.
                    for (const auto& view : views)
                        if (cv::norm(view, corners, cv::NORM_L2) / std::sqrt(corners.size()) <
                            0.015 * std::hypot(frame.cols, frame.rows))
                            duplicate = true;
                }
                if (!found)
                    message("No se capturó: el tablero debe verse completo y enfocado.");
                else if (duplicate)
                    message("Vista muy similar. Mueve o inclina el tablero antes de capturar.");
                else {
                    views.push_back(corners);
                    pose = false;
                    std::lock_guard<std::mutex> lock(mutex);
                    state.samples = static_cast<int>(views.size());
                    state.calibration = {};
                    state.message = "Vista capturada. Cambia de ángulo y posición para la siguiente.";
                }
            }
            if (calibrationRequested.exchange(false) && !pending.valid()) {
                if (views.size() < minimumViews)
                    message("Necesitas al menos 8 vistas diferentes para calibrar.");
                else {
                    pending = std::async(std::launch::async, calibrate, board, imageSize, views);
                    std::lock_guard<std::mutex> lock(mutex);
                    state.calibrating = true;
                    state.message = "Calibrando… Puedes seguir viendo el vídeo.";
                }
            }
            const auto calibration = snapshot().calibration;
            double distance = 0;
            if (found) {
                if (pose && calibration.valid())
                    drawPose(frame, board, calibration, corners, distance);
                cv::drawChessboardCorners(frame, {board.columns, board.rows}, corners, true);
            }
            ++frames;
            const double elapsed = std::chrono::duration<double>(Clock::now() - lastRate).count();
            std::lock_guard<std::mutex> lock(mutex);
            if (elapsed >= 0.5) {
                state.fps = frames / elapsed;
                frames = 0;
                lastRate = Clock::now();
            }
            // Los fotogramas publicados son inmutables; la UI toma solo el último.
            state.frame = frame.isContinuous() ? frame : frame.clone();
            ++state.sequence;
            state.connected = true;
            state.pattern = found;
            state.distanceMm = distance;
        }
        message("Cámara detenida. Las capturas se conservan en esta sesión.");
    } catch (const std::exception& error) {
        message(QString::fromUtf8(error.what()));
    }
    if (pending.valid())
        finishCalibration();
    std::lock_guard<std::mutex> lock(mutex);
    state.connected = false;
    state.pattern = false;
    state.distanceMm = 0;
    state.fps = 0;
}
