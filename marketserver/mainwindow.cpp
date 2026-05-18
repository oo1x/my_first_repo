#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDateTime>
#include <QMessageBox>
#include <QJsonParseError>
#include <QJsonObject>
#include <QSqlQuery>
#include<QJsonArray>
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    mysocket = nullptr; // 初始化为空
    bsize=0;
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    // 连接新客户端信号 (此时还未监听)
    connect(&myserver, &QTcpServer::newConnection, this, &MainWindow::acceptClient);

// 2. 【核心修改】初始化开关按钮
    // 确保按钮是可以被“选中”的 (Checkable)
    ui->btn_server_switch->setCheckable(true);
    ui->btn_server_switch->setChecked(false);  // 默认未启动
    ui->btn_server_switch->setText("启动服务器");
    // 连接按钮的 toggled (切换) 信号到我们的槽函数
    connect(ui->btn_server_switch, &QPushButton::toggled, this, &MainWindow::onServerSwitchToggled);



// 1. 点击“监控概览” -> 切换到第0页
    connect(ui->btn_nav_home, &QPushButton::clicked, [=](){
        ui->stackedWidget_Content->setCurrentIndex(0); // 0 对应 page_home
        ui->label_current_page->setText("监控概览");
    });

    // 2. 点击“会员管理” -> 切换到第1页
    connect(ui->btn_nav_users, &QPushButton::clicked, [=](){
        ui->stackedWidget_Content->setCurrentIndex(1); // 1 对应 page_users
        ui->label_current_page->setText("会员管理");
    });

    // 3. 点击“商品库存” -> 切换到第2页
    connect(ui->btn_nav_products, &QPushButton::clicked, [=](){
        ui->stackedWidget_Content->setCurrentIndex(2); // 2 对应 page_products
        ui->label_current_page->setText("商品库存");
    });

    // 设置默认页面
    ui->stackedWidget_Content->setCurrentIndex(0);

    // 初始化表格列宽（让表格好看一点）
    ui->table_users->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->table_products->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    ui->label_monitor_img->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

        // 2. 设置居中显示 (这样如果图片长宽比和Label不一样，图片会居中，两边留黑)
    ui->label_monitor_img->setAlignment(Qt::AlignCenter);

    initDatabase();
    refreshUserTable();
    // 程序启动时，自动加载已有的商品数据
    loadProductTable();
}
MainWindow::~MainWindow()
{
    // 关闭服务器
    if(myserver.isListening()) {
        myserver.close();
    }
    delete ui;
}
void MainWindow::onServerSwitchToggled(bool checked)
{
    if (checked)
    {
        // --- 用户想要启动服务器 ---

        // 获取端口 (假设界面上有 lineEdit_port，如果没有，请直接写 9999)
        int port = 9999;
        if(ui->lineEdit_port) {
            port = ui->lineEdit_port->text().toInt();
            if(port <= 0) port = 9999;
        }

        // 开始监听
        if (!myserver.listen(QHostAddress::Any, port)) {
            // 启动失败
            QMessageBox::critical(this, "错误", "无法监听端口，可能被占用！");
            ui->textEdit_log->append("[Error] 端口监听失败: " + myserver.errorString());

            // 把按钮状态弹回去
            // 临时断开信号防止死循环
            ui->btn_server_switch->blockSignals(true);
            ui->btn_server_switch->setChecked(false);
            ui->btn_server_switch->blockSignals(false);
            return;
        }

        // 启动成功：更新UI
        ui->btn_server_switch->setText("停止服务器");
        // 按钮变红
        ui->btn_server_switch->setStyleSheet("background-color: #e74c3c; color: white;");

        // 更新状态标签 (假设有 label_status_val)
        if(ui->label_status_val) {
            ui->label_status_val->setText("运行中");
            ui->label_status_val->setStyleSheet("color: #27ae60; font-weight: bold;");
        }

        ui->textEdit_log->append(QString("[System] 服务器已启动，监听端口: %1").arg(port));
    }
    else
    {
        // --- 用户想要停止服务器 ---

        myserver.close();

        // 如果有客户端连接，也可以在这里断开
        if(mysocket && mysocket->isOpen()) {
            mysocket->disconnectFromHost();
        }

        // 更新UI
        ui->btn_server_switch->setText("启动服务器");
        // 按钮变绿
        ui->btn_server_switch->setStyleSheet("background-color: #27ae60; color: white;");

        if(ui->label_status_val) {
            ui->label_status_val->setText("已停止");
            ui->label_status_val->setStyleSheet("color: #e74c3c; font-weight: bold;");
        }

        ui->textEdit_log->append("[System] 服务器已停止。");
    }
}
void MainWindow::acceptClient()
{
    mysocket = myserver.nextPendingConnection();

    QString ip = mysocket->peerAddress().toString();
    ui->textEdit_log->append("[Connect] 新客户端接入: " + ip);

    // 连接数据读取信号
    connect(mysocket, &QTcpSocket::readyRead, this, &MainWindow::ready_readData);

    // 【重要】处理客户端断开，防止崩溃
    connect(mysocket, &QTcpSocket::disconnected, [=](){
        ui->textEdit_log->append("[Disconnect] 客户端已断开");
        mysocket->deleteLater();
        mysocket = nullptr;
    });
    sendProductListToClient(mysocket);
}
void MainWindow::ready_readData()
{

    // 1. 将所有新到达的数据追加到缓存区
    QByteArray temp = mysocket->readAll();
    m_buffer.append(temp);

    // 2. 循环处理缓存区（处理粘包：可能一次收到了多个包，或者半个包）
    while (m_buffer.size() >= (int)sizeof(unsigned int))
    {
        // 2.1 取出前4个字节，计算包体长度
        unsigned int packetLen = 0;
        memcpy(&packetLen, m_buffer.constData(), sizeof(unsigned int));

        // 2.2 判断缓存区数据是否足够一个完整的包
        // 完整包长度 = 包头(4字节) + 包体(JSON长度)
        if (m_buffer.size() < (int)(sizeof(unsigned int) + packetLen)) {
            // 数据不够，跳出循环，等待下一次 readyRead
            break;
        }

        // 2.3 提取包体数据 (JSON部分)
        QByteArray jsonData = m_buffer.mid(sizeof(unsigned int), packetLen);

        // 2.4 从缓存区移除已处理的数据 (滑动窗口)
        m_buffer.remove(0, sizeof(unsigned int) + packetLen);

        // ==========================================
        //  开始业务处理
        // ==========================================
        QJsonParseError jsonError;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &jsonError);

        if (jsonError.error != QJsonParseError::NoError || doc.isNull()) {
            qDebug() << "JSON 解析失败:" << jsonError.errorString();
            continue;
        }

        QJsonObject obj = doc.object();
        int type = obj.value("type").toInt();
        double price = 0.0;
        if (obj.contains("price") && obj.value("price").isDouble()) {
            price = obj.value("price").toDouble();
        }

        // ----------------------------------------------------
        // 类型 2: 注册信息 (包含：姓名 + 卡号 + 图片)
        // ----------------------------------------------------
        if (type == 1)
        {

            QString name = obj.value("name").toString();
            QString cardId = obj.value("cardId").toString();
            QString imgBase64 = obj.value("image").toString();

            // 解码 Base64 -> 二进制图片数据
            QByteArray imgBytes = QByteArray::fromBase64(imgBase64.toUtf8());
            QPixmap photo;
            m_currentReceivedFaceData = imgBytes;
            bool loadOk = photo.loadFromData(imgBytes, "JPG");

            if(loadOk)
            {
                // A. 界面显示 (【关键】：使用平滑缩放解决视觉模糊)
                if(ui->label_user_photo) {
                    ui->label_user_photo->setPixmap(photo.scaled(
                        ui->label_user_photo->size(),
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation // 消除锯齿，画质变好
                    ));
                }

                // B. 填充文本信息
                if(ui->lineEdit_edit_name) ui->lineEdit_edit_name->setText(name);

                // 查找卡号控件
                QLineEdit *txtCard = findChild<QLineEdit*>("lineEdit_cardID");
                if(txtCard) txtCard->setText(cardId);
                else if(ui->lineEdit_edit_balance) ui->lineEdit_edit_balance->setText(cardId); // 兼容旧UI

                // C. 自动切换到“会员管理”页查看
                ui->stackedWidget_Content->setCurrentIndex(1); // 假设1是会员页

                // D. 【核心】直接保存接收到的二进制数据为文件 (这是高清原图)
                QString saveDir = "./data/faces/";
                QDir dir; if(!dir.exists(saveDir)) dir.mkpath(saveDir);

                QString fileName = QString("%1_%2.jpg").arg(cardId).arg(name);
                QFile file(saveDir + fileName);
                if(file.open(QIODevice::WriteOnly)) {
                    file.write(imgBytes); // 直接写二进制，没有任何压缩损失
                    file.close();
                    qDebug() << "高清原图已保存:" << fileName;
                }

                ui->textEdit_log->append(QString("[Register] 收到: %1 (%2)").arg(name).arg(cardId));
            }
        }
        else if (type == 5) {
            //处理库存更新请求
            qDebug()<<"handleInventoryUpdatehandleInventoryUpdate";
            handleInventoryUpdate(obj);
        }
        // ----------------------------------------------------
        // 类型 1: 实时监控 (只包含：图片)
        // ----------------------------------------------------
        else if (type == 2)
        {

            QString imgBase64 = obj.value("image").toString();
            // 解码 Base64 -> 二进制图片数据
            QByteArray imgBytes = QByteArray::fromBase64(imgBase64.toUtf8());

            QPixmap photo;

            bool loadOk = photo.loadFromData(imgBytes, "JPG");

            if(loadOk)
            {
                // A. 界面显示 (【关键】：使用平滑缩放解决视觉模糊)
                if(ui->label_monitor_img) {
                    ui->label_monitor_img->setPixmap(photo.scaled(
                        ui->label_monitor_img->size(),
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation // 消除锯齿，画质变好
                    ));
                }
            }
            // 3. 转换为 OpenCV Mat 格式 (供人脸识别使用)
                // 将 QByteArray 数据放入 vector 中以便 imdecode 使用
            std::vector<uchar> decode_buf(imgBytes.begin(), imgBytes.end());
            cv::Mat faceImage = cv::imdecode(decode_buf, cv::IMREAD_COLOR);

            int64_t faceId = faceObj.face_query(faceImage); // 调用识别函数
            qDebug() << "Query FaceID:" << faceId << " Price:" << price;
            // 5. 准备返回给客户端的 JSON 对象
            QJsonObject responseObj;
            responseObj.insert("type", 2); // 回复类型也是2，代表支付结果
            if (faceId < 0)
             {
                // --- 识别失败 ---
                responseObj.insert("result", false);
                responseObj.insert("msg", "未识别到注册用户");
             }
            else
            {

                qint64 currentTime = QDateTime::currentMSecsSinceEpoch(); // 获取当前毫秒时
                // 检查该 FaceID 是否在缓存中
                if (m_lastPayTime.contains(faceId))
                {
                    qint64 lastTime = m_lastPayTime.value(faceId);

                    // 如果 距离上次支付时间 < 5000毫秒 (5秒)，则认为是重复请求，直接忽略
                    if (currentTime - lastTime < 5000)
                    {
                        qDebug() << "FaceID:" << faceId << " 处于冷却期，忽略本次请求";

                        // 这里可以选择：
                        // 1. 直接 return，不给客户端回包（客户端会以为没识别到）
                        // 2. 或者返回一个特殊状态告诉客户端“支付过于频繁”

                        // 简单做法：构建一个简单的提示包返回，或者直接忽略
                        QJsonObject responseObj;
                        responseObj.insert("type", 2);
                        responseObj.insert("result", false);
                        responseObj.insert("msg", "支付频繁，请稍后再试");

                        // 发送忽略提示 (可选)
                        QJsonDocument doc(responseObj);
                        QByteArray sendData = doc.toJson(QJsonDocument::Compact);
                        QByteArray packet;
                        unsigned int len = sendData.size();
                        packet.append((char*)&len, sizeof(len));
                        packet.append(sendData);
                        mysocket->write(packet);

                        return; // ⛔️ 终止本次执行，不再扣费
                    }
                }
                // --- 识别成功，查询数据库进行支付 ---
                QSqlQuery query;
                // 查询 用户信息 和 余额
                query.prepare("SELECT name, cardid, balance FROM employee WHERE faceid = :fid");
                query.bindValue(":fid", (qint64)faceId);

                if (query.exec() && query.next())
                {
                    QString name = query.value("name").toString();
                    QString cardId = query.value("cardid").toString();
                    double currentBalance = query.value("balance").toDouble();

                    // --- 6. 核心支付逻辑：判断余额 ---
                    if (currentBalance >= price)
                    {
                        // A. 余额充足，执行扣费
                        double newBalance = currentBalance - price;

                        // 更新数据库余额
                        QSqlQuery updateQuery;
                        updateQuery.prepare("UPDATE employee SET balance = :newBal WHERE faceid = :fid");
                        updateQuery.bindValue(":newBal", newBalance);
                        updateQuery.bindValue(":fid", (qint64)faceId);

                        if(updateQuery.exec())
                        {
                            m_lastPayTime.insert(faceId, QDateTime::currentMSecsSinceEpoch());

                            // 支付成功
                            responseObj.insert("result", true);
                            responseObj.insert("name", name);
                            responseObj.insert("cardId", cardId);
                            responseObj.insert("balance", newBalance); // 返回最新余额
                            responseObj.insert("faceid", QString::number(faceId));
                            responseObj.insert("msg", "支付成功");
                            qDebug()<<"支付成功!!!!!!!!!!!!!!!!!!";

                        }
                        else
                        {
                            // 数据库更新失败
                            responseObj.insert("result", false);
                            responseObj.insert("msg", "数据库错误: 扣费失败");
                        }
                    }
                    else
                    {
                        // B. 余额不足
                        responseObj.insert("result", false);
                        responseObj.insert("name", name);
                        responseObj.insert("balance", currentBalance);
                        responseObj.insert("msg", QString("余额不足，当前余额: %1").arg(currentBalance));
                    }
                }
                else
                {
                    // 虽然 FaceID > 0，但数据库里查不到记录 (数据不一致)
                    responseObj.insert("result", false);
                    responseObj.insert("msg", "用户数据异常");
                }
            }

            // 7. 发送反馈数据给客户端
            QJsonDocument doc(responseObj);
            QByteArray sendData = doc.toJson(QJsonDocument::Compact);
            qDebug()<<"sendData__sendData"<<sendData;
            QByteArray packet;
            unsigned int len = sendData.size();
            packet.append((char*)&len, sizeof(len));
            packet.append(sendData);

            mysocket->write(packet);
            mysocket->flush();

        }
    }
}

