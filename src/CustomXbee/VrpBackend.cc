#include "VrpBackend.h"

#include <QDataStream>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kBalanceWeight = 0.15;
}

VrpBackend::VrpBackend(QObject* parent)
    : QObject(parent)
{
}

void VrpBackend::setMissionControl(MissionControl* missionControl)
{
    _missionControl = missionControl;
}

void VrpBackend::setOrigin(double lat, double lng, double alt)
{
    _originLat = lat;
    _originLng = lng;
    _originAlt = alt;
}

void VrpBackend::setDraftPointAlt(double alt)
{
    if (!std::isfinite(alt)) {
        return;
    }

    if (std::fabs(_draftPointAlt - alt) < 1e-9) {
        return;
    }

    _draftPointAlt = alt;
    emit draftPointAltChanged();
}

void VrpBackend::addVrpPoint(QGeoCoordinate coord)
{
    addVrpPointWithAlt(coord, _draftPointAlt);
}

void VrpBackend::addVrpPointWithAlt(QGeoCoordinate coord, double alt)
{
    if (!coord.isValid()) {
        _log(">> Invalid VRP point coordinate.");
        return;
    }

    if (!std::isfinite(alt)) {
        alt = 0.0;
    }

    const QGeoCoordinate mapCoord(coord.latitude(), coord.longitude(), 0.0);

    _targets.append(mapCoord);
    _targetRelAlts.push_back(alt);
    _assignedRoutes.clear();
    _vrpPoints.append(new VrpPointItem(mapCoord));

    _log(QString(">> VRP point added: idx=%1 relAlt=%2")
             .arg(_targets.size())
             .arg(alt, 0, 'f', 1));
}

void VrpBackend::clearAllVrpPoints()
{
    _targets.clear();
    _targetRelAlts.clear();
    _assignedRoutes.clear();
    _vrpPoints.clearAndDeleteContents();
    _log(">> VRP points cleared.");
}

bool VrpBackend::runVrpAllocation()
{
    if (!_missionControl) {
        _log(">> VRP allocate failed: MissionControl is null.");
        return false;
    }

    if (_targets.isEmpty()) {
        _log(">> VRP allocate failed: no VRP points.");
        return false;
    }

    if (!_hasValidOrigin()) {
        _log(">> VRP allocate failed: origin is invalid.");
        return false;
    }

    const QVariantList uavs = _missionControl->getActiveUavEnuStates();
    if (uavs.isEmpty()) {
        _log(">> VRP allocate failed: no active UAV telemetry.");
        return false;
    }

    QVector<ProtocolPointENU> targetsEnu;
    targetsEnu.reserve(_targets.size());
    for (int i = 0; i < _targets.size(); ++i) {
        const QGeoCoordinate& coord = _targets[i];
        const double relAlt = (i < _targetRelAlts.size() && std::isfinite(_targetRelAlts[i])) ? _targetRelAlts[i] : 0.0;
        const QGeoCoordinate coordWithAbsAlt(coord.latitude(), coord.longitude(), _originAlt + relAlt);

        ProtocolPointENU enu {0.0, 0.0, 0.0};
        if (!PacketProtocol::coordToEnu(coordWithAbsAlt, _originLat, _originLng, _originAlt, enu)) {
            _log(">> VRP allocate failed: coordToEnu failed.");
            return false;
        }
        targetsEnu.push_back(enu);
    }

    _assignedRoutes.clear();
    _assignedRoutes.reserve(uavs.size());
    for (const QVariant& item : uavs) {
        const QVariantMap row = item.toMap();
        AssignedRoute route;
        route.uavId = row.value("id").toInt();
        route.lastPos.e = row.value("e").toDouble();
        route.lastPos.n = row.value("n").toDouble();
        route.lastPos.u = row.value("u").toDouble();
        _assignedRoutes.push_back(route);
    }

    QVector<int> unassigned;
    unassigned.reserve(targetsEnu.size());
    for (int i = 0; i < targetsEnu.size(); ++i) {
        unassigned.push_back(i);
    }

    while (!unassigned.isEmpty()) {
        int bestRouteIdx = -1;
        int bestTargetIdx = -1;
        double bestScore = std::numeric_limits<double>::infinity();

        for (int r = 0; r < _assignedRoutes.size(); ++r) {
            const AssignedRoute& route = _assignedRoutes[r];
            for (int idx : unassigned) {
                const double dist = _distance2d(route.lastPos, targetsEnu[idx]);
                const double score = dist + route.routeDistance * kBalanceWeight;
                if (score < bestScore) {
                    bestScore = score;
                    bestRouteIdx = r;
                    bestTargetIdx = idx;
                }
            }
        }

        if (bestRouteIdx < 0 || bestTargetIdx < 0) {
            _log(">> VRP allocate failed: internal selection error.");
            _assignedRoutes.clear();
            return false;
        }

        AssignedRoute& route = _assignedRoutes[bestRouteIdx];
        const ProtocolPointENU targetEnu = targetsEnu[bestTargetIdx];
        route.routeDistance += _distance2d(route.lastPos, targetEnu);
        route.lastPos = targetEnu;
        route.targetIndices.push_back(bestTargetIdx);
        route.pointsEnu.push_back(targetEnu);

        const auto eraseIt = std::find(unassigned.begin(), unassigned.end(), bestTargetIdx);
        if (eraseIt != unassigned.end()) {
            unassigned.erase(eraseIt);
        }
    }

    _log(QString(">> VRP allocated: targets=%1 uavs=%2")
             .arg(_targets.size())
             .arg(_assignedRoutes.size()));

    for (const AssignedRoute& route : _assignedRoutes) {
        QStringList labels;
        for (int idx : route.targetIndices) {
            labels << QString::number(idx + 1);
        }
        const QString routeText = labels.isEmpty() ? "none" : labels.join(", ");
        _log(QString(">> UAV %1 -> [%2]").arg(route.uavId).arg(routeText));
    }

    return true;
}

