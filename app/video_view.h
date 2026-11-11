#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <opencv2/core.hpp>
#include <memory>

class VideoView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit VideoView(QWidget* parent = nullptr);
    ~VideoView() override;
    void setFrame(const cv::Mat& image);
    QString error;

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void cleanup();
    cv::Mat frame;
    bool dirty = false;
    GLuint texture = 0, vao = 0, vbo = 0;
    cv::Size textureSize;
    std::unique_ptr<QOpenGLShaderProgram> program;
};
