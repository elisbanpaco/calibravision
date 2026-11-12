#pragma once

#include <opencv2/core.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <opencv2/features2d.hpp>
#include <vector>
#include <string>

enum class Pattern { Chessboard, AsymmetricCircles, Charuco };

struct Board {
    int columns = 9;
    int rows = 6;
    float squareMm = 24.5f;
    Pattern pattern = Pattern::Chessboard;
    float markerMm = 15;
    std::vector<cv::Point3f> points() const;
    const char* key() const;
};

struct View {
    std::vector<cv::Point2f> image;
    std::vector<cv::Point3f> object;
    bool usable() const;
};

class PatternDetector {
public:
    explicit PatternDetector(Board board);
    View detect(const cv::Mat& gray) const;
private:
    Board board;
    std::vector<cv::Point3f> object;
    cv::Ptr<cv::aruco::CharucoDetector> charuco;
    cv::Ptr<cv::SimpleBlobDetector> blobs;
};

bool similarView(const View& a, const View& b, cv::Size imageSize);

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
                      const std::vector<View>& views);
bool drawPose(cv::Mat& frame, const Board& board, const Calibration& calibration,
              const View& view, double& distanceMm);