void MainWindow::sendProductListToClient(QTcpSocket *clientSocket)
{
    if (!clientSocket) return;

        // 1. 查询数据库中的商品
        QSqlQuery query("SELECT name, price, stock FROM products");

        QJsonArray prodArray;

        while (query.next()) {
            QJsonObject prodObj;
            prodObj.insert("name", query.value("name").toString());
            prodObj.insert("price", query.value("price").toDouble());
            prodObj.insert("stock", query.value("stock").toInt());

            prodArray.append(prodObj);
        }

        // 2. 构建主 JSON 包 (Type = 3 代表同步商品列表)
        QJsonObject json;
        json.insert("type", 3);
        json.insert("products", prodArray);

        // 3. 序列化并发送 (带包头)
        QJsonDocument doc(json);
        QByteArray sendData = doc.toJson(QJsonDocument::Compact);

        QByteArray packet;
        unsigned int len = sendData.size();
        packet.append((char*)&len, sizeof(len)); // 包头：长度
        packet.append(sendData);                 // 包体：JSON

        clientSocket->write(packet);
        clientSocket->flush();

        qDebug() << "已向客户端发送商品列表，共" << prodArray.size() << "件商品";
}

// 将 QImage 转换为 cv::Mat
cv::Mat MainWindow::QImageToMat(QImage image)
{
    cv::Mat mat;
    switch (image.format())
    {
    case QImage::Format_ARGB32:
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32_Premultiplied:
        mat = cv::Mat(image.height(), image.width(), CV_8UC4, (void*)image.constBits(), image.bytesPerLine());
        break;
    case QImage::Format_RGB888:
        mat = cv::Mat(image.height(), image.width(), CV_8UC3, (void*)image.constBits(), image.bytesPerLine());
        cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
        break;
    case QImage::Format_Indexed8:
        mat = cv::Mat(image.height(), image.width(), CV_8UC1, (void*)image.constBits(), image.bytesPerLine());
        break;
    default:
        // 如果格式不匹配，先转为 RGB888
        QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
        mat = cv::Mat(rgbImage.height(), rgbImage.width(), CV_8UC3, (void*)rgbImage.constBits(), rgbImage.bytesPerLine());
        cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
        break;
    }
    return mat.clone(); // 深拷贝，防止数据随 QImage 释放而丢失
}

