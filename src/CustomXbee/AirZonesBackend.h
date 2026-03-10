#pragma once

#include <QGeoCoordinate>
#include <QObject>
#include <QVariantList>
#include <limits>

#include "MissionControl.h"
#include "QmlObjectListModel.h"

//禁飞区类
class AirZonePointItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate CONSTANT)
public:
    explicit AirZonePointItem(const QGeoCoordinate& coord, QObject* parent = nullptr)
        : QObject(parent)
        , _coord(coord)
    {
    }

    QGeoCoordinate coordinate() const { return _coord; }

private:
    QGeoCoordinate _coord;
};


//禁飞区后端
class AirZonesBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlObjectListModel* zonePoints READ zonePoints CONSTANT)
    Q_PROPERTY(QVariantList zonePolygons READ zonePolygons NOTIFY zonesChanged)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zonesChanged)

public:
    explicit AirZonesBackend(QObject* parent = nullptr);        //构造函数

    void setMissionControl(MissionControl* missionControl);     //获取missionControl指针，便于调用另一个类的方法
    void setOrigin(double lat, double lng, double alt);         //设置经纬度原点，便于将禁飞区顶点数据转换

    Q_INVOKABLE void qmlSetOrigin(double lat, double lng, double alt) { setOrigin(lat, lng, alt); }
    Q_INVOKABLE void qmlAddZoneVertex(QGeoCoordinate coord) { addZoneVertex(coord); }
    Q_INVOKABLE bool qmlFinalizeCurrentZone() { return finalizeCurrentZone(); }
    Q_INVOKABLE void qmlClearAllZones() { clearAllZones(); }
    Q_INVOKABLE bool qmlUploadZones(int targetId) { return uploadZones(targetId); }
    Q_INVOKABLE QVariantList qmlGetZonePolygons() const { return zonePolygons(); }

    QmlObjectListModel* zonePoints() { return &_zonePoints; }  //给 QML 返回“点模型”（用于地图上画红色顶点点位）
    QVariantList zonePolygons() const;      //返回“多边形列表数据”（每个禁飞区一条 path），给 QML 画面/边界线
    int zoneCount() const { return _zones.size(); }     //返回当前已保存禁飞区数量

    void addZoneVertex(QGeoCoordinate coord);       //往“当前正在绘制的草稿禁飞区”加一个顶点
    bool finalizeCurrentZone();     //把草稿区保存成正式禁飞区（通常会分配 zoneId，并清空草稿）
    void clearAllZones();
    bool uploadZones(int targetId); //按协议下发禁飞区到指定 UAV（通常先清空，再分片发送每个区）

signals:
    void zonesChanged();

private:
    struct ZoneData {
        quint16 zoneId = 0;
        bool enabled = true;
        quint8 zoneType = 0;   // 0 = NoFly
        quint8 level2d = 0;
        quint8 levelH = 0;
        double minAlt = -1000.0;
        double maxAlt = 10000.0;
        QList<QGeoCoordinate> vertices;
    };

    static constexpr int kMaxFragPayload = 180;

    void _log(const QString& msg) const;        //统一写日志（通常转发到 MissionControl 日志窗口）
    bool _hasValidOrigin() const;       //校验原点是否有效（坐标转换前防错）
    void _rebuildDisplayPoints();       //把“已保存区顶点 + 草稿区顶点”刷新到 QML 点模型
    bool _buildZoneBlobV1(const ZoneData& zone, QByteArray& outBlob, QString& outError) const;
    bool _buildZoneFragments(const ZoneData& zone, int targetId, QList<QByteArray>& outPackets, QString& outError) const;

    MissionControl* _missionControl = nullptr;
    QmlObjectListModel _zonePoints;               // 给前端画顶点的模型（显示层数据）
    QList<QGeoCoordinate> _draftZoneVertices;     //当前“正在绘制但未保存”的禁飞区顶点
    QList<ZoneData> _zones;                       //已保存禁飞区集合

    double _originLat = std::numeric_limits<double>::quiet_NaN();
    double _originLng = std::numeric_limits<double>::quiet_NaN();
    double _originAlt = std::numeric_limits<double>::quiet_NaN();
    quint16 _nextZoneId = 1;
};
