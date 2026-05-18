#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "camera_worker.h"

#include <QDebug>
#include <QNetworkProxy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>
#include <QMessageBox>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPixmap>
#include <QBuffer>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>

using namespace cv;

static const char *SERVER_IP = "192.168.1.115";
static const quint16 SERVER_PORT = 5001;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if (ui->label_camera) {
        ui->label_camera->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        ui->label_camera->setScaledContents(true);
    }

    if (ui->table_cart) {
        ui->table_cart->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }
    if (ui->table_order) {
        ui->table_order->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    connect(ui->btn_capture_face, &QPushButton::clicked,
            this, &MainWindow::on_btn_capture_face_clicked);
    connect(ui->btn_register_confirm, &QPushButton::clicked,
            this, &MainWindow::on_btn_register_confirm_clicked);
//    connect(ui->btn_checkout, &QPushButton::clicked,
//            this, &MainWindow::on_btn_checkout_clicked);
    connect(ui->btn_clear_cart, &QPushButton::clicked,
            this, &MainWindow::on_btn_clear_cart_clicked);
    connect(ui->pushButton_add, &QPushButton::clicked,
            this, &MainWindow::on_pushButton_add_clicked);
    connect(ui->pushButton_jianshao, &QPushButton::clicked,
            this, &MainWindow::on_pushButton_jianshao_clicked);

    connect(ui->tabWidget_Right, &QTabWidget::currentChanged,
            this, [=](int index){
        qDebug() << "Tab switched to:" << index;
    });

    if (ui->table_cart) {
        connect(ui->table_cart, &QTableWidget::cellPressed,
                this, &MainWindow::on_table_cart_cellClicked);
    }

    mysocket = new QTcpSocket(this);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);

    connect(&mytimer, &QTimer::timeout, this, &MainWindow::timer_connect);
    connect(mysocket, &QTcpSocket::connected, this, &MainWindow::stop_connect);
    connect(mysocket, &QTcpSocket::disconnected, this, &MainWindow::start_connect);
    connect(mysocket, &QTcpSocket::readyRead, this, &MainWindow::readyRData);

    timer_connect();
    mytimer.start(2000);

    if (!cascade.load("./haarcascade_frontalface_alt2.xml")) {
        qWarning() << "load haarcascade failed";
    }

    flag_count = 0;
    bool_faceOpen = 0;

    initShopData();
    startRc522Thread();
    startCameraThread();
}

MainWindow::~MainWindow()
{
    stopCameraThread();
    stopRc522Thread();

    delete ui;
}

void MainWindow::startCameraThread()
{
    if (m_cameraThread) {
        return;
    }

    m_cameraThread = new QThread(this);
    m_cameraWorker = new CameraWorker();

    m_cameraWorker->moveToThread(m_cameraThread);

    connect(m_cameraThread, &QThread::started,
            m_cameraWorker, &CameraWorker::startWork);

    connect(m_cameraWorker, &CameraWorker::frameReady,
            this, &MainWindow::onCameraFrameReady);

    connect(m_cameraWorker, &CameraWorker::errorOccurred,
            this, [=](const QString &msg){
        qWarning() << msg;
        if (ui->label_camera) {
            ui->label_camera->setText(msg);
        }
    });

    connect(m_cameraWorker, &CameraWorker::facePayFrameReady,
            this, &MainWindow::onFacePayFrameReady);

    connect(this, &MainWindow::frameConsumed,
            m_cameraWorker, &CameraWorker::onFrameConsumed);

    connect(this, &MainWindow::setFacePayEnabled,
            m_cameraWorker, &CameraWorker::setFacePayEnabled);

    connect(m_cameraThread, &QThread::finished,
            m_cameraWorker, &QObject::deleteLater);

    m_cameraThread->start();
}

void MainWindow::stopCameraThread()
{
    if (!m_cameraThread || !m_cameraWorker) {
        return;
    }

    m_cameraWorker->stopWork();
    m_cameraThread->quit();
    m_cameraThread->wait();

    m_cameraWorker = nullptr;
    m_cameraThread = nullptr;
}