void MainWindow::initDatabase()
{
    // 1. 连接数据库
    QSqlDatabase db;
    if(QSqlDatabase::contains("qt_sql_default_connection")) {
        db = QSqlDatabase::database("qt_sql_default_connection");
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE");
        db.setDatabaseName("server.db"); // 数据库文件名
    }

    if (!db.open()) {
        qDebug() << "Error: Failed to connect database." << db.lastError();
        return;
    }

    // 2. 创建表 (如果不存在)
    // 包含字段: id(自增主键), name, cardid(唯一), balance, faceid, headfile, create_time
    QSqlQuery query;
    QString sql = "CREATE TABLE IF NOT EXISTS employee ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL, "
                  "cardid TEXT UNIQUE, "  // 卡号唯一
                  "balance DOUBLE DEFAULT 0.0, "
                  "faceid INTEGER, "
                  "headfile TEXT, "
                  "create_time DATETIME DEFAULT CURRENT_TIMESTAMP)";

    if (!query.exec(sql)) {
        qDebug() << "Error: Create table failed." << query.lastError();
    }

    // 创建商品表：id自增，name文本，price浮点，stock整数
     sql = "CREATE TABLE IF NOT EXISTS products ("
                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                  "name TEXT NOT NULL, "
                  "price REAL NOT NULL, "
                  "stock INTEGER NOT NULL)";
    if (!query.exec(sql)) {
            qDebug() << "Error creating table:" << query.lastError();
        }

}

