#include "MissionControl.h"

#include <algorithm>
#include <QNetworkDatagram>
#include <QSerialPortInfo>

MissionControl::MissionControl(QObject* parent)
    : QObject(parent)
{
    _udp = new QUdpSocket(this);
    _serial = new QSerialPort(this);

    connect(_udp, &QUdpSocket::readyRead, this, &MissionControl::onUdpReadyRead);
    connect(_serial, &QSerialPort::readyRead, this, &MissionControl::onSerialReadyRead);
    _uavCleanupTimer = new QTimer(this);
    _uavCleanupTimer->setInterval(1000);
    connect(_uavCleanupTimer, &QTimer::timeout, this, &MissionControl::_pruneInactiveUavs);
    _uavCleanupTimer->start();

    initMacTable();
}

MissionControl::~MissionControl()
{
    disconnectAll();
}

void MissionControl::initMacTable()
{
    // Keep in sync with onboard communication_info.py -> XBee_Devices
    _macTable.insert(1, QByteArray::fromHex("0013A2004126C97C"));
    _macTable.insert(2, QByteArray::fromHex("0013A2004154C5D3"));
    _macTable.insert(3, QByteArray::fromHex("0013A2004105EB5A"));
}

QStringList MissionControl::getSerialPorts()
{
    QStringList list;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        list << info.portName();
    }
    return list;
}

void MissionControl::connectUdp(QString ip, int port)
{
    disconnectAll();

    _targetIp = QHostAddress(ip);
    _targetPort = port;
    if (_udp->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        _useXbee = false;
        emit connectionChanged();
        emit logMessage(QString(">> UDP Listening on %1").arg(port));
    } else {
        emit logMessage(">> UDP Bind Failed!");
    }
}

void MissionControl::connectXbee(QString portName)
{
    disconnectAll();

    _serial->setPortName(portName);
    _serial->setBaudRate(57600);
    if (_serial->open(QIODevice::ReadWrite)) {
        _useXbee = true;
        emit connectionChanged();
        emit logMessage(QString(">> Xbee Opened: %1").arg(portName));
    } else {
        emit logMessage(">> Serial Open Failed!");
    }
}

void MissionControl::disconnectAll()
{
    const int prevCount = _uavTable.size();
    if (_udp->state() != QAbstractSocket::UnconnectedState) {
        _udp->close();
    }
    if (_serial->isOpen()) {
        _serial->close();
    }

    _uavTable.clear();
    _uavLastSeenMs.clear();
    _emitUavList();
    if (prevCount > 0) {
        emit activeUavCountChanged();
    }
    emit connectionChanged();
}

bool MissionControl::isConnected() const
{
    return (_useXbee && _serial->isOpen()) || (!_useXbee && _udp->state() == QAbstractSocket::BoundState);
}

void MissionControl::appendLogMessage(const QString& msg)
{
    emit logMessage(msg);
}

bool MissionControl::sendCustomPayload(int targetID, const QByteArray& payload, const QString& commandLabel)
{
    QByteArray packet = payload;
    const bool ok = sendPayload(targetID, packet);
    if (ok && !commandLabel.isEmpty()) {
        _logCommandSent(commandLabel, targetID);
    }
    return ok;
}

bool MissionControl::sendPayload(int targetID, QByteArray payload)
{
    if (!isConnected()) {
        emit logMessage(">> Error: Not Connected!");
        return false;
    }

    if (targetID < 0) {
        emit logMessage(">> Error: Invalid target ID.");
        return false;
    }

    QList<int> sendList;
    if (targetID == 0) {
        sendList = _uavTable.keys();
        if (sendList.isEmpty()) {
            // Testing fallback: if no active telemetry, still allow sending ID=0 once.
            sendList.append(0);
            emit logMessage(">> Warning: No active UAV telemetry, fallback to UAV ID 0.");
        }
    } else {
        if (!_uavTable.contains(targetID)) {
            emit logMessage(QString(">> Error: UAV %1 is not active.").arg(targetID));
            return false;
        }
        sendList.append(targetID);
    }

    std::sort(sendList.begin(), sendList.end());

    bool allOk = true;
    for (int id : sendList) {
        if (payload.size() >= 2) {
            payload[1] = static_cast<char>(id);
        }

        if (_useXbee) {
            const QByteArray mac = _macTable.value(id, QByteArray::fromHex("FFFFFFFFFFFFFFFF"));
            const QByteArray frame = PacketProtocol::wrapXbeeApiFrame(mac, payload);
            if (_serial->write(frame) < 0) {
                allOk = false;
            }
        } else {
            if (_udp->writeDatagram(payload, _targetIp, _targetPort) < 0) {
                allOk = false;
            }
        }
    }

    return allOk;
}

void MissionControl::sendArm(int id)
{
    const QByteArray pkt = PacketProtocol::packCommand(id, ProtocolEnum::Arm, ProtocolEnum::armed);
    if (sendPayload(id, pkt)) {
        _logCommandSent("ARM", id);
    }
}

void MissionControl::sendDisarm(int id)
{
    const QByteArray pkt = PacketProtocol::packCommand(id, ProtocolEnum::Arm, ProtocolEnum::disarmed);
    if (sendPayload(id, pkt)) {
        _logCommandSent("DISARM", id);
    }
}

void MissionControl::sendTakeoff(int id, int h)
{
    const QByteArray pkt = PacketProtocol::packTakeoff(id, h);
    if (sendPayload(id, pkt)) {
        _logCommandSent("TAKEOFF", id);
    }
}