void MainWindow::startRc522Thread()
{
    if (m_thread) {
        return;
    }

    m_thread = new QThread(this);
    m_worker = new Rc522Worker();

    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &Rc522Worker::doWork);
    connect(m_worker, &Rc522Worker::cardDetected, this, &MainWindow::onCardDetected);
    connect(m_worker, &Rc522Worker::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    m_thread->start();
}

void MainWindow::stopRc522Thread()
{
    if (!m_thread || !m_worker) {
        return;
    }

    m_worker->stopWork();
    m_thread->quit();
    m_thread->wait();

    m_worker = nullptr;
    m_thread = nullptr;
}

void MainWindow::onCameraFrameReady(const QImage &image)
{
    if (image.isNull()) {
        emit frameConsumed();
        return;
    }

    m_latestCameraImage = image.copy();

    if (ui->label_camera) {
        ui->label_camera->setPixmap(QPixmap::fromImage(image));
    }

    emit frameConsumed();
}

void MainWindow::onFacePayFrameReady(const QImage &image)
{
    if (!bool_faceOpen) {
        return;
    }

    sendFacePayRequest(image);
}

QImage MainWindow::MatToQImage(const cv::Mat &mat)
{
    if (mat.empty()) {
        return QImage();
    }

    if (mat.type() == CV_8UC1) {
        QImage image(mat.cols, mat.rows, QImage::Format_Grayscale8);
        for (int row = 0; row < mat.rows; ++row) {
            memcpy(image.scanLine(row), mat.ptr(row), static_cast<size_t>(mat.cols));
        }
        return image;
    }

    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage image(rgb.data, rgb.cols, rgb.rows,
                     static_cast<int>(rgb.step),
                     QImage::Format_RGB888);
        return image.copy();
    }

    return QImage();
}

