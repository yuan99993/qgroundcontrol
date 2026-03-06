#include "SeadBackend.h"

#include <QDataStream>
#include <QVector>
#include <cmath>
#include <limits>

namespace {
bool toMmInt32(double meters, qint32& out)
{
    const qint64 mm = qRound64(meters * 1000.0);
    if (mm < std::numeric_limits<qint32>::min() || mm > std::numeric_limits<qint32>::max()) {
        return false;
    }
    out = static_cast<qint32>(mm);
    return true;
}

bool isOriginValid(double lat, double lng, double alt)  //用于判断原点经纬度是否合规
{
    return std::isfinite(lat) &&
           std::isfinite(lng) &&
           std::isfinite(alt) &&
           lat >= -90.0 && lat <= 90.0 &&
           lng >= -180.0 && lng <= 180.0;
}
}

SeadBackend::SeadBackend(MissionControl* missionControl, QObject* parent)
    : QObject(parent)
    , _missionControl(missionControl)
{
    const ProtocolOrigin origin = PacketProtocol::defaultOrigin();
    _originLat = origin.lat;
    _originLng = origin.lng;
    _originAlt = origin.alt;
}

void SeadBackend::setMissionControl(MissionControl* missionControl)
{
    _missionControl = missionControl;
}

void SeadBackend::setTargetUavId(int id)
{
    if (_targetUavId == id) {
        return;
    }
    _targetUavId = id;
    emit configChanged();
}

void SeadBackend::setSeadUavType(int type)
{
    if (_seadUavType == type) {
        return;
    }
    _seadUavType = type;
    emit configChanged();
}

void SeadBackend::setSeadVelocity(double velocity)
{
    if (qFuzzyCompare(_seadVelocity, velocity)) {
        return;
    }
    _seadVelocity = velocity;
    emit configChanged();
}

void SeadBackend::setSeadRmin(double rmin)
{
    if (qFuzzyCompare(_seadRmin, rmin)) {
        return;
    }
    _seadRmin = rmin;
    emit configChanged();
}

void SeadBackend::setWaypointRadius(int radius)
{
    if (_waypointRadius == radius) {
        return;
    }
    _waypointRadius = radius;
    emit configChanged();
}

void SeadBackend::setOriginLat(double lat)
{
    if (qFuzzyCompare(_originLat, lat)) {
        return;
    }
    _originLat = lat;
    PacketProtocol::setOrigin(_originLat, _originLng, _originAlt);
    emit configChanged();
}

void SeadBackend::setOriginLng(double lng)
{
    if (qFuzzyCompare(_originLng, lng)) {
        return;
    }
    _originLng = lng;
    PacketProtocol::setOrigin(_originLat, _originLng, _originAlt);
    emit configChanged();
}

void SeadBackend::setOriginAlt(double alt)
{
    if (qFuzzyCompare(_originAlt, alt)) {
        return;
    }
    _originAlt = alt;
    PacketProtocol::setOrigin(_originLat, _originLng, _originAlt);
    emit configChanged();
}

void SeadBackend::addSeadPoint(QGeoCoordinate coord)
{
    if (!coord.isValid()) {
        _log(">> Invalid SEAD point coordinate.");
        return;
    }

    _missionPoints.append(new SeadMapPointItem(coord));
}

void SeadBackend::insertTask(QGeoCoordinate coord)
{
    if (!coord.isValid()) {
        _log(">> Invalid insert-task coordinate.");
        return;
    }

    _insertPoints.append(new SeadMapPointItem(coord));
    if (!_missionControl) {
        return;
    }

    const QByteArray packet = _buildTaskInsertPacket(_targetUavId, coord, 0);
    if (packet.isEmpty()) {
        _log(">> Task_Insert packet build failed.");
        return;
    }

    _missionControl->sendCustomPayload(_targetUavId, packet, "TASK_INSERT");
}

void SeadBackend::addZoneVertex(QGeoCoordinate coord)
{
    if (!coord.isValid()) {
        _log(">> Invalid zone vertex coordinate.");
        return;
    }

    _zonePoints.append(new SeadMapPointItem(coord));
}

void SeadBackend::clearAll()
{
    _missionPoints.clearAndDeleteContents();
    _insertPoints.clearAndDeleteContents();
    _zonePoints.clearAndDeleteContents();
}

bool SeadBackend::setOrigin(double lat, double lng, double alt)
{
    if (!isOriginValid(lat, lng, alt)) {
        _log(">> Origin input invalid. Expected lat[-90,90], lng[-180,180], alt finite.");
        return false;
    }

    const bool changed = !qFuzzyCompare(_originLat, lat) ||
                         !qFuzzyCompare(_originLng, lng) ||
                         !qFuzzyCompare(_originAlt, alt);

    _originLat = lat;
    _originLng = lng;
    _originAlt = alt;
    PacketProtocol::setOrigin(_originLat, _originLng, _originAlt);  //传经纬度给打包后端

    if (changed) {
        emit configChanged();
    }

    _log(QString(">> Origin set: lat=%1 lng=%2 alt=%3")
             .arg(_originLat, 0, 'f', 6)
             .arg(_originLng, 0, 'f', 6)
             .arg(_originAlt, 0, 'f', 2));
    return true;
}

bool SeadBackend::hasValidOrigin() const
{
    return isOriginValid(_originLat, _originLng, _originAlt);
}