void MainWindow::refreshUserTable()
{
    // 1. 准备 SQL 查询
        // 假设你的表名是 employee，包含字段: cardid, name, department, balance, create_time
    // 如果没有 department 字段，请从 SQL 中去掉
    QSqlQuery query;
    if (!query.exec("SELECT cardid, name, balance, create_time FROM employee")) {
        qDebug() << "Query failed:" << query.lastError().text();
        return;
    }

    // 2. 清空当前表格内容 (保留表头)
    ui->table_users->setRowCount(0);

    // 3. 循环遍历结果集
    while (query.next())
    {
        // 获取数据
        QString cardId = query.value("cardid").toString();
        QString name = query.value("name").toString();

        double balance = query.value("balance").toDouble();
        QString time = query.value("create_time").toDateTime().toString("yyyy-MM-dd HH:mm");

        // 4. 在表格末尾插入新行
        int row = ui->table_users->rowCount();
        ui->table_users->insertRow(row);

        // 5. 设置单元格内容
        // 第0列: ID/卡号
        ui->table_users->setItem(row, 0, new QTableWidgetItem(cardId));

        // 第1列: 姓名
        ui->table_users->setItem(row, 1, new QTableWidgetItem(name));

        // 第2列: 部门 (如果暂时没有，填默认值)
        ui->table_users->setItem(row, 2, new QTableWidgetItem("普通会员"));

        // 第3列: 余额 (保留2位小数)
        ui->table_users->setItem(row, 3, new QTableWidgetItem(QString::number(balance, 'f', 2)));

        // 第4列: 注册时间
        ui->table_users->setItem(row, 4, new QTableWidgetItem(time));

        // 设置所有单元格文字居中 (可选)
        for(int i=0; i<5; i++) {
            if(ui->table_users->item(row, i))
                ui->table_users->item(row, i)->setTextAlignment(Qt::AlignCenter);
        }
    }
}
void MainWindow::on_btn_save_user_clicked()
{
    // ============================================
    // 1. 基础校验
    // ============================================
    QString name = ui->lineEdit_edit_name->text().trimmed();
    QString balanceStr = ui->lineEdit_edit_balance->text().trimmed();

    // 获取卡号 (查找控件)
    QString cardId;
    QLineEdit *txtCard = findChild<QLineEdit*>("lineEdit_cardID");
    if(txtCard) cardId = txtCard->text().trimmed();
    else cardId = ui->lineEdit_edit_balance->text(); // 备用

    // 校验输入
    if(name.isEmpty() || cardId.isEmpty()) {
        QMessageBox::warning(this, "提示", "姓名和卡号不能为空");
        return;
    }

    // 校验是否接收到人脸数据
    if (m_currentReceivedFaceData.isEmpty()) {
        QMessageBox::warning(this, "错误", "未接收到人脸数据，无法注册！\n请让客户端重新发送。");
        return;
    }

    // ============================================
    // 2. 人脸特征提取 (SeetaFace)
    // ============================================
    std::vector<uchar> buf(m_currentReceivedFaceData.begin(), m_currentReceivedFaceData.end());
    cv::Mat faceImage = cv::imdecode(buf, cv::IMREAD_COLOR);

    if (faceImage.empty()) {
        QMessageBox::warning(this, "错误", "人脸图像解码失败");
        return;
    }

    // 调用人脸注册
    // 建议：m_faceObj 应该是 MainWindow 的成员变量，不要在这里临时定义

    int64_t faceId = faceObj.face_register(faceImage);

    if (faceId < 0) {
        QMessageBox::warning(this, "注册失败", "人脸质量不合格或未检测到人脸");
        return;
    }

    // ============================================
    // 3. 保存高清原图文件
    // ============================================
    QString saveDir = "./data/faces/";
    QDir dir;
    if (!dir.exists(saveDir)) dir.mkpath(saveDir);

    QString fileName = QString("%1_%2.jpg").arg(cardId).arg(name);
    QString fullPath = saveDir + fileName;

    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_currentReceivedFaceData); // 写入原始数据，无损
        file.close();
    } else {
        QMessageBox::warning(this, "警告", "头像保存失败，请检查磁盘权限");
        return;
    }

    // ============================================
    // 4. 【核心修改】使用 QSqlQuery 写入数据库
    // ============================================
    QSqlQuery query;

    // (A) 准备 SQL 语句，使用 :占位符
    query.prepare("INSERT INTO employee (name, cardid, balance, faceid, headfile, create_time) "
                  "VALUES (:name, :cardid, :balance, :faceid, :headfile, :time)");

    // (B) 绑定变量 (自动处理类型转换和特殊字符)
    query.bindValue(":name", name);
    query.bindValue(":cardid", cardId);
    query.bindValue(":balance", balanceStr.toDouble());
    query.bindValue(":faceid", (qint64)faceId); // 强转确保类型匹配
    query.bindValue(":headfile", fullPath);
    query.bindValue(":time", QDateTime::currentDateTime());

    // (C) 执行插入
    if (query.exec())
    {
        // --- 成功逻辑 ---
        // --- 成功逻辑 ---
        QMessageBox::information(this, "成功", QString("用户注册成功！\nFaceID: %1").arg(faceId));

        // ============================================
        // 【核心修改】注册成功后，刷新界面列表
        // ============================================
        refreshUserTable();

        // 清理缓存...
        m_currentReceivedFaceData.clear();
        ui->label_user_photo->clear();
        ui->label_user_photo->setText("等待数据...");
        ui->lineEdit_edit_name->clear();
        if(txtCard) txtCard->clear();
    }
    else
    {
        // --- 失败逻辑 ---
        // 比如：卡号重复、数据库被锁等
        QString errInfo = query.lastError().text();
        qDebug() << "SQL Error:" << errInfo;

        if(errInfo.contains("UNIQUE constraint failed")) {
            QMessageBox::critical(this, "注册失败", "该卡号已存在，请勿重复注册！");
        } else {
            QMessageBox::critical(this, "注册失败", "数据库错误:\n" + errInfo);
        }
    }
}