cv::Mat MainWindow::QImageToMatBGR(const QImage &image)
{
    if (image.isNull()) {
        return cv::Mat();
    }

    QImage img = image.convertToFormat(QImage::Format_RGB888);

    cv::Mat rgb(img.height(),
                img.width(),
                CV_8UC3,
                const_cast<uchar*>(img.bits()),
                static_cast<size_t>(img.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr.clone();
}

void MainWindow::readyRData()
{
    const QByteArray temp = mysocket->readAll();
    m_buffer.append(temp);

    while (m_buffer.size() >= static_cast<int>(sizeof(unsigned int))) {
        unsigned int packetLen = 0;
        memcpy(&packetLen, m_buffer.constData(), sizeof(unsigned int));

        const int fullPackageSize = static_cast<int>(sizeof(unsigned int) + packetLen);
        if (m_buffer.size() < fullPackageSize) {
            break;
        }

        const QByteArray jsonData = m_buffer.mid(sizeof(unsigned int), packetLen);
        m_buffer.remove(0, fullPackageSize);

        QJsonParseError jsonError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &jsonError);
        if (jsonError.error != QJsonParseError::NoError) {
            qDebug() << "JSON parse failed:" << jsonError.errorString();
            continue;
        }

        if (!doc.isObject()) {
            continue;
        }

        const QJsonObject obj = doc.object();
        const int type = obj.value("type").toInt();

        if (type == 2) {
            handlePaymentResponse(obj);
        } else if (type == 3) {
            handleProductList(obj);
        }
    }
}

void MainWindow::timer_connect()
{
    if (mysocket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "Connecting to server" << SERVER_IP << SERVER_PORT;
        mysocket->connectToHost(SERVER_IP, SERVER_PORT);
    }
}

void MainWindow::stop_connect()
{
    qDebug() << "Server connected";
    mytimer.stop();
}

void MainWindow::start_connect()
{
    qDebug() << "Server disconnected";
    mytimer.start();
}

void MainWindow::on_btn_capture_face_clicked()
{
    if (m_latestCameraImage.isNull()) {
        QMessageBox::warning(this, "提示", "当前没有摄像头画面");
        return;
    }

    m_registerFaceMat = QImageToMatBGR(m_latestCameraImage);

    const QImage img = m_latestCameraImage;
    if (ui->label_reg_face_preview) {
        ui->label_reg_face_preview->setPixmap(
            QPixmap::fromImage(img).scaled(
                ui->label_reg_face_preview->size(),
                Qt::KeepAspectRatio,
                Qt::FastTransformation));
        ui->label_reg_face_preview->setText("");
    }
}

void MainWindow::on_btn_register_confirm_clicked()
{
    const QString cardId = ui->lineEdit_reg_cardid->text().trimmed();

    if (cardId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先刷卡");
        return;
    }

    if (m_registerFaceMat.empty()) {
        QMessageBox::warning(this, "提示", "请先抓拍人脸");
        return;
    }

    std::vector<uchar> buf;
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(90);

    if (!cv::imencode(".jpg", m_registerFaceMat, buf, params)) {
        QMessageBox::warning(this, "失败", "人脸图片编码失败");
        return;
    }

    QByteArray imgBytes(reinterpret_cast<const char*>(buf.data()),
                        static_cast<int>(buf.size()));
    const QString imgBase64 = imgBytes.toBase64();

    QJsonObject json;
    json.insert("type", 1);
    json.insert("name", QString("用户_%1").arg(cardId));
    json.insert("cardId", cardId);
    json.insert("image", imgBase64);

    const QJsonDocument doc(json);
    const QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    QByteArray packet;
    unsigned int len = static_cast<unsigned int>(jsonData.size());
    packet.append(reinterpret_cast<const char*>(&len), sizeof(len));
    packet.append(jsonData);

    if (mysocket->state() == QAbstractSocket::ConnectedState) {
        mysocket->write(packet);
        mysocket->flush();
        QMessageBox::information(this, "成功", "注册信息已发送");
    } else {
        QMessageBox::warning(this, "失败", "服务器未连接");
    }
}

void MainWindow::on_btn_checkout_clicked()
{
    if (!ui->table_order || ui->table_order->rowCount() <= 0) {
        QMessageBox::warning(this, "提示", "请先添加商品");
        return;
    }

    if (bool_faceOpen == 0) {
        bool_faceOpen = 1;
        flag_count = 0;

        emit setFacePayEnabled(true);

        if (ui->btn_checkout) {
            ui->btn_checkout->setText("关闭刷脸支付");
        }

        if (ui->label_shop_tip) {
            ui->label_shop_tip->setText("请面向摄像头进行刷脸支付");
        }
    } else {
        bool_faceOpen = 0;
        flag_count = 0;

        emit setFacePayEnabled(false);

        if (ui->btn_checkout) {
            ui->btn_checkout->setText("打开刷脸支付");
        }

        if (ui->label_shop_tip) {
            ui->label_shop_tip->setText("已关闭刷脸支付");
        }
    }
}

void MainWindow::on_btn_clear_cart_clicked()
{
    if (ui->table_order) {
        ui->table_order->setRowCount(0);
    }
    m_currentTotalPrice = 0.0;
    updateTotalPrice();
}

void MainWindow::on_pushButton_add_clicked()
{
    if (!ui || !ui->lineEdit_item || !ui->table_order) {
        qWarning() << "UI pointer invalid in on_pushButton_add_clicked";
        return;
    }

    const QString targetName = ui->lineEdit_item->text().trimmed();
    if (targetName.isEmpty()) {
        qWarning() << "targetName is empty";
        return;
    }

    if (!m_priceList.contains(targetName)) {
        qWarning() << "Product not found in m_priceList:" << targetName;
        return;
    }

    const double price = m_priceList.value(targetName);

    int orderRow = -1;
    for (int i = 0; i < ui->table_order->rowCount(); ++i) {
        QTableWidgetItem *nameItem = ui->table_order->item(i, 0);
        if (nameItem && nameItem->text() == targetName) {
            orderRow = i;
            break;
        }
    }

    if (orderRow != -1) {
        QTableWidgetItem *qtyItem = ui->table_order->item(orderRow, 2);
        QTableWidgetItem *subItem = ui->table_order->item(orderRow, 3);

        if (!qtyItem) {
            qtyItem = new QTableWidgetItem("0");
            qtyItem->setTextAlignment(Qt::AlignCenter);
            ui->table_order->setItem(orderRow, 2, qtyItem);
        }

        if (!subItem) {
            subItem = new QTableWidgetItem("0.00");
            subItem->setTextAlignment(Qt::AlignCenter);
            ui->table_order->setItem(orderRow, 3, subItem);
        }

        const int oldQty = qtyItem->text().toInt();
        const int newQty = oldQty + 1;

        qtyItem->setText(QString::number(newQty));
        subItem->setText(QString::number(price * newQty, 'f', 2));
    } else {
        const int row = ui->table_order->rowCount();
        ui->table_order->insertRow(row);

        QTableWidgetItem *item0 = new QTableWidgetItem(targetName);
        QTableWidgetItem *item1 = new QTableWidgetItem(QString::number(price, 'f', 2));
        QTableWidgetItem *item2 = new QTableWidgetItem("1");
        QTableWidgetItem *item3 = new QTableWidgetItem(QString::number(price, 'f', 2));

        item0->setTextAlignment(Qt::AlignCenter);
        item1->setTextAlignment(Qt::AlignCenter);
        item2->setTextAlignment(Qt::AlignCenter);
        item3->setTextAlignment(Qt::AlignCenter);

        ui->table_order->setItem(row, 0, item0);
        ui->table_order->setItem(row, 1, item1);
        ui->table_order->setItem(row, 2, item2);
        ui->table_order->setItem(row, 3, item3);
    }

    updateTotalPrice();
    ui->lineEdit_item->clear();
}

void MainWindow::on_pushButton_jianshao_clicked()
{
    if (!ui || !ui->lineEdit_item || !ui->table_order) {
        qWarning() << "UI pointer invalid in on_pushButton_jianshao_clicked";
        return;
    }

    const QString targetName = ui->lineEdit_item->text().trimmed();
    if (targetName.isEmpty()) {
        return;
    }

    int orderRow = -1;
    for (int i = 0; i < ui->table_order->rowCount(); ++i) {
        QTableWidgetItem *nameItem = ui->table_order->item(i, 0);
        if (nameItem && nameItem->text() == targetName) {
            orderRow = i;
            break;
        }
    }

    if (orderRow < 0 || orderRow >= ui->table_order->rowCount()) {
        qWarning() << "Product not found in order:" << targetName;
        return;
    }

    QTableWidgetItem *priceItem = ui->table_order->item(orderRow, 1);
    QTableWidgetItem *qtyItem   = ui->table_order->item(orderRow, 2);
    QTableWidgetItem *subItem   = ui->table_order->item(orderRow, 3);

    if (!priceItem || !qtyItem || !subItem) {
        qWarning() << "Order row item missing, row =" << orderRow;
        return;
    }

    const double price = priceItem->text().toDouble();
    const int qty = qtyItem->text().toInt();
    const int newQty = qty - 1;

    if (newQty <= 0) {
        ui->table_order->removeRow(orderRow);
    } else {
        qtyItem->setText(QString::number(newQty));
        subItem->setText(QString::number(price * newQty, 'f', 2));
    }

    updateTotalPrice();
    ui->lineEdit_item->clear();
}

void MainWindow::on_table_cart_cellClicked(int row, int column)
{
    Q_UNUSED(column);

    if (!ui || !ui->table_cart || !ui->lineEdit_item) {
        qWarning() << "UI pointer invalid in on_table_cart_cellClicked";
        return;
    }

    if (row < 0 || row >= ui->table_cart->rowCount()) {
        qWarning() << "Invalid row:" << row << "rowCount:" << ui->table_cart->rowCount();
        return;
    }

    QTableWidgetItem *item = ui->table_cart->item(row, 0);
    if (!item) {
        qWarning() << "table_cart item(row,0) is null, row =" << row;
        return;
    }

    const QString name = item->text().trimmed();
    if (name.isEmpty()) {
        qWarning() << "Clicked product name is empty";
        return;
    }

    ui->lineEdit_item->setText(name);
    on_pushButton_add_clicked();
}

void MainWindow::updateTotalPrice()
{
    if (!ui || !ui->table_order || !ui->label_total_price) {
        qWarning() << "UI pointer invalid in updateTotalPrice";
        return;
    }

    double total = 0.0;
    for (int i = 0; i < ui->table_order->rowCount(); ++i) {
        QTableWidgetItem *subItem = ui->table_order->item(i, 3);
        if (subItem) {
            total += subItem->text().toDouble();
        }
    }

    m_currentTotalPrice = total;
    ui->label_total_price->setText(QString("￥%1").arg(QString::number(m_currentTotalPrice, 'f', 2)));
}

void MainWindow::handleProductList(const QJsonObject &obj)
{
    if (!obj.contains("products")) {
        return;
    }

    const QJsonArray arr = obj.value("products").toArray();

    ui->table_cart->setRowCount(0);
    m_priceList.clear();
    m_nameToIdMap.clear();

    for (const QJsonValue &val : arr) {
        const QJsonObject prod = val.toObject();

        const int id = prod.value("id").toInt();
        const QString name = prod.value("name").toString();
        const double price = prod.value("price").toDouble();
        const int stock = prod.value("stock").toInt();

        m_priceList.insert(name, price);
        m_nameToIdMap.insert(name, id);

        const int row = ui->table_cart->rowCount();
        ui->table_cart->insertRow(row);

        QTableWidgetItem *itemName = new QTableWidgetItem(name);
        itemName->setTextAlignment(Qt::AlignCenter);

        QTableWidgetItem *itemPrice = new QTableWidgetItem(QString::number(price, 'f', 2));
        itemPrice->setTextAlignment(Qt::AlignCenter);

        const QString stockStr = (stock > 0) ? QString::number(stock) : "缺货";
        QTableWidgetItem *itemStock = new QTableWidgetItem(stockStr);
        itemStock->setTextAlignment(Qt::AlignCenter);
        if (stock <= 0) {
            itemStock->setForeground(Qt::red);
        }

        ui->table_cart->setItem(row, 0, itemName);
        ui->table_cart->setItem(row, 1, itemPrice);
        ui->table_cart->setItem(row, 2, itemStock);
    }
}

void MainWindow::initShopData()
{
    m_priceList.clear();
    m_priceList.insert("苹果", 20.00);
    m_priceList.insert("葡萄", 29.00);

    if (ui->table_cart) {
        ui->table_cart->setRowCount(0);
        ui->table_cart->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

        QMapIterator<QString, double> i(m_priceList);
        while (i.hasNext()) {
            i.next();
            const int row = ui->table_cart->rowCount();
            ui->table_cart->insertRow(row);

            ui->table_cart->setItem(row, 0, new QTableWidgetItem(i.key()));
            ui->table_cart->setItem(row, 1, new QTableWidgetItem(QString::number(i.value(), 'f', 2)));
            ui->table_cart->setItem(row, 2, new QTableWidgetItem("999"));

            for (int k = 0; k < 3; ++k) {
                ui->table_cart->item(row, k)->setTextAlignment(Qt::AlignCenter);
            }
        }
    }

    if (ui->table_order) {
        ui->table_order->setRowCount(0);
        ui->table_order->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    }

    m_currentTotalPrice = 0.0;
    updateTotalPrice();
}

void MainWindow::sendInventoryUpdate()
{
    QJsonArray itemsArray;

    for (int i = 0; i < ui->table_order->rowCount(); ++i) {
        const QString name = ui->table_order->item(i, 0)->text();
        const int count = ui->table_order->item(i, 2)->text().toInt();

        if (!name.isEmpty()) {
            QJsonObject itemObj;
            itemObj.insert("name", name);
            itemObj.insert("count", count);
            itemsArray.append(itemObj);
        }

        for (int k = 0; k < ui->table_cart->rowCount(); ++k) {
            if (ui->table_cart->item(k, 0)->text() == name) {
                int currentStock = ui->table_cart->item(k, 2)->text().toInt();
                int newStock = currentStock - count;
                if (newStock < 0) {
                    newStock = 0;
                }

                ui->table_cart->item(k, 2)->setText(QString::number(newStock));
                if (newStock == 0) {
                    ui->table_cart->item(k, 2)->setForeground(Qt::red);
                }
                break;
            }
        }
    }

    if (itemsArray.isEmpty()) {
        return;
    }

    QJsonObject json;
    json.insert("type", 5);
    json.insert("items", itemsArray);

    const QJsonDocument doc(json);
    const QByteArray sendData = doc.toJson(QJsonDocument::Compact);

    QByteArray packet;
    unsigned int len = static_cast<unsigned int>(sendData.size());
    packet.append(reinterpret_cast<const char*>(&len), sizeof(len));
    packet.append(sendData);

    if (mysocket->state() == QAbstractSocket::ConnectedState) {
        mysocket->write(packet);
        mysocket->flush();
    }
}

void MainWindow::sendFacePayRequest(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    if (mysocket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "失败", "服务器未连接");
        return;
    }

    cv::Mat bgr = QImageToMatBGR(image);
    if (bgr.empty()) {
        qWarning() << "QImageToMatBGR failed for face pay";
        return;
    }

    std::vector<uchar> buf;
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(90);

    if (!cv::imencode(".jpg", bgr, buf, params)) {
        qWarning() << "imencode failed for face pay";
        return;
    }

    QByteArray imgBytes(reinterpret_cast<const char*>(buf.data()),
                        static_cast<int>(buf.size()));
    QString imgBase64 = imgBytes.toBase64();

    QJsonObject json;
    json.insert("type", 2);
    json.insert("image", imgBase64);
    json.insert("price", m_currentTotalPrice);

    QJsonDocument doc(json);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    QByteArray packet;
    unsigned int len = static_cast<unsigned int>(jsonData.size());
    packet.append(reinterpret_cast<const char*>(&len), sizeof(len));
    packet.append(jsonData);

    mysocket->write(packet);
    mysocket->flush();

    qDebug() << "face payment request sent, price =" << m_currentTotalPrice;

    bool_faceOpen = 0;
    emit setFacePayEnabled(false);

    if (ui->btn_checkout) {
        ui->btn_checkout->setText("打开刷脸支付");
    }

    if (ui->label_shop_tip) {
        ui->label_shop_tip->setText("正在进行人脸识别支付...");
    }
}

void MainWindow::handlePaymentResponse(const QJsonObject &obj)
{
    bool_faceOpen = 0;
    emit setFacePayEnabled(false);

    if (ui->btn_checkout) {
        ui->btn_checkout->setText("打开刷脸支付");
    }

    const bool success = obj.value("result").toBool();
    const QString msg = obj.value("msg").toString();

    if (success) {
        sendInventoryUpdate();

        if (ui->table_order) {
            ui->table_order->setRowCount(0);
        }
        m_currentTotalPrice = 0.0;
        updateTotalPrice();

        if (ui->label_shop_tip) {
            ui->label_shop_tip->setText("✅ 支付成功");
        }
    } else {
        if (ui->label_shop_tip) {
            ui->label_shop_tip->setText(QString("❌ 支付失败: %1").arg(msg));
        }
    }
}

void MainWindow::onCardDetected(QString uid)
{
    qDebug() << "UI received card:" << uid;
    if (ui->lineEdit_reg_cardid) {
        ui->lineEdit_reg_cardid->setText(uid);
    }
}

void MainWindow::onErrorOccurred(QString msg)
{
    if (ui->lineEdit_reg_cardid) {
        ui->lineEdit_reg_cardid->setText("错误: " + msg);
    }
}
