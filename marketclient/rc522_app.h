#ifndef RC522WORKER_H
#define RC522WORKER_H

#include <QObject>
#include <QThread>
#include <stdint.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#define RC522_IOC_MAGIC             'r'
#define RC522_IOC_INIT              _IO(RC522_IOC_MAGIC, 0)
#define RC522_IOC_ANTENNA_ON        _IO(RC522_IOC_MAGIC, 1)
#define RC522_IOC_ANTENNA_OFF       _IO(RC522_IOC_MAGIC, 2)
#define RC522_IOC_REQUEST_A         _IOR(RC522_IOC_MAGIC, 3, __u8[2])
#define RC522_IOC_GET_UID           _IOR(RC522_IOC_MAGIC, 4, __u8[4])

class Rc522Worker : public QObject
{
    Q_OBJECT
public:
    explicit Rc522Worker(QObject *parent = nullptr);
    ~Rc522Worker();

    void stopWork();

public slots:
    void doWork();

signals:
    void cardDetected(QString uid);
    void errorOccurred(QString msg);

private:
    int fd = -1;
    bool m_running = true;
};

#endif // RC522WORKER_H
