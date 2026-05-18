#include "camera_worker.h"

#include <QDebug>

CameraWorker::CameraWorker(QObject *parent)
    : QObject(parent)
{
}

CameraWorker::~CameraWorker()
{
    stopWork();
}

void CameraWorker::startWork()
{
    if (m_running) {
        return;
    }

    // 这里按你实际摄像头节点改
    if (!m_camera.openDevice("/dev/video1", 640, 480)) {
        emit errorOccurred("打开摄像头失败");
        return;
    }

    if (!m_faceCascade.load("./haarcascade_frontalface_alt2.xml")) {
        m_camera.closeDevice();
        emit errorOccurred("加载人脸分类器失败");
        return;
    }

    m_running = true;
    m_facePayEnabled = false;
    m_faceStableCount = 0;
    m_facePayTriggered = false;
    m_framePending.storeRelease(0);

    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &CameraWorker::captureFrame);
    }

    m_timer->start(30);
    qDebug() << "CameraWorker started";
}

void CameraWorker::stopWork()
{
    m_running = false;

    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    if (m_camera.isOpened()) {
        m_camera.closeDevice();
    }

    m_facePayEnabled = false;
    m_faceStableCount = 0;
    m_facePayTriggered = false;
    m_framePending.storeRelease(0);

    qDebug() << "CameraWorker stopped";
}

void CameraWorker::setFacePayEnabled(bool enabled)
{
    m_facePayEnabled = enabled;

    if (!enabled) {
        m_faceStableCount = 0;
        m_facePayTriggered = false;
    }

    qDebug() << "setFacePayEnabled =" << enabled;
}

void CameraWorker::onFrameConsumed()
{
    m_framePending.storeRelease(0);
}

void CameraWorker::captureFrame()
{
    if (!m_running) {
        return;
    }

    // 主线程还没消费完上一帧，直接丢掉本次，避免堆积
    if (m_framePending.loadAcquire() != 0) {
        return;
    }

    cv::Mat bgrFrame;
    if (!m_camera.readFrame(bgrFrame) || bgrFrame.empty()) {
        return;
    }

    QImage img = matToQImage(bgrFrame);
    if (img.isNull()) {
        return;
    }

    // 先发给主线程显示
    m_framePending.storeRelease(1);
    emit frameReady(img);

    // 没开刷脸支付 or 已经触发过本轮支付，就不做检测
    if (!m_facePayEnabled || m_facePayTriggered) {
        return;
    }

    // 子线程里做人脸检测，避免主线程卡顿
    cv::Mat gray;
    cv::cvtColor(bgrFrame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    m_faceCascade.detectMultiScale(gray,
                                   faces,
                                   1.1,
                                   3,
                                   0,
                                   cv::Size(80, 80));

    if (!faces.empty()) {
        m_faceStableCount++;
        qDebug() << "face detected, stable count =" << m_faceStableCount;

        // 连续3帧检测到人脸，认为稳定
        if (m_faceStableCount >= 3) {
            m_facePayTriggered = true;
            m_faceStableCount = 0;

            emit facePayFrameReady(img);
        }
    } else {
        m_faceStableCount = 0;
    }
}

QImage CameraWorker::matToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return QImage();
    }

    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data,
                     rgb.cols,
                     rgb.rows,
                     static_cast<int>(rgb.step),
                     QImage::Format_RGB888);
        return image.copy();
    }

    if (mat.type() == CV_8UC1) {
        QImage image(mat.data,
                     mat.cols,
                     mat.rows,
                     static_cast<int>(mat.step),
                     QImage::Format_Grayscale8);
        return image.copy();
    }

    return QImage();
}
