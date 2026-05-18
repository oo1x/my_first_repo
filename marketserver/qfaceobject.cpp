#include "qfaceobject.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QDebug>

qfaceobject::qfaceobject(QObject *parent)
    : QObject(parent),
      m_detector(nullptr),
      m_landmarker(nullptr),
      m_recognizer(nullptr),
      m_inited(false),
      m_featureSize(0),
      m_similarityThreshold(0.6f)   // 这里沿用你旧代码里 0.6 的思路
{
    // 你可以把模型放到程序目录下的 ./models/
    // 例如:
    // ./models/face_detector.csta
    // ./models/face_landmarker_pts5.csta
    // ./models/face_recognizer.csta
    m_modelDir = QCoreApplication::applicationDirPath() + "/models";
    m_fdModelPath = m_modelDir + "/fd_2_00.dat";
    m_pdModelPath = m_modelDir + "/pd_2_00_pts5.dat";
    m_frModelPath = m_modelDir + "/fr_2_10.dat";

    // 特征库存储文件
    QString dataDir = QCoreApplication::applicationDirPath() + "/data";
    QDir().mkpath(dataDir);
    m_featureDbPath = dataDir + "/face_features.dat";

    m_inited = initSeeta();
    if (m_inited) {
        loadFeatureDb();
    }
}

qfaceobject::~qfaceobject()
{
    saveFeatureDb();

    delete m_detector;
    m_detector = nullptr;

    delete m_landmarker;
    m_landmarker = nullptr;

    delete m_recognizer;
    m_recognizer = nullptr;
}

bool qfaceobject::initSeeta()
{
    if (!QFile::exists(m_fdModelPath)) {
        qDebug() << "[Seeta] face detector model not found:" << m_fdModelPath;
        return false;
    }
    if (!QFile::exists(m_pdModelPath)) {
        qDebug() << "[Seeta] face landmarker model not found:" << m_pdModelPath;
        return false;
    }
    if (!QFile::exists(m_frModelPath)) {
        qDebug() << "[Seeta] face recognizer model not found:" << m_frModelPath;
        return false;
    }

    try {
        seeta::ModelSetting fdSetting;
        fdSetting.append(m_fdModelPath.toStdString().c_str());

        seeta::ModelSetting pdSetting;
        pdSetting.append(m_pdModelPath.toStdString().c_str());

        seeta::ModelSetting frSetting;
        frSetting.append(m_frModelPath.toStdString().c_str());

        m_detector = new seeta::FaceDetector(fdSetting);
        m_landmarker = new seeta::FaceLandmarker(pdSetting);
        m_recognizer = new seeta::FaceRecognizer(frSetting);

        // 可选：调一下最小人脸尺寸
        m_detector->set(seeta::FaceDetector::PROPERTY_MIN_FACE_SIZE, 40);

        m_featureSize = m_recognizer->GetExtractFeatureSize();

        qDebug() << "[Seeta] init success. feature size =" << m_featureSize;
        return true;
    } catch (...) {
        qDebug() << "[Seeta] init exception.";
        return false;
    }
}

bool qfaceobject::extractFeature(const cv::Mat &faceimage, QVector<float> &featureOut)
{
    if (!m_inited || !m_detector || !m_landmarker || !m_recognizer) {
        qDebug() << "[Seeta] not initialized.";
        return false;
    }

    if (faceimage.empty()) {
        qDebug() << "[Seeta] input image is empty.";
        return false;
    }

    cv::Mat bgr;
    if (faceimage.channels() == 3) {
        bgr = faceimage;
    } else if (faceimage.channels() == 4) {
        cv::cvtColor(faceimage, bgr, cv::COLOR_BGRA2BGR);
    } else if (faceimage.channels() == 1) {
        cv::cvtColor(faceimage, bgr, cv::COLOR_GRAY2BGR);
    } else {
        qDebug() << "[Seeta] unsupported image channels:" << faceimage.channels();
        return false;
    }

    SeetaImageData simage;
    simage.data = bgr.data;
    simage.width = bgr.cols;
    simage.height = bgr.rows;
    simage.channels = bgr.channels();   // BGR, 3 通道

    // 1. 检测所有人脸
    SeetaFaceInfoArray faces = m_detector->detect(simage);
    if (faces.size <= 0 || faces.data == nullptr) {
        qDebug() << "[Seeta] no face detected.";
        return false;
    }

    // 2. 取最大的人脸
    int bestIndex = 0;
    int bestArea = 0;
    for (int i = 0; i < faces.size; ++i) {
        int area = faces.data[i].pos.width * faces.data[i].pos.height;
        if (area > bestArea) {
            bestArea = area;
            bestIndex = i;
        }
    }

    SeetaRect bestFace = faces.data[bestIndex].pos;

    // 3. 5点关键点
    std::vector<SeetaPointF> points = m_landmarker->mark(simage, bestFace);
    if (points.size() != 5) {
        qDebug() << "[Seeta] landmark points size invalid:" << points.size();
        return false;
    }

    // 4. 提取特征
    featureOut.resize(m_featureSize);
    m_recognizer->Extract(simage, points.data(), featureOut.data());

    return true;
}

