#include "rc522_app.h"
#include <QDebug>
#include <errno.h>
#include <string.h>

Rc522Worker::Rc522Worker(QObject *parent)
    : QObject(parent), fd(-1), m_running(true)
{
}

Rc522Worker::~Rc522Worker()
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void Rc522Worker::stopWork()
{
    m_running = false;
}

void Rc522Worker::doWork()
{
    fd = ::open("/dev/rc522", O_RDWR);
    if (fd < 0) {
        emit errorOccurred(QString("无法打开设备 /dev/rc522: %1")
                           .arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    qDebug() << "RC522 device open success, fd =" << fd;

    auto initDevice = [this]() -> bool {
        if (::ioctl(fd, RC522_IOC_INIT) < 0) {
            qDebug() << "RC522_IOC_INIT failed:" << strerror(errno);
            return false;
        }

        if (::ioctl(fd, RC522_IOC_ANTENNA_ON) < 0) {
            qDebug() << "RC522_IOC_ANTENNA_ON failed:" << strerror(errno);
            return false;
        }

        qDebug() << "RC522 init + antenna on ok";
        QThread::msleep(50);   // 给芯片一点稳定时间
        return true;
    };

    if (!initDevice()) {
        emit errorOccurred(QString("RC522 初始化失败: %1")
                           .arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    qDebug() << "RC522 Worker started polling...";

    QString lastUid;
    int failCount = 0;
    int noCardCount = 0;

    while (m_running) {
        __u8 atqa[2] = {0};

        int ret = ::ioctl(fd, RC522_IOC_REQUEST_A, atqa);
        if (ret == 0) {
            failCount = 0;
            noCardCount = 0;

            __u8 uid[4] = {0};
            ret = ::ioctl(fd, RC522_IOC_GET_UID, uid);
            if (ret == 0) {
                QString uidStr = QString("%1%2%3%4")
                        .arg(uid[0], 2, 16, QChar('0'))
                        .arg(uid[1], 2, 16, QChar('0'))
                        .arg(uid[2], 2, 16, QChar('0'))
                        .arg(uid[3], 2, 16, QChar('0'))
                        .toUpper();

                if (uidStr != lastUid) {
                    lastUid = uidStr;
                    qDebug() << "Card Found:" << uidStr;
                    emit cardDetected(uidStr);
                }

                // 卡仍在感应区，适当停一下
                QThread::msleep(300);
            } else {
                qDebug() << "GET_UID failed:" << strerror(errno);
                failCount++;
            }
        } else {
            failCount++;
            noCardCount++;

            // 每隔一段时间打印一次，避免刷屏
//            if (failCount % 10 == 0) {
//                qDebug() << "REQUEST_A failed count =" << failCount
//                         << "," << strerror(errno);
//            }

            // 连续失败一段时间后，重新初始化 RC522
            if (failCount >= 100) {
//                qDebug() << "Too many REQUEST_A failures, re-init RC522...";
                initDevice();
                failCount = 0;
            }

            // 一段时间没检测到卡，清空 lastUid
            // 这样同一张卡拿开后再贴上来还能重新触发
            if (noCardCount >= 5) {
                lastUid.clear();
            }

            QThread::msleep(100);
        }
    }

    qDebug() << "RC522 Worker stopped.";
}
