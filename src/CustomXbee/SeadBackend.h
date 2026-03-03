#pragma once

#include <QGeoCoordinate>
#include <QObject>

#include "MissionControl.h"
#include "QmlObjectListModel.h"

class SeadMapPointItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate CONSTANT)

public:
    explicit SeadMapPointItem(const QGeoCoordinate& coord, QObject* parent = nullptr)
        : QObject(parent)
        , _coord(coord)
    {
    }

    QGeoCoordinate coordinate() const { return _coord; }

private:
    QGeoCoordinate _coord;
};

class SeadBackend : public QObject
{
    Q_OBJECT    /*  必须写在类里（通常第一行）。
                    开启 Qt 元对象能力：signals、slots、Q_PROPERTY、反射。
                    少了它，很多 Qt 功能会直接失效。 */

    /* Q_PROPERTY(...) 把 C++ 成员“声明成属性”，让 QML 能直接访问。 */
    Q_PROPERTY(QmlObjectListModel* missionPoints READ missionPoints CONSTANT)   //任务点
    Q_PROPERTY(QmlObjectListModel* insertPoints READ insertPoints CONSTANT)     //插入点
    Q_PROPERTY(QmlObjectListModel* zonePoints READ zonePoints CONSTANT)         //禁飞区顶点

    // 配置参数
    Q_PROPERTY(int targetUavId READ targetUavId WRITE setTargetUavId NOTIFY configChanged)  // 目标无人机ID，0表示广播给所有无人机
    Q_PROPERTY(int seadUavType READ seadUavType WRITE setSeadUavType NOTIFY configChanged)  // SEAD任务的飞机类型，1=旋翼，2=固定翼
    Q_PROPERTY(double seadVelocity READ seadVelocity WRITE setSeadVelocity NOTIFY configChanged)    // SEAD任务的飞行速度，单位m/s
    Q_PROPERTY(double seadRmin READ seadRmin WRITE setSeadRmin NOTIFY configChanged)                // SEAD任务的飞行半径，单位m
    Q_PROPERTY(int waypointRadius READ waypointRadius WRITE setWaypointRadius NOTIFY configChanged) // 航点任务的触发半径，单位m

public:
    explicit SeadBackend(MissionControl* missionControl = nullptr, QObject* parent = nullptr);

    QmlObjectListModel* missionPoints() { return &_missionPoints; } //任务点列表，维护一个列表，每个元素是一个SeadMapPointItem对象，包含一个QGeoCoordinate坐标，在点击SEAD按钮后下发该列表中的点
    QmlObjectListModel* insertPoints() { return &_insertPoints; }   //插入点列表
    QmlObjectListModel* zonePoints() { return &_zonePoints; }       //禁飞区顶点列表

    //获取配置参数的接口
    int targetUavId() const { return _targetUavId; }
    int seadUavType() const { return _seadUavType; }
    double seadVelocity() const { return _seadVelocity; }
    double seadRmin() const { return _seadRmin; }
    int waypointRadius() const { return _waypointRadius; }

    //修改配置参数的接口
    void setMissionControl(MissionControl* missionControl);
    void setTargetUavId(int id);
    void setSeadUavType(int type);
    void setSeadVelocity(double velocity);
    void setSeadRmin(double rmin);
    void setWaypointRadius(int radius);


    //QML可调用的接口
    /* Q_INVOKABLE 标记函数可被 QML 直接调用。 */
    Q_INVOKABLE void addSeadPoint(QGeoCoordinate coord);
    Q_INVOKABLE void insertTask(QGeoCoordinate coord);
    Q_INVOKABLE void addZoneVertex(QGeoCoordinate coord);
    Q_INVOKABLE void clearAll();


    Q_INVOKABLE void sendSeadMission();
    Q_INVOKABLE void uploadZones();
    Q_INVOKABLE void saveZonesToFile();

signals:
    void configChanged();

private:
    QByteArray _buildSeadMissionPacket(int targetId);   //把当前 _missionPoints 里的点打包成 SEAD_mission (msg_id=18) 二进制数据帧。返回可直接发送的字节包（QByteArray
    QByteArray _buildTaskInsertPacket(int targetId, const QGeoCoordinate& coord, int taskType) const;   //把一个“中途插入任务点”打包成 Task_Insert (msg_id=19)
    void _log(const QString& msg) const;    //把文本转发给 MissionControl，显示到窗口日志框

    MissionControl* _missionControl = nullptr;  //指向通信后端（UDP/Xbee）的指针
    QmlObjectListModel _missionPoints;
    QmlObjectListModel _insertPoints;
    QmlObjectListModel _zonePoints;

    int _targetUavId = 0;     // 0 = all active UAVs
    int _seadUavType = 2;
    double _seadVelocity = 18.0;
    double _seadRmin = 35.0;
    int _waypointRadius = 15;
};
