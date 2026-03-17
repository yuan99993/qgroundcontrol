#pragma once

#include <QGeoCoordinate>
#include <QObject>
#include <QVector>
#include <limits>

#include "MissionControl.h"
#include "QmlObjectListModel.h"
#include "packetprotocol.h"

//地图上的一个 VRP 点对象（给 QML 显示）
class VrpPointItem : public QObject
{
    //
    Q_OBJECT
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate CONSTANT)
public:
    //构造时保存坐标
    explicit VrpPointItem(const QGeoCoordinate& coord, QObject* parent = nullptr)
        : QObject(parent)
        , _coord(coord)
    {
    }

    //返回当前点坐标
    QGeoCoordinate coordinate() const { return _coord; }

private:
    //存储点坐标
    QGeoCoordinate _coord;
};

class VrpBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlObjectListModel* vrpPoints READ vrpPoints CONSTANT)   //把 VRP 点列表模型暴露给 QML 地图层
    Q_PROPERTY(double draftPointAlt READ draftPointAlt WRITE setDraftPointAlt NOTIFY draftPointAltChanged)  //暴露“新加点默认相对高度”到 QML，可读写，变化会通知 UI。

public:
    explicit VrpBackend(QObject* parent = nullptr);

    void setMissionControl(MissionControl* missionControl);     //注入 MissionControl 指针，后续用它发包和写日志。
    void setOrigin(double lat, double lng, double alt);          //设置 ENU 转换原点（经纬高）
    void setDraftPointAlt(double alt);                           //设置 VRP 打点时用的默认相对高度
    double draftPointAlt() const { return _draftPointAlt; }       //读取 VRP 打点时用的默认相对高度

    Q_INVOKABLE void qmlSetOrigin(double lat, double lng, double alt) { setOrigin(lat, lng, alt); } //QML 可直接调用，内部转到 setOrigin
    Q_INVOKABLE void addVrpPoint(QGeoCoordinate coord); //添加一个 VRP 点（用当前 draftPointAlt）
    Q_INVOKABLE void addVrpPointWithAlt(QGeoCoordinate coord, double alt);  //添加点并指定该点相对高度。
    Q_INVOKABLE void clearAllVrpPoints();       //清空 VRP 点和分配结果
    Q_INVOKABLE bool runVrpAllocation();        //执行 VRP 分配算法，生成每架机的目标序列。
    Q_INVOKABLE bool uploadVrpResult(int waypointRadius = 15);  //把分配结果打包并发给对应 UAV；waypointRadius 是到点半径参数。

    QmlObjectListModel* vrpPoints() { return &_vrpPoints; }

signals:
    void draftPointAltChanged();    //高度默认值变化时通知 QML 刷新

private:
    //保存某架 UAV 的分配结果。
    //uavId 飞机ID；lastPos 当前路径末点；targetIndices 分到的目标索引；pointsEnu 分到的 ENU 点序列；routeDistance 路径累计长度。
    struct AssignedRoute {
        int uavId = 0;
        ProtocolPointENU lastPos {0.0, 0.0, 0.0};
        QVector<int> targetIndices;
        QVector<ProtocolPointENU> pointsEnu;
        double routeDistance = 0.0;
    };

    static double _distance2d(const ProtocolPointENU& a, const ProtocolPointENU& b);    //计算两点平面距离
    static bool _toMmInt32(double meters, qint32& out);     //把米转毫米并检查是否能装进 int32（协议打包需要）。
    bool _hasValidOrigin() const;       //检查原点是否有效（不是 NaN 且范围合法）。
    bool _buildGuideWaypointsPacket(int uavId,
                                    const QVector<ProtocolPointENU>& pointsEnu,
                                    int waypointRadius,
                                    QByteArray& outPacket,
                                    QString& outError) const;   //构建 guide_waypoints 协议包（包含 uavId、点序列、waypointRadius）。
    void _log(const QString& msg) const;

    MissionControl* _missionControl = nullptr;  //MissionControl的后端指针，用于调用该类的内部方法
    QmlObjectListModel _vrpPoints;      //QML 显示用的 VRP 点对象列表
    QList<QGeoCoordinate> _targets;     //保存 VRP 目标点地理坐标
    QVector<double> _targetRelAlts;     //每个目标点对应的相对高度（与 _targets 同索引）
    QVector<AssignedRoute> _assignedRoutes; //保存分配算法输出的各机路线结果

    //经纬度值，用于坐标转换
    double _originLat = std::numeric_limits<double>::quiet_NaN();
    double _originLng = std::numeric_limits<double>::quiet_NaN();
    double _originAlt = std::numeric_limits<double>::quiet_NaN();
    double _draftPointAlt = 10.0;       //VRP功能的高度值
};



