#pragma once

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

#include "packetprotocol.h"

class MissionControl : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isConnected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(int activeUavCount READ activeUavCount NOTIFY activeUavCountChanged)

public:
    explicit MissionControl(QObject* parent = nullptr);
    ~MissionControl();

    Q_INVOKABLE QStringList getSerialPorts();   //枚举本机串口列表（给 Xbee 下拉框用）。
    Q_INVOKABLE void connectUdp(QString ip, int port);  //启动 UDP 模式并绑定ip和端口，准备收发
    Q_INVOKABLE void connectXbee(QString portName); //启动 Xbee 模式并绑定COM口，准备收发
    Q_INVOKABLE void disconnectAll();        //关闭 UDP/串口，清空在线无人机状态表

    //发送指令
    Q_INVOKABLE void sendArm(int targetID);
    Q_INVOKABLE void sendDisarm(int targetID);
    Q_INVOKABLE void sendTakeoff(int targetID, int height);
    Q_INVOKABLE void sendLand(int targetID);
    Q_INVOKABLE void sendRTL(int targetID);
    Q_INVOKABLE void sendMode(int targetID, QString modeName);

    bool isConnected() const;       //返回当前链路是否连上（UDP是否连接或者Xbee是否连上）
    int activeUavCount() const { return _uavTable.size(); }     //返回当前在线无人机数量
    bool sendCustomPayload(int targetID, const QByteArray& payload, const QString& commandLabel = QString());
    void appendLogMessage(const QString& msg);

signals:
    void connectionChanged();
    void activeUavCountChanged();
    void logMessage(QString msg);
    void uavListUpdated(QVariantList list);

private slots:
    void onUdpReadyRead();
    void onSerialReadyRead();
    void _pruneInactiveUavs();  //定时清理超时未回传的 UAV，防止“假在线”

private:
    QUdpSocket*  _udp       = nullptr;
    QSerialPort* _serial    = nullptr;
    bool         _useXbee   = false;

    QHostAddress _targetIp;
    int          _targetPort = 0;
    QByteArray   _serialBuffer;

    QMap<int, QVariantMap> _uavTable;
    QMap<int, QByteArray>  _macTable;
    QMap<int, qint64>      _uavLastSeenMs;
    QTimer*                _uavCleanupTimer = nullptr;
    int                    _uavDataTimeoutMs = 3000;

    void initMacTable();
    bool sendPayload(int targetID, QByteArray payload);     //真正发送函数（UDP/Xbee分支 + 多机分发 + fallback逻辑）。
    void processPacket(QByteArray data);        //解析机载回传包：遥测更新状态表、文本消息写日志
    void _emitUavList();       //把内部 _uavTable 转成 QVariantList 发给 QML
    void _logCommandSent(const QString& commandLabel, int targetID);
};