int64_t qfaceobject::generateFaceId() const
{
    if (m_featureDb.isEmpty()) {
        return 1;
    }

    qint64 maxId = 0;
    for (auto it = m_featureDb.constBegin(); it != m_featureDb.constEnd(); ++it) {
        if (it.key() > maxId) {
            maxId = it.key();
        }
    }
    return maxId + 1;
}

bool qfaceobject::saveFeatureDb()
{
    QFile file(m_featureDbPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "[Seeta] saveFeatureDb open failed:" << m_featureDbPath;
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_9);

    out << QString("FACE_FEATURE_DB_V1");
    out << m_featureSize;
    out << qint32(m_featureDb.size());

    for (auto it = m_featureDb.constBegin(); it != m_featureDb.constEnd(); ++it) {
        out << qint64(it.key());
        out << it.value();
    }

    file.close();
    qDebug() << "[Seeta] feature db saved. count =" << m_featureDb.size();
    return true;
}

bool qfaceobject::loadFeatureDb()
{
    QFile file(m_featureDbPath);
    if (!file.exists()) {
        qDebug() << "[Seeta] feature db file not found, start empty.";
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Seeta] loadFeatureDb open failed:" << m_featureDbPath;
        return false;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_9);

    QString magic;
    int featureSize = 0;
    qint32 count = 0;

    in >> magic;
    in >> featureSize;
    in >> count;

    if (magic != "FACE_FEATURE_DB_V1") {
        qDebug() << "[Seeta] invalid feature db magic.";
        return false;
    }

    if (featureSize != m_featureSize) {
        qDebug() << "[Seeta] feature size mismatch. db =" << featureSize
                 << ", current =" << m_featureSize;
        return false;
    }

    m_featureDb.clear();

    for (qint32 i = 0; i < count; ++i) {
        qint64 faceId = 0;
        QVector<float> feature;
        in >> faceId;
        in >> feature;

        if (feature.size() == m_featureSize) {
            m_featureDb.insert(faceId, feature);
        }
    }

    file.close();
    qDebug() << "[Seeta] feature db loaded. count =" << m_featureDb.size();
    return true;
}

int64_t qfaceobject::face_register(cv::Mat &faceimage)
{
    if (!m_inited) {
        qDebug() << "[Seeta] register failed: not initialized.";
        return -1;
    }

    QVector<float> feature;
    if (!extractFeature(faceimage, feature)) {
        qDebug() << "[Seeta] register failed: extract feature failed.";
        return -1;
    }

    // 可选：防止重复注册
    qint64 existedId = -1;
    float bestScore = 0.0f;

    for (auto it = m_featureDb.constBegin(); it != m_featureDb.constEnd(); ++it) {
        float sim = m_recognizer->CalculateSimilarity(feature.constData(), it.value().constData());
        if (sim > bestScore) {
            bestScore = sim;
            existedId = it.key();
        }
    }

    // 这里沿用 0.6 作为重复判定起点
    if (bestScore >= m_similarityThreshold && existedId > 0) {
        qDebug() << "[Seeta] similar face already exists. id =" << existedId
                 << ", similarity =" << bestScore;
        return existedId;
    }

    qint64 newId = generateFaceId();
    m_featureDb.insert(newId, feature);
    saveFeatureDb();

    qDebug() << "[Seeta] register success. faceId =" << newId;
    return newId;
}

int64_t qfaceobject::face_query(cv::Mat &faceimage)
{
    if (!m_inited) {
        qDebug() << "[Seeta] query failed: not initialized.";
        emit send_faceid(-1);
        return -1;
    }

    if (m_featureDb.isEmpty()) {
        qDebug() << "[Seeta] query failed: feature db is empty.";
        emit send_faceid(-1);
        return -1;
    }

    QVector<float> feature;
    if (!extractFeature(faceimage, feature)) {
        qDebug() << "[Seeta] query failed: extract feature failed.";
        emit send_faceid(-1);
        return -1;
    }

    qint64 bestId = -1;
    float bestScore = 0.0f;

    for (auto it = m_featureDb.constBegin(); it != m_featureDb.constEnd(); ++it) {
        float sim = m_recognizer->CalculateSimilarity(feature.constData(), it.value().constData());
        if (sim > bestScore) {
            bestScore = sim;
            bestId = it.key();
        }
    }

    qDebug() << "[Seeta] best match id =" << bestId << ", similarity =" << bestScore;

    if (bestScore >= m_similarityThreshold) {
        emit send_faceid(int(bestId));
        return bestId;
    }

    emit send_faceid(-1);
    return -1;
}
