#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include <QTcpServer>
#include <QSqlError>
#include <QMainWindow>
#include <QSqlTableModel>
#include <QNetworkProxy>
#include <QSqlRecord>
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <QMessageBox>
#include <QDir>
#include<QTcpSocket>
#include "qfaceobject.h" // 包含你的人脸识别类
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
// 1. 定义商品结构体
struct ProductInfo {
    int id;          // 数据库自动生成的唯一ID
    QString name;    // 商品名称
    double price;    // 单价
    int stock;       // 库存
};
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
public slots:
    void acceptClient();
    void ready_readData();

private:
    Ui::MainWindow *ui;

    QTcpServer myserver;
    QTcpSocket * mysocket;
    QByteArray m_buffer; // 【新增】用于
    QByteArray m_currentReceivedFaceData;
    qfaceobject faceObj;

    QMap<qint64, qint64> m_lastPayTime;
    void sendProductListToClient(QTcpSocket* clientSocket); // 【新增】发送商品列表
public:
    int bsize;
    cv::Mat QImageToMat(QImage image);
    void initDatabase();
    void refreshUserTable(); // 【新增】刷新会员列表函数

    void loadProductTable();  // 从数据库加载所有商品到表格
    void addRowToTable(const ProductInfo &product); // 辅助函数：往表格插入一行
    void handleInventoryUpdate(const QJsonObject& obj);
private slots:

    //：增加一个处理开关状态的槽函数
    void onServerSwitchToggled(bool checked);

    void on_btn_save_user_clicked();
    void on_btn_delete_user_clicked();

    void on_btn_confirm_recharge_clicked();
    void on_btn_update_user_clicked();
    void on_btn_refresh_product_clicked();
    void on_btn_delete_product_clicked();
    void on_btn_add_product_clicked();
};
#endif // MAINWINDOW_H
