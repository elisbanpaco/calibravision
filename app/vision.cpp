#include "vision.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>

std::vector<cv::Point3f> Board::points() const
{
    if (columns < 3 || columns > 30 || rows < 3 || rows > 30 ||
        !std::isfinite(squareMm) || squareMm <= 0 || squareMm > 1000)
        throw std::invalid_argument("Dimensiones del tablero inválidas.");
    std::vector<cv::Point3f> result;
    result.reserve(columns * rows);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
            result.emplace_back(x * squareMm, y * squareMm, 0);
    return result;
}

bool Calibration::valid() const
{
    return cameraMatrix.type() == CV_64F && cameraMatrix.rows == 3 &&
           cameraMatrix.cols == 3 && distCoeffs.type() == CV_64F &&
           (distCoeffs.rows == 1 || distCoeffs.cols == 1) &&
           (distCoeffs.total() == 4 || distCoeffs.total() == 5 || distCoeffs.total() == 8 ||
            distCoeffs.total() == 12 || distCoeffs.total() == 14) &&
           cv::checkRange(cameraMatrix) && cv::checkRange(distCoeffs) &&
           cameraMatrix.at<double>(0, 0) > 0 && cameraMatrix.at<double>(1, 1) > 0 &&
           std::abs(cameraMatrix.at<double>(2,0)) < 1e-9 &&
           std::abs(cameraMatrix.at<double>(2,1)) < 1e-9 &&
           std::abs(cameraMatrix.at<double>(2,2)-1) < 1e-9 &&
           imageSize.width > 0 && imageSize.height > 0 && std::isfinite(rms) && rms >= 0;
}

CalibrationFile readCalibration(const std::string& yaml)
{
    if (yaml.empty() || yaml.size() > 1024*1024)
        throw std::invalid_argument("El archivo está vacío o supera 1 MB.");
    try {
        cv::FileStorage file(yaml, cv::FileStorage::READ | cv::FileStorage::MEMORY);
        if (!file.isOpened())
            throw std::invalid_argument("No se pudo leer la calibración.");
        auto number = [&](const char* key) {
            const auto node = file[key];
            if ((!node.isInt() && !node.isReal()) || !std::isfinite(node.real()))
                throw std::invalid_argument(std::string("Falta o es inválido: ") + key +
                    ". Usa un archivo guardado desde Camera Lab con sus metadatos.");
            return node.real();
        };
        auto integer = [&](const char* key, int minimum, int maximum) {
            const double value = number(key);
            if (value < minimum || value > maximum || std::floor(value) != value)
                throw std::invalid_argument(std::string("Valor fuera de rango: ") + key);
            return static_cast<int>(value);
        };
        CalibrationFile result;
        auto& c = result.calibration;
        c.imageSize = {integer("imageWidth",1,32768), integer("imageHeight",1,32768)};
        result.board.columns = integer("boardColumns",3,30);
        result.board.rows = integer("boardRows",3,30);
        const double square = number("squareSizeMm");
        if (square < 0.1 || square > 1000)
            throw std::invalid_argument("El lado del cuadrado está fuera de rango.");
        result.board.squareMm = static_cast<float>(square);
        result.board.points();
        c.samples = integer("samples",1,100000);
        c.rms = number("RMS");
        if (!file["cameraIndex"].empty())
            result.cameraIndex = integer("cameraIndex",0,99);
        auto matrix = [&](const char* key, bool intrinsic) {
            const auto node = file[key];
            if (!node.isMap() || !node["rows"].isInt() || !node["cols"].isInt())
                throw std::invalid_argument(std::string("Matriz inválida: ") + key);
            const int rows = static_cast<int>(node["rows"]);
            const int cols = static_cast<int>(node["cols"]);
            if (rows < 1 || rows > 14 || cols < 1 || cols > 14 ||
                (intrinsic ? rows != 3 || cols != 3 : rows != 1 && cols != 1) ||
                node["dt"].string() != "d" || !node["data"].isSeq() ||
                node["data"].size() != static_cast<std::size_t>(rows*cols))
                throw std::invalid_argument(std::string("Formato o dimensiones inválidas: ") + key);
            cv::Mat value;
            node >> value;
            return value;
        };
        c.cameraMatrix = matrix("cameraMatrix", true);
        c.distCoeffs = matrix("distCoeffs", false);
        if (!c.valid())
            throw std::invalid_argument("Matrices, distorsión o RMS de calibración inválidos.");
        return result;
    } catch (const cv::Exception&) {
        throw std::invalid_argument("El archivo no contiene una calibración YAML de OpenCV válida.");
    }
}

