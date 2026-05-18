#ifndef V4L2_MMAP_CAPTURE_H
#define V4L2_MMAP_CAPTURE_H

#include <QString>
#include <opencv2/opencv.hpp>

class V4L2MmapCapture
{
public:
    V4L2MmapCapture();
    ~V4L2MmapCapture();

    bool openDevice(const QString &devicePath, int width, int height);
    void closeDevice();

    bool isOpened() const;

    bool readFrame(cv::Mat &bgrFrame);

    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    struct Buffer
    {
        void *start = nullptr;
        size_t length = 0;
    };
    static void yuyvToRgb(const unsigned char *src, unsigned char *dst, int width, int height);

private:
    static int xioctl(int fd, unsigned long request, void *arg);

    bool initDevice();
    bool initMmap();
    bool startStreaming();
    void stopStreaming();
    void uninitMmap();

private:
    int m_fd;
    QString m_devicePath;
    int m_width;
    int m_height;

    Buffer *m_buffers;
    unsigned int m_bufferCount;
    bool m_streaming;
};

#endif // V4L2_MMAP_CAPTURE_H
