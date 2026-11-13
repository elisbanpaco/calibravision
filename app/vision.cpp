#include "vision.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <utility>

const char* Board::key() const
{
    switch (pattern) {
    case Pattern::Chessboard: return "chessboard";
    case Pattern::AsymmetricCircles: return "asymmetric_circles";
    case Pattern::Charuco: return "charuco";
    }
    throw std::invalid_argument("Tipo de patrón no compatible.");
}

std::vector<cv::Point3f> Board::points() const
{
    key();
    if (columns < 3 || columns > 30 || rows < 3 || rows > 30 ||
        !std::isfinite(squareMm) || squareMm <= 0 || squareMm > 1000)
        throw std::invalid_argument("Dimensiones del tablero inválidas.");
    if (pattern == Pattern::Charuco) {
        if (!std::isfinite(markerMm) || markerMm < 0.1f || markerMm >= squareMm ||
            columns*rows/2 > 100 || (columns-1)*(rows-1) < 6)
            throw std::invalid_argument("ChArUco: al menos 6 esquinas, máximo 100 marcadores y marcador menor que el cuadrado.");
        return cv::aruco::CharucoBoard({columns, rows}, squareMm, markerMm,
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_5X5_100)).getChessboardCorners();
    }
    std::vector<cv::Point3f> result;
    result.reserve(columns * rows);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
            result.emplace_back((pattern == Pattern::AsymmetricCircles ? 2*x+y%2 : x) * squareMm,
                                y * squareMm, 0);
    return result;
}

bool View::usable() const
{
    if (image.size() < 6 || image.size() != object.size()) return false;
    std::vector<cv::Point2f> plane;
    for (std::size_t i = 0; i < image.size(); ++i) {
        if (!std::isfinite(image[i].x) || !std::isfinite(image[i].y) ||
            !std::isfinite(object[i].x) || !std::isfinite(object[i].y) || object[i].z != 0)
            return false;
        plane.emplace_back(object[i].x, object[i].y);
    }
    std::vector<cv::Point2f> hull;
    cv::convexHull(plane, hull);
    return cv::contourArea(hull) > 1e-6;
}

PatternDetector::PatternDetector(Board board) : board(board), object(board.points())
{
    if (board.pattern == Pattern::Charuco)
        charuco = cv::makePtr<cv::aruco::CharucoDetector>(cv::aruco::CharucoBoard(
            cv::Size(board.columns, board.rows), board.squareMm, board.markerMm,
            cv::aruco::getPredefinedDictionary(cv::aruco::DICT_5X5_100)));
    if (board.pattern == Pattern::AsymmetricCircles) {
        cv::SimpleBlobDetector::Params parameters;
        parameters.minArea = 16;
        parameters.maxArea = 100000;
        blobs = cv::SimpleBlobDetector::create(parameters);
    }
}

