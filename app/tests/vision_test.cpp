#include "vision.h"
#include "camera.h"

#include <opencv2/calib3d.hpp>
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class F> void rejects(F action)
{
    try { action(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Se aceptaron datos inválidos.");
}

int main()
{
    try {
        const Board board;
        const auto object = board.points();
        check(object.size() == 54, "Número de esquinas incorrecto.");
        check(object.back() == cv::Point3f(196,122.5f,0), "Escala física incorrecta.");
        rejects([] { Board{2,6,24.5f}.points(); });
        rejects([] { Board{9,6,0}.points(); });
        rejects([] { Board{9,6,std::numeric_limits<float>::quiet_NaN()}.points(); });
        rejects([&] { calibrate(board, {640,480}, {}); });

        const cv::Mat intrinsic = (cv::Mat_<double>(3,3) << 700,0,320, 0,710,240, 0,0,1);
        std::vector<std::vector<cv::Point2f>> views;
        for (int i = 0; i < 12; ++i) {
            std::vector<cv::Point2f> corners;
            cv::projectPoints(object,
                cv::Vec3d(-0.3 + 0.06*i, 0.2 - 0.035*i, 0.025*i),
                cv::Vec3d(-100 + 9*i, -70 + 4*(i%4), 570 + 24*(i%5)),
                intrinsic, cv::Mat::zeros(1,5,CV_64F), corners);
            views.push_back(corners);
        }
        auto invalid = views;
        invalid[0].pop_back();
        rejects([&] { calibrate(board, {640,480}, invalid); });
        invalid = views;
        invalid[0][0].x = std::numeric_limits<float>::infinity();
        rejects([&] { calibrate(board, {640,480}, invalid); });
        const auto result = calibrate(board, {640,480}, views);
        check(result.valid(), "Calibración sintética inválida.");
        check(result.rms < 0.001, "Error de reproyección excesivo.");
        check(cv::norm(result.cameraMatrix, intrinsic, cv::NORM_INF) < 0.1,
              "No se recuperaron los parámetros de cámara.");
        check(result.samples == 12, "Conteo de vistas incorrecto.");

        const auto yaml = writeCalibration(result, board, 2);
        const auto loaded = readCalibration(yaml);
        check(loaded.calibration.valid() && loaded.cameraIndex == 2,
              "No se pudo cargar la calibración guardada.");
        check(loaded.board.columns == 9 && loaded.board.rows == 6 && loaded.board.squareMm == 24.5f,
              "Se perdió la configuración física del tablero.");
        check(loaded.calibration.samples == 12 && loaded.calibration.imageSize == cv::Size(640,480),
              "Se perdieron metadatos de calibración.");
        check(cv::norm(result.cameraMatrix, loaded.calibration.cameraMatrix) < 1e-10 &&
              cv::norm(result.distCoeffs, loaded.calibration.distCoeffs) < 1e-10 &&
              std::abs(result.rms-loaded.calibration.rms) < 1e-10,
              "Los parámetros cambiaron al guardar y cargar.");
        Camera restored(2, loaded.board, loaded.calibration);
        const auto restoredState = restored.snapshot();
        check(restoredState.calibration.valid() && restoredState.calibrationRevision == 1 &&
              restoredState.samples == 0 && !restoredState.connected && restoredState.frame.empty(),
              "Cargar una calibración no debe inventar capturas ni iniciar la cámara.");
        Camera fresh(0, board);
        check(!fresh.snapshot().calibration.valid(), "Una sesión nueva aparece calibrada.");

        auto replace = [](std::string text, const std::string& from, const std::string& to) {
            const auto position = text.find(from);
            check(position != std::string::npos, "No se encontró el campo de prueba.");
            text.replace(position, from.size(), to);
            return text;
        };
        check(readCalibration(replace(yaml,"cameraIndex: 2\n","")).cameraIndex == -1,
              "No se admitió un archivo anterior de Camera Lab sin índice de cámara.");
        rejects([] { readCalibration(""); });
        rejects([] { readCalibration(std::string(1024*1024+1,'x')); });
        rejects([] { readCalibration("not a calibration"); });
        rejects([] { readCalibration("%YAML:1.0\n---\nRMS: 0.2\n"); });
        rejects([&] { readCalibration(replace(yaml,"imageWidth: 640","imageWidth: -1")); });
        rejects([&] { readCalibration(replace(yaml,"boardColumns: 9","boardColumns: 9.5")); });
        rejects([&] { readCalibration(replace(yaml,"squareSizeMm: 24.5","squareSizeMm: 0")); });
        rejects([&] { readCalibration(replace(yaml,"samples: 12","samples: 0")); });
        rejects([&] { readCalibration(replace(yaml,"cameraIndex: 2","cameraIndex: -1")); });
        rejects([&] { readCalibration(replace(yaml,"cols: 5","cols: 6")); });
        rejects([&] { readCalibration(replace(yaml,"rows: 3","rows: 999999999")); });
        auto invalidCalibration = result;
        invalidCalibration.distCoeffs = cv::Mat::zeros(1,6,CV_64F);
        check(!invalidCalibration.valid(), "Se aceptó una forma de distorsión no compatible.");
        rejects([&] { writeCalibration(invalidCalibration, board, 0); });

        cv::Mat image = cv::Mat::zeros(480,640,CV_8UC3);
        double distance = 0;
        check(drawPose(image, board, result, views[0], distance), "No se recuperó la pose.");
        check(std::abs(distance - cv::norm(cv::Vec3d(-100,-70,570))) < 0.1,
              "Distancia física incorrecta.");
        check(cv::countNonZero(image.reshape(1)) > 0, "No se dibujó la realidad aumentada.");
        check(!drawPose(image, board, {}, views[0], distance), "Se aceptó una cámara sin calibrar.");
        cv::Mat wrongSize = cv::Mat::zeros(240,320,CV_8UC3);
        check(!drawPose(wrongSize, board, result, views[0], distance), "Se aceptó otra resolución.");
        check(!drawPose(image, board, result, {}, distance), "Se aceptaron esquinas ausentes.");
        std::cout << "OK: calibración, guardado/carga, validación, sesión restaurada y pose.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
