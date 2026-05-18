#ifndef QFACEOBJECT_H
#define QFACEOBJECT_H

#include <QObject>
#include <QMap>
#include <QVector>
#include <QString>

#include <opencv2/opencv.hpp>

#include <seeta/FaceDetector.h>
#include <seeta/FaceLandmarker.h>
#include <seeta/FaceRecognizer.h>

class qfaceobject : public QObject
{
    Q_OBJECT
public:
    explicit qfaceobject(QObject *parent = nullptr);
    ~qfaceobject();

public slots:
    int64_t face_register(cv::Mat &faceimage);
    int64_t face_query(cv::Mat &faceimage);

signals:
    void send_faceid(int faceid);

private:
    bool initSeeta();
    bool extractFeature(const cv::Mat &faceimage, QVector<float> &featureOut);
    bool saveFeatureDb();
    bool loadFeatureDb();
    int64_t generateFaceId() const;

private:
    seeta::FaceDetector *m_detector;
    seeta::FaceLandmarker *m_landmarker;
    seeta::FaceRecognizer *m_recognizer;

    bool m_inited;
    int m_featureSize;

    // key = faceId, value = 对应的人脸特征
    QMap<qint64, QVector<float>> m_featureDb;

    // 模型路径
    QString m_modelDir;
    QString m_fdModelPath;
    QString m_pdModelPath;
    QString m_frModelPath;

    // 特征库持久化文件
    QString m_featureDbPath;

    // 相似度阈值
    float m_similarityThreshold;
};

#endif // QFACEOBJECT_H
