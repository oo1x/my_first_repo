#ifndef CAMERA_WORKER_H
#define CAMERA_WORKER_H

#include <QObject>
#include <QImage>
#include <QTimer>
#include <QAtomicInt>
#include <opencv2/opencv.hpp>

#include "v4l2_mmap_camera.h"

class CameraWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker();

public slots:
    void startWork();
    void stopWork();
    void captureFrame();
    void onFrameConsumed();
    void setFacePayEnabled(bool enabled);

signals:
    void frameReady(const QImage &image);
    void errorOccurred(const QString &msg);
    void facePayFrameReady(const QImage &image);

private:
    QImage matToQImage(const cv::Mat &mat);

private:
    V4L2MmapCapture m_camera;
    QTimer *m_timer = nullptr;
    bool m_running = false;

    // 0: 没有待消费帧
    // 1: 已经发出一帧，等主线程消费
    QAtomicInt m_framePending {0};

    // 刷脸支付检测状态
    bool m_facePayEnabled = false;
    int  m_faceStableCount = 0;
    bool m_facePayTriggered = false;

    cv::CascadeClassifier m_faceCascade;
};

#endif // CAMERA_WORKER_H