View PatternDetector::detect(const cv::Mat& gray) const
{
    View view;
    if (charuco) {
        std::vector<int> ids;
        charuco->detectBoard(gray, view.image, ids);
        for (int id : ids) view.object.push_back(object.at(id));
        return view;
    }
    const cv::Size size(board.columns, board.rows);
    const bool found = blobs ?
        cv::findCirclesGrid(gray, size, view.image, cv::CALIB_CB_ASYMMETRIC_GRID, blobs) :
        cv::findChessboardCorners(gray, size, view.image, cv::CALIB_CB_ADAPTIVE_THRESH |
            cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
    if (!found) return {};
    if (!blobs)
        cv::cornerSubPix(gray, view.image, {11,11}, {-1,-1},
            {cv::TermCriteria::EPS | cv::TermCriteria::COUNT,30,0.1});
    view.object = object;
    return view;
}

bool similarView(const View& a, const View& b, cv::Size size)
{
    if (!a.usable() || !b.usable()) return false;
    double squared = 0;
    std::size_t common = 0;
    for (std::size_t i = 0; i < a.object.size(); ++i) {
        const auto match = std::find(b.object.begin(), b.object.end(), a.object[i]);
        if (match == b.object.end()) continue;
        const auto delta = a.image[i] - b.image[std::distance(b.object.begin(), match)];
        squared += delta.dot(delta);
        ++common;
    }
    // shortcut: compara solo desplazamiento de puntos comunes, evaluar cobertura para uso metrológico.
    return common >= 6 && common >= 0.8*std::max(a.object.size(), b.object.size()) &&
           std::sqrt(squared/common) < 0.015*std::hypot(size.width,size.height);
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
        if (!file["patternType"].empty()) {
            const auto type = file["patternType"].string();
            if (type == "asymmetric_circles") result.board.pattern = Pattern::AsymmetricCircles;
            else if (type == "charuco") result.board.pattern = Pattern::Charuco;
            else if (type != "chessboard") throw std::invalid_argument("Tipo de patrón no compatible.");
        }
        auto& c = result.calibration;
        c.imageSize = {integer("imageWidth",1,32768), integer("imageHeight",1,32768)};
        result.board.columns = integer("boardColumns",3,30);
        result.board.rows = integer("boardRows",3,30);
        const double square = number("squareSizeMm");
        if (square < 0.1 || square > 1000)
            throw std::invalid_argument("El lado del cuadrado está fuera de rango.");
        result.board.squareMm = static_cast<float>(square);
        if (result.board.pattern == Pattern::Charuco) {
            result.board.markerMm = static_cast<float>(number("markerSizeMm"));
            if (file["arucoDictionary"].string() != "DICT_5X5_100")
                throw std::invalid_argument("El diccionario ChArUco debe ser DICT_5X5_100.");
        }
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
    yaml << "patternType" << board.key();
    if (board.pattern == Pattern::Charuco)
        yaml << "markerSizeMm" << board.markerMm << "arucoDictionary" << "DICT_5X5_100";
    return yaml.releaseAndGetString();
}

Calibration calibrate(const Board& board, cv::Size size,
                      const std::vector<View>& views)
{
    board.points();
    if (size.width <= 0 || size.height <= 0 || views.size() < minimumViews)
        throw std::invalid_argument("Captura al menos 8 vistas antes de calibrar.");
    std::vector<std::vector<cv::Point3f>> object;
    std::vector<std::vector<cv::Point2f>> image;
    for (const auto& view : views) {
        if (!view.usable())
            throw std::invalid_argument("Cada vista necesita al menos 6 puntos válidos no colineales.");
        object.push_back(view.object);
        image.push_back(view.image);
    }
    Calibration result;
    result.imageSize = size;
    result.samples = static_cast<int>(views.size());
    std::vector<cv::Mat> rotations, translations;
    result.rms = cv::calibrateCamera(
        object, image, size,
        result.cameraMatrix, result.distCoeffs, rotations, translations);
    if (!result.valid())
        throw std::runtime_error("La calibración no es válida. Repite las capturas.");
    return result;
}

bool drawPose(cv::Mat& frame, const Board& board, const Calibration& calibration,
              const View& view, double& distanceMm, Figure figure, bool axes)
{
    if (!calibration.valid() || frame.size() != calibration.imageSize ||
        !view.usable())
        return false;
    cv::Mat rotation, translation;
    if (!cv::solvePnP(view.object, view.image, calibration.cameraMatrix, calibration.distCoeffs,
                      rotation, translation, false, cv::SOLVEPNP_SQPNP) ||
        !cv::checkRange(translation) || translation.at<double>(2) <= 0)
        return false;
    const auto points = board.points();
    float x = 0, y = 0;
    for (const auto& point : points) { x = std::max(x, point.x); y = std::max(y, point.y); }
    if (board.pattern == Pattern::Charuco) { x += board.squareMm; y += board.squareMm; }
    const float z = -3 * board.squareMm;
    std::vector<cv::Point3f> vertices;
    std::vector<std::pair<int,int>> edges;
    switch (figure) {
    case Figure::Prism:
        vertices = {{0,0,0},{x,0,0},{x,y,0},{0,y,0},
                    {0,0,z},{x,0,z},{x,y,z},{0,y,z}};
        for (int i = 0; i < 4; ++i) {
            edges.emplace_back(i,(i+1)%4);
            edges.emplace_back(i+4,(i+1)%4+4);
            edges.emplace_back(i,i+4);
        }
        break;
    case Figure::Pyramid:
        vertices = {{0,0,0},{x,0,0},{x,y,0},{0,y,0},{x/2,y/2,z}};
        for (int i = 0; i < 4; ++i) {
            edges.emplace_back(i,(i+1)%4);
            edges.emplace_back(i,4);
        }
        break;
    case Figure::Cylinder:
    case Figure::Cone: {
        constexpr int segments = 48;
        const bool cone = figure == Figure::Cone;
        const float radius = std::min(x,y)/2;
        for (int ring = 0; ring < (cone ? 1 : 2); ++ring)
            for (int i = 0; i < segments; ++i) {
                const double angle = 2*CV_PI*i/segments;
                vertices.emplace_back(x/2+radius*std::cos(angle), y/2+radius*std::sin(angle), ring*z);
            }
        if (cone) vertices.emplace_back(x/2,y/2,z);
        for (int i = 0; i < segments; ++i) {
            edges.emplace_back(i,(i+1)%segments);
            if (!cone) edges.emplace_back(i+segments,(i+1)%segments+segments);
            if (i%(segments/4) == 0) edges.emplace_back(i,cone ? segments : i+segments);
        }
        break;
    }
    case Figure::Sphere:
    case Figure::Torus: {
        constexpr int segments = 48, bands = 12;
        const bool sphere = figure == Figure::Sphere;
        const int rings = sphere ? bands+1 : bands;
        const float radius = sphere ? std::min(-z/2,std::min(x,y)/2) : std::min(x,y)/2;
        for (int j = 0; j < rings; ++j) {
            const double latitude = (sphere ? CV_PI : 2*CV_PI)*j/bands;
            const double ringRadius = sphere ? radius*std::sin(latitude) : radius*(2+std::cos(latitude))/3;
            const double height = sphere ? -radius+radius*std::cos(latitude) : radius*(-1+std::sin(latitude))/3;
            for (int i = 0; i < segments; ++i) {
                const double longitude = 2*CV_PI*i/segments;
                vertices.emplace_back(x/2+ringRadius*std::cos(longitude),
                                      y/2+ringRadius*std::sin(longitude), height);
                if (j%2 == 0 && (!sphere || (j > 0 && j < bands)))
                    edges.emplace_back(j*segments+i,j*segments+(i+1)%segments);
                if (i%(segments/8) == 0 && (!sphere || j < bands))
                    edges.emplace_back(j*segments+i,((j+1)%rings)*segments+i);
            }
        }
        break;
    }
    default:
        return false;
    }
    std::vector<cv::Point2f> projected;
    cv::projectPoints(vertices, rotation, translation, calibration.cameraMatrix,
                      calibration.distCoeffs, projected);
    for (const auto& point : projected)
        if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
            std::abs(point.x) > 1e6 || std::abs(point.y) > 1e6)
            return false;
    for (const auto& edge : edges)
        cv::line(frame, projected[edge.first], projected[edge.second], {0,200,255}, 2, cv::LINE_AA);
    if (axes)
        cv::drawFrameAxes(frame, calibration.cameraMatrix, calibration.distCoeffs,
                          rotation, translation, 3 * board.squareMm, 2);
    distanceMm = cv::norm(translation);
    return true;
}