void MissionControl::sendLand(int id)
{
    const QByteArray pkt = PacketProtocol::packCommand(id, ProtocolEnum::Mode_Change, ProtocolEnum::LAND);
    if (sendPayload(id, pkt)) {
        _logCommandSent("LAND", id);
    }
}

void MissionControl::sendRTL(int id)
{
    const QByteArray pkt = PacketProtocol::packCommand(id, ProtocolEnum::Mode_Change, ProtocolEnum::RTL);
    if (sendPayload(id, pkt)) {
        _logCommandSent("RTL", id);
    }
}

void MissionControl::sendMode(int id, QString mode)
{
    int mEnum = ProtocolEnum::GUIDED;
    if (mode == "LOITER") {
        mEnum = ProtocolEnum::LOITER;
    } else if (mode == "RTL") {
        mEnum = ProtocolEnum::RTL;
    } else if (mode == "LAND") {
        mEnum = ProtocolEnum::LAND;
    }

    const QByteArray pkt = PacketProtocol::packCommand(id, ProtocolEnum::Mode_Change, mEnum);
    if (sendPayload(id, pkt)) {
        _logCommandSent(QString("SET %1").arg(mode), id);
    }
}

void MissionControl::onUdpReadyRead()
{
    while (_udp->hasPendingDatagrams()) {
        const QNetworkDatagram d = _udp->receiveDatagram();
        processPacket(d.data());
    }
}

void MissionControl::onSerialReadyRead()
{
    _serialBuffer.append(_serial->readAll());

    while (_serialBuffer.contains(static_cast<char>(0x7E))) {
        const int idx = _serialBuffer.indexOf(static_cast<char>(0x7E));
        if (idx > 0) {
            _serialBuffer.remove(0, idx);
            continue;
        }
        if (_serialBuffer.size() < 3) {
            return;
        }

        const quint16 len = (static_cast<unsigned char>(_serialBuffer[1]) << 8) | static_cast<unsigned char>(_serialBuffer[2]);
        if (_serialBuffer.size() < len + 4) {
            return;
        }

        const QByteArray frame = _serialBuffer.mid(3, len);
        _serialBuffer.remove(0, len + 4);
        if (frame.size() > 12 && frame[0] == static_cast<char>(0x90)) {
            processPacket(frame.mid(12));
        }
    }
}

void MissionControl::processPacket(QByteArray data)
{
    UavStatus st;
    if (PacketProtocol::parseUavStatus(data, st)) {
        const bool isNewUav = !_uavTable.contains(st.uav_id);
        QVariantMap m;
        m["id"] = st.uav_id;
        m["frame_type"] = (st.frame_type == 0 ? "Quad" : "Fixed");
        m["bat"] = st.battery;
        m["mode"] = st.mode;
        m["armed"] = st.armed;
        m["mission"] = "Default";

        m["e"] = QString::number(st.x, 'f', 1);
        m["n"] = QString::number(st.y, 'f', 1);
        m["u"] = QString::number(st.z, 'f', 1);
        m["uav_time"] = QString::number(st.timestamp, 'f', 2);
        m["gcs_time"] = QDateTime::currentDateTime().toString("HH:mm:ss");
        m["speed"] = QString::number(st.speed, 'f', 1);

        m["roll"] = QString::number(st.roll, 'f', 1);
        m["pitch"] = QString::number(st.pitch, 'f', 1);
        m["yaw"] = QString::number(st.yaw, 'f', 1);
        m["heading"] = QString::number(st.yaw, 'f', 1);

        const ProtocolOrigin origin = PacketProtocol::defaultOrigin();
        const ProtocolPointLLA realPos = PacketProtocol::enuToLla(
            st.x,
            st.y,
            st.z,
            origin.lat,
            origin.lng,
            origin.alt
        );
        m["lat"] = QString::number(realPos.lat, 'f', 7);
        m["lng"] = QString::number(realPos.lng, 'f', 7);
        m["alt"] = QString::number(realPos.alt, 'f', 4);
        m["info"] = "OK";

        _uavTable[st.uav_id] = m;
        _uavLastSeenMs[st.uav_id] = QDateTime::currentMSecsSinceEpoch();
        _emitUavList();
        if (isNewUav) {
            emit activeUavCountChanged();
        }
        return;
    }

    int uid = 0;
    const QString info = PacketProtocol::parseInfoMessage(data, uid);
    if (!info.isEmpty()) {
        emit logMessage(QString("[UAV %1] %2").arg(uid).arg(info));
    }
}

void MissionControl::_emitUavList()
{
    QVariantList list;
    QList<int> keys = _uavTable.keys();
    std::sort(keys.begin(), keys.end());
    for (int k : keys) {
        list << _uavTable[k];
    }
    emit uavListUpdated(list);
}

void MissionControl::_logCommandSent(const QString& commandLabel, int targetID)
{
    emit logMessage(
        QString("CMD [%1] -> UAV %2")
            .arg(commandLabel)
            .arg(targetID == 0 ? "ALL" : QString::number(targetID))
    );
}

void MissionControl::_pruneInactiveUavs()
{
    if (_uavLastSeenMs.isEmpty()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<int> removeIds;
    for (auto it = _uavLastSeenMs.constBegin(); it != _uavLastSeenMs.constEnd(); ++it) {
        if (now - it.value() > _uavDataTimeoutMs) {
            removeIds.append(it.key());
        }
    }

    if (removeIds.isEmpty()) {
        return;
    }

    for (int id : removeIds) {
        _uavLastSeenMs.remove(id);
        _uavTable.remove(id);
    }

    _emitUavList();
    emit activeUavCountChanged();
}