void MainWindow::on_btn_delete_user_clicked()
{
    // 1. 获取要删除的 ID (假设输入的是 cardid)
    QString targetId = ui->lineEdit_search_user->text().trimmed();

    if (targetId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入要删除的会员 卡号(ID)！");
        return;
    }

    // 2. 二次确认 (防止手抖误删)
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认删除",
                                  QString("确定要删除卡号为 [%1] 的会员吗？\n此操作不可恢复！").arg(targetId),
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) {
        return;
    }

    // ==========================================
    // 3. (可选) 先查询出头像路径，以便稍后删除文件
    // ==========================================
    QString headFilePath;
    QSqlQuery querySelect;
    querySelect.prepare("SELECT headfile FROM employee WHERE cardid = :id");
    querySelect.bindValue(":id", targetId);
    if(querySelect.exec() && querySelect.next()) {
        headFilePath = querySelect.value("headfile").toString();
    }

    // ==========================================
    // 4. 执行删除操作
    // ==========================================
    QSqlQuery query;
    // 假设你的唯一键是 cardid，如果是数据库自增id，请把 WHERE 改为 id = :id
    query.prepare("DELETE FROM employee WHERE cardid = :id");
    query.bindValue(":id", targetId);

    if (query.exec())
    {
        // 检查是否真的删除了数据 (防止输入了一个不存在的ID，SQL执行成功但没删掉任何行)
        if (query.numRowsAffected() > 0)
        {
            // --- 成功逻辑 ---

            // 5. 删除本地头像文件 (清理垃圾)
            if (!headFilePath.isEmpty() && QFile::exists(headFilePath)) {
                QFile::remove(headFilePath);
            }

            // 6. 刷新表格显示
            refreshUserTable();

            // 7. 清空输入框和右侧详情
            ui->lineEdit_search_user->clear();
//            if(ui->lineEdit_detail_name) ui->lineEdit_detail_name->clear();
//            if(ui->lineEdit_detail_card) ui->lineEdit_detail_card->clear();
//            if(ui->lineEdit_detail_balance) ui->lineEdit_detail_balance->clear();
//            if(ui->label_user_avatar) ui->label_user_avatar->setText("已删除");

            QMessageBox::information(this, "成功", "会员删除成功！");
        }
        else
        {
            QMessageBox::warning(this, "删除失败", "未找到该卡号，请检查输入是否正确。");
        }
    }
    else
    {
        QMessageBox::critical(this, "错误", "数据库错误: " + query.lastError().text());
    }
}