void SeadBackend::sendSeadMission()
{
    if (!_missionControl) {
        _log(">> SEAD backend is not connected to MissionControl.");
        return;
    }

    if (_missionPoints.count() <= 0) {
        _log(">> No SEAD mission points.");
        return;
    }

    if (!hasValidOrigin()) {
        _log(">> Origin not set. Please input lat/lng/alt before sending SEAD.");
        return;
    }

    const QByteArray packet = _buildSeadMissionPacket(_targetUavId);
    if (packet.isEmpty()) {
        _log(">> Build SEAD mission packet failed.");
        return;
    }

    if (_missionControl->sendCustomPayload(_targetUavId, packet, "SEAD_MISSION")) {
        _log(QString(">> SEAD mission sent, targets=%1").arg(_missionPoints.count()));
    }
}

void SeadBackend::uploadZones()
{
    _log(">> Zone upload is not enabled in this backend yet.");
}

void SeadBackend::saveZonesToFile()
{
    _log(">> Zone file export is not enabled in this backend yet.");
}

QByteArray SeadBackend::_buildTaskInsertPacket(int targetId, const QGeoCoordinate& coord, int taskType) const
{
    if (!hasValidOrigin()) {
        _log(">> Origin not set. Task insert blocked.");
        return QByteArray();
    }
    ProtocolPointENU enu;
    enu.e = 0.0;
    enu.n = 0.0;
    enu.u = 0.0;
    if (!PacketProtocol::coordToEnu(coord, _originLat, _originLng, _originAlt, enu)) {
        return QByteArray();
    }

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint8 safeTargetId = static_cast<quint8>(qBound(0, targetId, 255));
    const quint8 safeTaskType = static_cast<quint8>(qBound(0, taskType, 255));
    qint32 eMm = 0;
    qint32 nMm = 0;
    if (!toMmInt32(enu.e, eMm) || !toMmInt32(enu.n, nMm)) {
        _log(">> Task_Insert out of int32 ENU range. Check origin alignment (Origin_Correction).");
        return QByteArray();
    }
    stream << quint8(ProtocolEnum::Task_Insert);
    stream << safeTargetId;
    stream << eMm;
    stream << nMm;
    stream << safeTaskType;
    return packet;
}

QByteArray SeadBackend::_buildSeadMissionPacket(int targetId)
{
    if (!hasValidOrigin()) {
        _log(">> Origin not set. SEAD mission blocked.");
        return QByteArray();
    }
    QVector<ProtocolPointENU> points;
    points.reserve(_missionPoints.count());

    for (int i = 0; i < _missionPoints.count(); ++i) {
        SeadMapPointItem* item = qobject_cast<SeadMapPointItem*>(_missionPoints.get(i));
        if (!item) {
            continue;
        }
        ProtocolPointENU enu;
        enu.e = 0.0;
        enu.n = 0.0;
        enu.u = 0.0;
        if (PacketProtocol::coordToEnu(item->coordinate(), _originLat, _originLng, _originAlt, enu)) {
            points.push_back(enu);
        }
    }

    if (points.isEmpty()) {
        return QByteArray();
    }

    const int targetCount = qMin(static_cast<int>(points.size()), 255);
    const ProtocolPointENU start = points.first();
    const ProtocolPointENU base = points.first();
    const double startHeadingDeg = 0.0;
    const double endHeadingDeg = 0.0;

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint8 safeTargetId = static_cast<quint8>(qBound(0, targetId, 255));
    const quint8 safeUavType = static_cast<quint8>(qBound(0, _seadUavType, 255));
    const quint8 safeRadius = static_cast<quint8>(qBound(0, _waypointRadius, 255));

    stream << quint8(ProtocolEnum::SEAD_mission);
    stream << safeTargetId;
    stream << safeUavType;
    stream << qint32(qRound64(_seadVelocity * 1000.0));
    stream << qint32(qRound64(_seadRmin * 1000.0));
    stream << safeRadius;

    qint32 startEMm = 0;
    qint32 startNMm = 0;
    qint32 baseEMm = 0;
    qint32 baseNMm = 0;
    if (!toMmInt32(start.e, startEMm) ||
        !toMmInt32(start.n, startNMm) ||
        !toMmInt32(base.e, baseEMm) ||
        !toMmInt32(base.n, baseNMm)) {
        _log(">> SEAD point out of int32 ENU range. Check origin alignment (Origin_Correction).");
        return QByteArray();
    }

    // init_pos: [E, N, heading_deg]
    stream << startEMm;
    stream << startNMm;
    stream << qint32(qRound64(startHeadingDeg * 1000.0));

    // end/base: [E, N, heading_deg]
    stream << baseEMm;
    stream << baseNMm;
    stream << qint32(qRound64(endHeadingDeg * 1000.0));

    stream << quint8(targetCount); // targets count
    stream << quint8(0);           // unknown targets count

    for (int i = 0; i < targetCount; ++i) {
        qint32 eMm = 0;
        qint32 nMm = 0;
        if (!toMmInt32(points[i].e, eMm) || !toMmInt32(points[i].n, nMm)) {
            _log(">> SEAD target out of int32 ENU range. Mission blocked.");
            return QByteArray();
        }
        stream << eMm;
        stream << nMm;
    }

    return packet;
}

void SeadBackend::_log(const QString& msg) const
{
    if (_missionControl) {
        _missionControl->appendLogMessage(msg);
    }
}
