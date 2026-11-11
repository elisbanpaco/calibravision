#pragma once

#include "vision.h"
#include <QThread>
#include <QString>
#include <atomic>
#include <cstdint>
#include <mutex>

struct CameraState {
    cv::Mat frame;
    Calibration calibration;
    std::uint64_t sequence = 0;
    std::uint64_t calibrationRevision = 0;
    int samples = 0;
    bool connected = false;
    bool pattern = false;
    bool calibrating = false;
    double fps = 0;
    double distanceMm = 0;
    QString message = "Inicia la cámara para comenzar.";
};

class Camera : public QThread {
public:
    Camera(int device, Board board, Calibration calibration = {});
    ~Camera() override;
    CameraState snapshot() const;
    void captureView() { captureRequested = true; }
    void calibrateViews() { calibrationRequested = true; }
    void setPose(bool enabled) { pose = enabled; }
    const Board board;

protected:
    void run() override;

private:
    const int device;
    mutable std::mutex mutex;
    CameraState state;
    std::vector<std::vector<cv::Point2f>> views;
    cv::Size imageSize;
    std::atomic_bool captureRequested{false}, calibrationRequested{false}, pose{false};
};