//充值金额的按钮
void MainWindow::on_btn_confirm_recharge_clicked()
{
    // 1. 获取输入信息
    // 注意：你题目说ID输入框叫 lineEdit_deleteID，虽然名字听起来像删除，但我按你要求的写
    QString targetId = ui->lineEdit_recharge_id->text().trimmed();
     QString moneyStr = ui->lineEdit_MoneyAdd->text().trimmed();

    // 2. 基础校验
    if (targetId.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入要充值的会员 ID/卡号！");
        return;
    }

    bool ok;
    double addMoney = moneyStr.toDouble(&ok);

    if (!ok || addMoney <= 0) {
        QMessageBox::warning(this, "提示", "请输入正确的充值金额（必须大于0）！");
        return;
    }

    // 3. 执行数据库更新操作
    QSqlQuery query;
    // SQL逻辑：原有余额 = 原有余额 + 新充值金额
    query.prepare("UPDATE employee SET balance = balance + :money WHERE cardid = :id");
    query.bindValue(":money", addMoney);
    query.bindValue(":id", targetId);

    if (query.exec())
    {
        // 4. 判断是否真的更新了数据 (有可能卡号输错了，数据库里没有这个人)
        if (query.numRowsAffected() > 0)
        {
            // --- 充值成功 ---
            QMessageBox::information(this, "成功", QString("充值成功！\n卡号: %1\n充值金额: %2 元").arg(targetId).arg(addMoney));

            // A. 刷新表格显示 (调用之前写好的刷新函数)
            refreshUserTable();

            // B. 清空输入框，防止重复点击
            ui->lineEdit_MoneyAdd->clear();
            ui->lineEdit_recharge_id->clear();
        }
        else
        {
            // --- 充值失败 (卡号不存在) ---
            QMessageBox::warning(this, "失败", "未找到该卡号，请检查输入是否正确。");
        }
    }
    else
    {
        // --- 数据库错误 ---
        QMessageBox::critical(this, "错误", "数据库执行失败: " + query.lastError().text());
    }
}