std::string writeCalibration(const Calibration& c, const Board& board, int cameraIndex)
{
    board.points();
    if (!c.valid() || c.samples < 1 || cameraIndex < 0 || cameraIndex > 99)
        throw std::invalid_argument("No hay una calibración válida para guardar.");
    cv::FileStorage yaml("calibration.yml", cv::FileStorage::WRITE | cv::FileStorage::MEMORY);
    yaml << "cameraMatrix" << c.cameraMatrix << "distCoeffs" << c.distCoeffs << "RMS" << c.rms
         << "imageWidth" << c.imageSize.width << "imageHeight" << c.imageSize.height
         << "boardColumns" << board.columns << "boardRows" << board.rows
         << "squareSizeMm" << board.squareMm << "samples" << c.samples << "cameraIndex" << cameraIndex;
    return yaml.releaseAndGetString();
}

Calibration calibrate(const Board& board, cv::Size size,
                      const std::vector<std::vector<cv::Point2f>>& views)
{
    const auto points = board.points();
    if (size.width <= 0 || size.height <= 0 || views.size() < minimumViews)
        throw std::invalid_argument("Captura al menos 8 vistas antes de calibrar.");
    for (const auto& view : views) {
        if (view.size() != points.size())
            throw std::invalid_argument("Una vista no contiene el tablero completo.");
        for (const auto& point : view)
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                throw std::invalid_argument("Una vista contiene coordenadas inválidas.");
    }
    Calibration result;
    result.imageSize = size;
    result.samples = static_cast<int>(views.size());
    std::vector<cv::Mat> rotations, translations;
    result.rms = cv::calibrateCamera(
        std::vector<std::vector<cv::Point3f>>(views.size(), points), views, size,
        result.cameraMatrix, result.distCoeffs, rotations, translations);
    if (!result.valid())
        throw std::runtime_error("La calibración no es válida. Repite las capturas.");
    return result;
}

bool drawPose(cv::Mat& frame, const Board& board, const Calibration& calibration,
              const std::vector<cv::Point2f>& corners, double& distanceMm)
{
    const auto object = board.points();
    if (!calibration.valid() || frame.size() != calibration.imageSize ||
        corners.size() != object.size())
        return false;
    cv::Mat rotation, translation;
    if (!cv::solvePnP(object, corners, calibration.cameraMatrix, calibration.distCoeffs,
                      rotation, translation, false, cv::SOLVEPNP_SQPNP) ||
        !cv::checkRange(translation) || translation.at<double>(2) <= 0)
        return false;
    const float x = (board.columns - 1) * board.squareMm;
    const float y = (board.rows - 1) * board.squareMm;
    const float z = -3 * board.squareMm;
    const std::vector<cv::Point3f> cube = {
        {0,0,0}, {x,0,0}, {x,y,0}, {0,y,0},
        {0,0,z}, {x,0,z}, {x,y,z}, {0,y,z}};
    std::vector<cv::Point2f> projected;
    cv::projectPoints(cube, rotation, translation, calibration.cameraMatrix,
                      calibration.distCoeffs, projected);
    for (const auto& point : projected)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            std::abs(point.x) > 1e6 || std::abs(point.y) > 1e6)
            return false;
    for (int i = 0; i < 4; ++i) {
        cv::line(frame, projected[i], projected[(i+1)%4], {0,200,255}, 2, cv::LINE_AA);
        cv::line(frame, projected[i+4], projected[(i+1)%4+4], {0,200,255}, 2, cv::LINE_AA);
        cv::line(frame, projected[i], projected[i+4], {0,200,255}, 2, cv::LINE_AA);
    }
    cv::drawFrameAxes(frame, calibration.cameraMatrix, calibration.distCoeffs,
                      rotation, translation, 3 * board.squareMm, 2);
    distanceMm = cv::norm(translation);
    return true;
}
