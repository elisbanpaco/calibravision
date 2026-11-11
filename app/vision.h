#pragma once

#include <opencv2/core.hpp>
#include <vector>
#include <string>

struct Board {
    int columns = 9;
    int rows = 6;
    float squareMm = 24.5f;
    std::vector<cv::Point3f> points() const;
};

struct Calibration {
    cv::Mat cameraMatrix, distCoeffs;
    cv::Size imageSize;
    double rms = 0;
    int samples = 0;
    bool valid() const;
};

struct CalibrationFile {
    Board board;
    Calibration calibration;
    int cameraIndex = -1;
};

CalibrationFile readCalibration(const std::string& yaml);
std::string writeCalibration(const Calibration& calibration, const Board& board, int cameraIndex);

constexpr int minimumViews = 8;
Calibration calibrate(const Board& board, cv::Size size,
                      const std::vector<std::vector<cv::Point2f>>& views);
bool drawPose(cv::Mat& frame, const Board& board, const Calibration& calibration,
              const std::vector<cv::Point2f>& corners, double& distanceMm);