void MainWindow::addRowToTable(const ProductInfo &product){
    int row = ui->table_products->rowCount();
    ui->table_products->insertRow(row);

    // 第0列：ID
    QTableWidgetItem *itemId = new QTableWidgetItem(QString::number(product.id));
    itemId->setTextAlignment(Qt::AlignCenter);
    ui->table_products->setItem(row, 0, itemId);

    // 第1列：名称
    QTableWidgetItem *itemName = new QTableWidgetItem(product.name);
    itemName->setTextAlignment(Qt::AlignCenter);
    ui->table_products->setItem(row, 1, itemName);

    // 第2列：单价 (保留2位小数)
    QTableWidgetItem *itemPrice = new QTableWidgetItem(QString::number(product.price, 'f', 2));
    itemPrice->setTextAlignment(Qt::AlignCenter);
    ui->table_products->setItem(row, 2, itemPrice);

    // 第3列：库存
    QTableWidgetItem *itemStock = new QTableWidgetItem(QString::number(product.stock));
    itemStock->setTextAlignment(Qt::AlignCenter);
    ui->table_products->setItem(row, 3, itemStock);


}
//更新会员信息
void MainWindow::on_btn_update_user_clicked()
{
    refreshUserTable();
}

void MainWindow::on_btn_refresh_product_clicked()
{
    loadProductTable();
}

void MainWindow::on_btn_delete_product_clicked()
{
    // 1. 获取当前选中的行
    int currentRow = ui->table_products->currentRow();
    if (currentRow == -1) {
        QMessageBox::warning(this, "提示", "请先选择要删除的商品！");
        return;
    }

    // 2. 获取该行的商品 ID (第0列)
    QString idStr = ui->table_products->item(currentRow, 0)->text();
    QString name = ui->table_products->item(currentRow, 1)->text();

    // 3. 再次确认
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认删除",
                                  QString("确定要删除商品 [%1] (ID:%2) 吗？").arg(name).arg(idStr),
                                  QMessageBox::Yes|QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // 4. 从数据库删除
        QSqlQuery query;
        query.prepare("DELETE FROM products WHERE id = :id");
        query.bindValue(":id", idStr.toInt());

        if (query.exec()) {
            // 5. 从 UI 表格移除 (不需要重查数据库，直接移除行)
            ui->table_products->removeRow(currentRow);
            QMessageBox::information(this, "成功", "删除成功");
        } else {
            QMessageBox::critical(this, "错误", "删除失败: " + query.lastError().text());
        }
    }
}

