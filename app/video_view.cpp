#include "video_view.h"

#include <QOpenGLContext>
#include <QPainter>

VideoView::VideoView(QWidget* parent) : QOpenGLWidget(parent)
{
    setMinimumSize(400, 300);
    setAccessibleName("Vídeo de la cámara y detección del tablero");
}

VideoView::~VideoView() { cleanup(); }

void VideoView::setFrame(const cv::Mat& image)
{
    frame = image;
    dirty = true;
    update();
}

void VideoView::cleanup()
{
    if (!context())
        return;
    makeCurrent();
    program.reset();
    if (texture) glDeleteTextures(1, &texture);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    texture = vbo = vao = 0;
    textureSize = {};
    doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, nullptr);
}

void VideoView::initializeGL()
{
    error.clear();
    if (!initializeOpenGLFunctions()) {
        error = "Se necesita OpenGL 3.3. Revisa el controlador gráfico.";
        return;
    }
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] { cleanup(); }, Qt::DirectConnection);
    program = std::make_unique<QOpenGLShaderProgram>();
    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex,
            "#version 330 core\nlayout(location=0) in vec2 position;"
            "layout(location=1) in vec2 uv; out vec2 texCoord;"
            "void main(){texCoord=uv;gl_Position=vec4(position,0,1);}") ||
        !program->addShaderFromSourceCode(QOpenGLShader::Fragment,
            "#version 330 core\nin vec2 texCoord;out vec4 color;uniform sampler2D image;"
            "void main(){color=vec4(texture(image,texCoord).bgr,1);}") || !program->link()) {
        error = "No se pudo preparar el vídeo: " + program->log();
        program.reset();
        return;
    }
    const float vertices[] = {-1,-1,0,1, 1,-1,1,1, -1,1,0,0, 1,1,1,0};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float),
                          reinterpret_cast<void*>(2*sizeof(float)));
    glBindVertexArray(0);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    dirty = true;
}

void VideoView::paintGL()
{
    if (!error.isEmpty() || frame.empty()) {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#111b2a"));
        painter.setPen(QColor("#cbd5e1"));
        painter.drawText(rect().adjusted(32,32,-32,-32), Qt::AlignCenter | Qt::TextWordWrap,
                         error.isEmpty() ? "Vista de cámara\nInicia una sesión para ver el vídeo." : error);
        return;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.067f, 0.106f, 0.165f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (dirty) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (textureSize != frame.size()) {
            textureSize = frame.size();
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, frame.cols, frame.rows, 0,
                          GL_RGB, GL_UNSIGNED_BYTE, frame.data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.cols, frame.rows,
                             GL_RGB, GL_UNSIGNED_BYTE, frame.data);
        }
        dirty = false;
    }
    const QSize surface(qRound(width()*devicePixelRatioF()), qRound(height()*devicePixelRatioF()));
    QSize fitted(frame.cols, frame.rows);
    fitted.scale(surface, Qt::KeepAspectRatio);
    glViewport((surface.width()-fitted.width())/2, (surface.height()-fitted.height())/2,
               fitted.width(), fitted.height());
    program->bind();
    program->setUniformValue("image", 0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    program->release();
}
