// Programa para calibracion de camara con la biblioteca OpenCV

// Include Libraries
#include<opencv2/opencv.hpp>
#include<iostream>

// Namespace to nullify use of cv::function(); syntax
using namespace std;
using namespace cv;

void drawAxis(cv::Mat& image, const cv::Mat& cameraMatrix, const cv::Mat& distCoeffs, const cv::Mat& rvec, const cv::Mat& tvec, float scale = 1.f) 
{
    float scaleX = scale * 9.f;
    float scaleY = scale * 6.f;
    float scaleZ = scale * 6.f;

    std::vector<cv::Point3f> axisPoints = {
        {0.f, 0.f, 0.f}, {scaleX, 0.f, 0.f}, {0.f, scaleY, 0.f}, {0.f, 0.f, -scaleZ},
        {0.f, 0.f, -scaleZ}, {scaleX, 0.f, -scaleZ}, {0.f, scaleY, -scaleZ}, {scaleX, scaleY, -scaleZ}, {scaleX, scaleY, 0.f}
    };

    std::vector<cv::Point2f> imagePoints;
    cv::projectPoints(axisPoints, rvec, tvec, cameraMatrix, distCoeffs, imagePoints);

    cv::line(image, imagePoints[0], imagePoints[1], cv::Scalar(0, 0, 255), 2);
    cv::line(image, imagePoints[0], imagePoints[2], cv::Scalar(0, 255, 0), 2);
    cv::line(image, imagePoints[0], imagePoints[3], cv::Scalar(255, 0, 0), 2);

    cv::line(image, imagePoints[1], imagePoints[5], cv::Scalar(0, 127, 255), 4);
    cv::line(image, imagePoints[2], imagePoints[6], cv::Scalar(0, 127, 255), 4);
    cv::line(image, imagePoints[8], imagePoints[7], cv::Scalar(0, 127, 255), 4);

    cv::line(image, imagePoints[4], imagePoints[5], cv::Scalar(0, 255, 255), 2);
    cv::line(image, imagePoints[4], imagePoints[6], cv::Scalar(0, 255, 255), 2);
    cv::line(image, imagePoints[5], imagePoints[7], cv::Scalar(0, 255, 255), 2);
    cv::line(image, imagePoints[6], imagePoints[7], cv::Scalar(0, 255, 255), 2);

    cv::putText(image, "X", imagePoints[1], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 2);
    cv::putText(image, "Y", imagePoints[2], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 2);
    cv::putText(image, "Z", imagePoints[3], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 2);

    float distance = cv::norm(tvec);
    std::string strDistance = cv::format("%.2f mm", distance);

    cv::Vec2f dirCenter = imagePoints[7] - imagePoints[4];
    cv::Point2f center = imagePoints[4] + cv::Point2f(dirCenter[0], dirCenter[1]) * 0.25f;
  
    cv::putText(image, strDistance, center, cv::FONT_HERSHEY_DUPLEX, 0.75, cv::Scalar(255, 0, 0), 2);
}