void MainWindow::on_btn_add_product_clicked()
{
    // 1. 获取 UI 输入信息
    QString name = ui->lineEdit_p_name->text().trimmed();
    QString priceStr = ui->lineEdit_p_price->text().trimmed();
    QString stockStr = ui->lineEdit_p_stock->text().trimmed();

    // 2. 数据校验 (防止空值或非法格式)
    if (name.isEmpty() || priceStr.isEmpty() || stockStr.isEmpty()) {
        QMessageBox::warning(this, "提示", "请填写完整的商品信息！");
        return;
    }

    bool priceOk, stockOk;
    double price = priceStr.toDouble(&priceOk);
    int stock = stockStr.toInt(&stockOk);

    if (!priceOk || !stockOk || price < 0 || stock < 0) {
        QMessageBox::warning(this, "提示", "价格或库存格式不正确！");
        return;
    }

    // 3. 插入数据库
    QSqlQuery query;
    query.prepare("INSERT INTO products (name, price, stock) VALUES (:name, :price, :stock)");
    query.bindValue(":name", name);
    query.bindValue(":price", price);
    query.bindValue(":stock", stock);

    if (query.exec()) {
        // --- 成功 ---

        // 4. 获取刚刚插入的 ID (主键)
        // lastInsertId() 是 Qt 提供的一个非常方便的函数，用于获取自增ID
        int newId = query.lastInsertId().toInt();

        // 5. 构建结构体
        ProductInfo newProduct;
        newProduct.id = newId;
        newProduct.name = name;
        newProduct.price = price;
        newProduct.stock = stock;

        // 6. 同步更新到 UI 表格 (不需要重新查数据库，直接在界面追加，效率最高)
        addRowToTable(newProduct);

        // 7. 清空输入框，方便下次输入
        ui->lineEdit_p_name->clear();
        ui->lineEdit_p_price->clear();
        ui->lineEdit_p_stock->clear();

        QMessageBox::information(this, "成功", "商品添加成功！");
    } else {
        // --- 失败 ---
        QMessageBox::critical(this, "错误", "数据库插入失败: " + query.lastError().text());
    }
}
void MainWindow::loadProductTable()
{
    // 1. 先清空表格，防止重复
    ui->table_products->setRowCount(0);

    // 2. 查询所有数据
    QSqlQuery query("SELECT id, name, price, stock FROM products");

    while (query.next()) {
        ProductInfo p;
        p.id = query.value("id").toInt();
        p.name = query.value("name").toString();
        p.price = query.value("price").toDouble();
        p.stock = query.value("stock").toInt();

        // 复用添加行的逻辑
        addRowToTable(p);
    }
}
void MainWindow::handleInventoryUpdate(const QJsonObject& obj)
{


    // 1. 解析客户端发来的商品列表
    if (!obj.contains("items")) return;
    QJsonArray items = obj.value("items").toArray();

    if (items.isEmpty()) return;

    QSqlQuery query;
    bool dbSuccess = true;

    // 2. 开启事务 (Transaction)
    // 事务保证了一批商品要么全部扣减成功，要么全部失败，防止数据错乱
    QSqlDatabase::database().transaction();


    // 3. 遍历列表，执行扣减 SQL
    for (const QJsonValue& val : items)
        {
            QJsonObject item = val.toObject();

            // 【修改点 1】解析 name 而不是 id
            QString name = item.value("name").toString();
            int count = item.value("count").toInt();

            // 【修改点 2】SQL 语句改为 WHERE name = :name
            query.prepare("UPDATE products SET stock = stock - :count WHERE name = :name");
            query.bindValue(":count", count);
            query.bindValue(":name", name); // 绑定名字

            if (!query.exec()) {
                qDebug() << "库存更新失败 Name:" << name << " Error:" << query.lastError();
                dbSuccess = false;
                break;
            }
        }

    // 4. 提交或回滚
    if (dbSuccess) {
        QSqlDatabase::database().commit(); // 提交事务，写入硬盘
        qDebug() << "库存扣除成功，正在同步所有客户端...";

        // 5. 【服务端刷新】刷新服务端自己的表格 (table_products)
        // 调用你之前写好的加载函数
        loadProductTable();


    }
    else {
        QSqlDatabase::database().rollback(); // 回滚事务，撤销更改
        qDebug() << "库存更新出现错误，已回滚。";
    }
}