bool VrpBackend::uploadVrpResult(int waypointRadius)
{
    if (!_missionControl) {
        _log(">> VRP upload failed: MissionControl is null.");
        return false;
    }

    if (_assignedRoutes.isEmpty()) {
        _log(">> VRP upload failed: no allocation result. Run VRP first.");
        return false;
    }

    bool allOk = true;
    bool anySent = false;
    for (const AssignedRoute& route : _assignedRoutes) {
        if (route.pointsEnu.isEmpty()) {
            continue;
        }

        QByteArray packet;
        QString error;
        if (!_buildGuideWaypointsPacket(route.uavId, route.pointsEnu, waypointRadius, packet, error)) {
            _log(QString(">> VRP packet build failed for UAV %1: %2").arg(route.uavId).arg(error));
            allOk = false;
            continue;
        }

        if (_missionControl->sendCustomPayload(route.uavId, packet, "VRP_WAYPOINTS")) {
            anySent = true;
            _log(QString(">> VRP sent to UAV %1: points=%2")
                     .arg(route.uavId)
                     .arg(route.pointsEnu.size()));
        } else {
            allOk = false;
            _log(QString(">> VRP send failed for UAV %1.").arg(route.uavId));
        }
    }

    if (!anySent) {
        _log(">> VRP upload skipped: all routes are empty.");
        return false;
    }

    if (allOk) {
        _log(">> VRP upload done.");
    }
    return allOk;
}

double VrpBackend::_distance2d(const ProtocolPointENU& a, const ProtocolPointENU& b)
{
    const double de = b.e - a.e;
    const double dn = b.n - a.n;
    return std::sqrt(de * de + dn * dn);
}

bool VrpBackend::_toMmInt32(double meters, qint32& out)
{
    const qint64 mm = qRound64(meters * 1000.0);
    if (mm < std::numeric_limits<qint32>::min() || mm > std::numeric_limits<qint32>::max()) {
        return false;
    }
    out = static_cast<qint32>(mm);
    return true;
}

bool VrpBackend::_hasValidOrigin() const
{
    return std::isfinite(_originLat) &&
           std::isfinite(_originLng) &&
           std::isfinite(_originAlt) &&
           _originLat >= -90.0 && _originLat <= 90.0 &&
           _originLng >= -180.0 && _originLng <= 180.0;
}

bool VrpBackend::_buildGuideWaypointsPacket(int uavId,
                                            const QVector<ProtocolPointENU>& pointsEnu,
                                            int waypointRadius,
                                            QByteArray& outPacket,
                                            QString& outError) const
{
    if (uavId <= 0 || uavId > 255) {
        outError = "uav id out of range";
        return false;
    }

    if (pointsEnu.isEmpty()) {
        outError = "empty route";
        return false;
    }

    const int maxCount = qMin(pointsEnu.size(), 255);

    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << quint8(ProtocolEnum::Waypoints);
    stream << quint8(uavId);
    stream << quint8(1); // guide_waypoints
    stream << quint8(qBound(0, waypointRadius, 255));
    stream << quint8(maxCount);

    for (int i = 0; i < maxCount; ++i) {
        qint32 eMm = 0;
        qint32 nMm = 0;
        qint32 uMm = 0;
        if (!_toMmInt32(pointsEnu[i].e, eMm) ||
            !_toMmInt32(pointsEnu[i].n, nMm) ||
            !_toMmInt32(pointsEnu[i].u, uMm)) {
            outError = "ENU out of int32 range";
            return false;
        }

        stream << eMm;
        stream << nMm;
        stream << uMm;
    }

    outPacket = packet;
    return true;
}

void VrpBackend::_log(const QString& msg) const
{
    if (_missionControl) {
        _missionControl->appendLogMessage(msg);
    }
}