int main()
{
    // initialize a video capture object
    VideoCapture vid_capture(
        // "D://OpenCV_Project_Test//data//BackgroundSubstraction_Medium.mp4"
        0
    );

    // Variables 
    double fps{0.0};
    int frameCount{0}, width{0}, height{0};

    // Print error message if the stream is invalid
    if (!vid_capture.isOpened())
    {
        cout << "Error opening video stream or file" << endl;
        return 0;
    }
    else
    {
        // Obtain fps and frame count by get() method and print
        fps = vid_capture.get(cv::CAP_PROP_FPS);
        frameCount = static_cast<int>(vid_capture.get(cv::CAP_PROP_FRAME_COUNT));
        width = static_cast<int>(vid_capture.get(cv::CAP_PROP_FRAME_WIDTH));
        height = static_cast<int>(vid_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        std::cout << "FPS: " << fps << "\nFrame count: " << frameCount << "\nResolution: " << width << "x" << height << std::endl;
    }

    float squareSize = 24.5f; //mm
    int chessboardWidth = 9;// 9;//chessboard cols  // 12; //symmetric circles cols // 11; //asymmetric circles cols 
    int chessboardHeight = 6;//6; //chessboard rows // 8; //symmetric circles rows // 4; //asymmetric circles rows

    cv::Mat cameraMatrix, distCoeffs;
    std::vector<cv::Mat> rvecs, tvecs;
    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;

    std::vector<cv::Point3f> obj;

    // Create object points coordinates
    for (int y = 0; y < chessboardHeight; ++y)
    {
        for (int x = 0; x < chessboardWidth; ++x)
        {
            obj.emplace_back(x * squareSize, y * squareSize, 0);
        }
    }

    // Grayscale image to apply pattern algorithms
    cv::Mat grayscale;

    // pattern count
    int chessboardCount = 0;
    bool isTakeChessboardPictureActive = false;
    bool isCameraCalibrationActive = false;
    bool isARDetectionActive = false;
    bool isCameraCalibrated = false;

    // Read the frames to the last frame
    while (vid_capture.isOpened() )
    {
        // Initialise frame matrix
        Mat frame, frame_01;

        // Initialize a boolean to check if frames are there or not
        bool isSuccess = vid_capture.read(frame);

        // If frames are not there, close it
        if (isSuccess == false)
        {
            cout << "Video camera is disconnected" << endl;
            break;
        }
        else
        {
            //Resize image
            //int width = 640;
            //int height = static_cast<int>(frame.rows * (width / static_cast<double>(frame.cols)));
            //resize(frame, frame, Size(width, height), 0, 0, INTER_LINEAR);

            cv::cvtColor(frame, grayscale, cv::COLOR_BGR2GRAY);
            std::vector<cv::Point2f> corners;
            bool foundPattern{ false };


            foundPattern = cv::findChessboardCorners(
                grayscale, 
                cv::Size(chessboardWidth, chessboardHeight), 
                corners,
                cv::CALIB_CB_ADAPTIVE_THRESH + cv::CALIB_CB_NORMALIZE_IMAGE + cv::CALIB_CB_FAST_CHECK);

            //foundPattern = cv::findCirclesGrid(
            //    grayscale,
            //    cv::Size(chessboardWidth, chessboardHeight),
            //    corners);

            //foundPattern = cv::findCirclesGrid(
            //    grayscale,
            //    cv::Size(chessboardWidth, chessboardHeight),
            //    corners, CALIB_CB_ASYMMETRIC_GRID);

            if (foundPattern) 
            {
                cv::cornerSubPix(grayscale, corners, cv::Size(11, 11), cv::Size(-1, -1),
                    cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

                if (isTakeChessboardPictureActive || isCameraCalibrationActive || isARDetectionActive)
                {
                    if (isTakeChessboardPictureActive) 
                    {
                        objectPoints.push_back(obj);
                        imagePoints.push_back(corners);
                        chessboardCount++;
                        std::cout << "Chessboard pattern " << chessboardCount << " captured." << std::endl;
                        isTakeChessboardPictureActive = false;
                    }
                    else if (isCameraCalibrationActive) 
                    {
                        if (!objectPoints.empty()) 
                        {
                            double rms = cv::calibrateCamera(objectPoints, imagePoints, frame.size(), cameraMatrix, distCoeffs, rvecs, tvecs);
                            std::cout << "Camera calibration completed. \nRMS: " << rms << "\nCamera Matrix:\n" << cameraMatrix << "\nDistortion Coefficients:\n" << distCoeffs << std::endl;
                            cv::FileStorage fs("calibration_results.yml", cv::FileStorage::WRITE);
                            fs << "cameraMatrix" << cameraMatrix << "distCoeffs" << distCoeffs << "RMS" << rms;
                            isCameraCalibrated = true;
                        }
                        else 
                        {
                            std::cerr << "Camera calibration aborted. No valid calibration images captured." << std::endl;
                        }
                        isCameraCalibrationActive = false;
                    }
                    else if (isARDetectionActive && isCameraCalibrated) 
                    {
                        if (!corners.empty()) 
                        {
                            cv::Mat rvec, tvec;
                            bool isPatternRescueMetrics = cv::solvePnP(objectPoints[0], corners, cameraMatrix, distCoeffs, rvec, tvec, true, cv::SOLVEPNP_SQPNP);
                            if (isPatternRescueMetrics) 
                            {
                                drawAxis(frame, cameraMatrix, distCoeffs, rvec, tvec, squareSize);
                            }
                        }
                    }
                }
                // draw chessboard corners
                cv::drawChessboardCorners(frame, cv::Size(chessboardWidth, chessboardHeight), corners, foundPattern);
            }
            //display frames
            imshow("Frame", frame);
        }

        //wait 20 ms between successive frames and break the loop if key q is pressed
        int key = cv::waitKey(20);
        if (key == 'q' || key == 'Q') 
        {
            std::cout << "Quit key pressed. Exiting..." << std::endl;
            break;
        }
        else if (key == 't' || key == 'T') 
        {
            isTakeChessboardPictureActive = !isTakeChessboardPictureActive;
            std::cout << "Chessboard pattern detection " << (isTakeChessboardPictureActive ? "activated." : "deactivated.") << std::endl;
        }
        else if (key == 'p' || key == 'P') 
        {
            isCameraCalibrationActive = !isCameraCalibrationActive;
            std::cout << "Camera calibration " << (isCameraCalibrationActive ? "activated." : "deactivated.") << std::endl;
        }
        else if (key == 'a' || key == 'A') 
        {
            isARDetectionActive = !isARDetectionActive;
            std::cout << "Augmented reality " << (isARDetectionActive ? "activated." : "deactivated.") << std::endl;
        }
    }

    // Release the video capture object
    vid_capture.release();
    destroyAllWindows();
    return 0;
}

