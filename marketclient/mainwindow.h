#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QThread>
#include <QByteArray>
#include <QMap>
#include <QImage>
#include <QJsonObject>

#include <opencv2/opencv.hpp>

#include "rc522_app.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class CameraWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void timer_connect();
    void stop_connect();
    void start_connect();
    void readyRData();

    void on_btn_capture_face_clicked();
    void on_btn_register_confirm_clicked();
    void on_btn_checkout_clicked();
    void on_btn_clear_cart_clicked();
    void on_pushButton_add_clicked();
    void on_pushButton_jianshao_clicked();
    void on_table_cart_cellClicked(int row, int column);

    void onCardDetected(QString uid);
    void onErrorOccurred(QString msg);

    void onCameraFrameReady(const QImage &image);
    void onFacePayFrameReady(const QImage &image);

private:
    QImage MatToQImage(const cv::Mat &mat);
    cv::Mat QImageToMatBGR(const QImage &image);

    void handlePaymentResponse(const QJsonObject &obj);
    void handleProductList(const QJsonObject &obj);
    void initShopData();
    void sendInventoryUpdate();
    void updateTotalPrice();

    void startRc522Thread();
    void stopRc522Thread();

    void startCameraThread();
    void stopCameraThread();

    void sendFacePayRequest(const QImage &image);

private:
    Ui::MainWindow *ui = nullptr;

    // 摄像头线程
    QThread *m_cameraThread = nullptr;
    CameraWorker *m_cameraWorker = nullptr;

    // 当前图像
    QImage m_latestCameraImage;
    cv::Mat m_registerFaceMat;
    cv::CascadeClassifier cascade;

    // 网络
    QTcpSocket *mysocket = nullptr;
    QTimer mytimer;
    QByteArray m_buffer;

    // 商品数据
    QMap<QString, double> m_priceList;
    QMap<QString, int> m_nameToIdMap;
    double m_currentTotalPrice = 0.0;
    bool m_isResultLocked = false;

    // 刷脸支付
    int flag_count = 0;
    int bool_faceOpen = 0;

    // RC522
    QThread *m_thread = nullptr;
    Rc522Worker *m_worker = nullptr;

signals:
    void frameConsumed();
    void setFacePayEnabled(bool enabled);
};

#endif // MAINWINDOW_H
