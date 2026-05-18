#include "v4l2_mmap_camera.h"

#include <QDebug>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <QElapsedTimer>

V4L2MmapCapture::V4L2MmapCapture()
    : m_fd(-1)
    , m_width(0)
    , m_height(0)
    , m_buffers(nullptr)
    , m_bufferCount(0)
    , m_streaming(false)
{
}

V4L2MmapCapture::~V4L2MmapCapture()
{
    closeDevice();
}

int V4L2MmapCapture::xioctl(int fd, unsigned long request, void *arg)
{
    int ret;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

bool V4L2MmapCapture::openDevice(const QString &devicePath, int width, int height)
{
    closeDevice();

    m_devicePath = devicePath;
    m_width = width;
    m_height = height;

    m_fd = ::open(devicePath.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK, 0);
    if (m_fd < 0) {
        qWarning() << "open camera failed:" << devicePath << strerror(errno);
        return false;
    }

    //QUERYCAP
    //S_FMT
    if (!initDevice()) {
        closeDevice();
        return false;
    }

    //REQBUFS
    //QUERYBUF
    //MMAP
    if (!initMmap()) {
        closeDevice();
        return false;
    }

    //ALL QBUF
    //STREAMON
    if (!startStreaming()) {
        closeDevice();
        return false;
    }

    qDebug() << "V4L2 camera open ok:" << devicePath
             << "size =" << m_width << "x" << m_height;
    return true;
}

void V4L2MmapCapture::closeDevice()
{
    if (m_streaming) {
        stopStreaming();
    }

    uninitMmap();

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }

    m_devicePath.clear();
    m_width = 0;
    m_height = 0;
}

bool V4L2MmapCapture::isOpened() const
{
    return m_fd >= 0;
}

bool V4L2MmapCapture::initDevice()
{
    struct v4l2_capability cap;
    std::memset(&cap, 0, sizeof(cap));

    if (xioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        qWarning() << "VIDIOC_QUERYCAP failed:" << strerror(errno);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        qWarning() << "device is not video capture";
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        qWarning() << "device does not support streaming";
        return false;
    }

    struct v4l2_format fmt;
    std::memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<unsigned int>(m_width);
    fmt.fmt.pix.height = static_cast<unsigned int>(m_height);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed:" << strerror(errno);
        return false;
    }

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        qWarning() << "camera current pixel format is not YUYV";
        qWarning() << "pixelformat =" << QString::number(fmt.fmt.pix.pixelformat, 16);
        return false;
    }

    m_width = static_cast<int>(fmt.fmt.pix.width);
    m_height = static_cast<int>(fmt.fmt.pix.height);

    return true;
}

bool V4L2MmapCapture::initMmap()
{
    struct v4l2_requestbuffers req;
    std::memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
        qWarning() << "VIDIOC_REQBUFS failed:" << strerror(errno);
        return false;
    }

    if (req.count < 2) {
        qWarning() << "insufficient buffer memory";
        return false;
    }

    m_buffers = new Buffer[req.count];
    m_bufferCount = req.count;

    for (unsigned int i = 0; i < m_bufferCount; ++i) {
        struct v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QUERYBUF failed:" << strerror(errno);
            return false;
        }

        m_buffers[i].length = buf.length;
        m_buffers[i].start = ::mmap(nullptr,
                                    buf.length,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED,
                                    m_fd,
                                    buf.m.offset);

        if (m_buffers[i].start == MAP_FAILED) {
            qWarning() << "mmap failed:" << strerror(errno);
            m_buffers[i].start = nullptr;
            return false;
        }
    }

    return true;
}

bool V4L2MmapCapture::startStreaming()
{
    for (unsigned int i = 0; i < m_bufferCount; ++i) {
        struct v4l2_buffer buf;
        std::memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed:" << strerror(errno);
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed:" << strerror(errno);
        return false;
    }

    m_streaming = true;
    return true;
}

void V4L2MmapCapture::stopStreaming()
{
    if (m_fd < 0) {
        return;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMOFF, &type) < 0) {
        qWarning() << "VIDIOC_STREAMOFF failed:" << strerror(errno);
    }

    m_streaming = false;
}

void V4L2MmapCapture::uninitMmap()
{
    if (m_buffers) {
        for (unsigned int i = 0; i < m_bufferCount; ++i) {
            if (m_buffers[i].start && m_buffers[i].length > 0) {
                ::munmap(m_buffers[i].start, m_buffers[i].length);
            }
        }
        delete [] m_buffers;
        m_buffers = nullptr;
    }

    m_bufferCount = 0;
}

bool V4L2MmapCapture::readFrame(cv::Mat &bgrFrame)
{
    if (m_fd < 0 || !m_streaming) {
        return false;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    int ret = 0;
    {
        ret = ::select(m_fd + 1, &fds, nullptr, nullptr, &tv);
    }

    if (ret < 0) {
        if (errno == EINTR) {
            return false;
        }
        qWarning() << "select failed:" << strerror(errno);
        return false;
    }

    if (ret == 0) {
        return false;
    }

    struct v4l2_buffer latestBuf;
    std::memset(&latestBuf, 0, sizeof(latestBuf));
    latestBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    latestBuf.memory = V4L2_MEMORY_MMAP;

    {

        if (xioctl(m_fd, VIDIOC_DQBUF, &latestBuf) < 0)
        {
            if (errno == EAGAIN)
            {
                return false;
            }

            qWarning() << "VIDIOC_DQBUF failed:" << strerror(errno);
            return false;
        }
    }

    if (latestBuf.index >= m_bufferCount) {
        qWarning() << "buffer index out of range";
        return false;
    }

    // 丢弃旧帧，只保留最新帧
    while (true) {
        struct v4l2_buffer nextBuf;
        std::memset(&nextBuf, 0, sizeof(nextBuf));
        nextBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        nextBuf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(m_fd, VIDIOC_DQBUF, &nextBuf) < 0) {
            if (errno == EAGAIN) {
                break;
            } else {
                qWarning() << "VIDIOC_DQBUF while draining failed:" << strerror(errno);
                break;
            }
        }

        if (xioctl(m_fd, VIDIOC_QBUF, &latestBuf) < 0) {
            qWarning() << "VIDIOC_QBUF(old latestBuf) failed:" << strerror(errno);
        }

        latestBuf = nextBuf;

        if (latestBuf.index >= m_bufferCount) {
            qWarning() << "buffer index out of range while draining";
            return false;
        }
    }

    {
        cv::Mat yuyv(m_height, m_width, CV_8UC2, m_buffers[latestBuf.index].start);
        cv::cvtColor(yuyv, bgrFrame, cv::COLOR_YUV2RGB_YUYV);
    }

    {
        if (xioctl(m_fd, VIDIOC_QBUF, &latestBuf) < 0)
        {
            qWarning() << "VIDIOC_QBUF(requeue latestBuf) failed:" << strerror(errno);
            return false;
        }
    }
    return !bgrFrame.empty();
}
