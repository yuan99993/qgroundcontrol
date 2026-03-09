#include "AirZonesBackend.h"

#include <QDataStream>
#include <QVariantMap>
#include <QtMath>
#include <cmath>
#include <limits>

AirZonesBackend::AirZonesBackend(QObject* parent)
    : QObject(parent)
{
}

void AirZonesBackend::setMissionControl(MissionControl* missionControl)
{
    _missionControl = missionControl;
}

void AirZonesBackend::setOrigin(double lat, double lng, double alt)
{
    _originLat = lat;
    _originLng = lng;
    _originAlt = alt;
}

QVariantList AirZonesBackend::zonePolygons() const
{
    QVariantList list;
    for (const ZoneData& zone : _zones) {
        QVariantMap row;
        row["zoneId"] = zone.zoneId;
        row["enabled"] = zone.enabled;
        row["minAlt"] = zone.minAlt;
        row["maxAlt"] = zone.maxAlt;

        QVariantList path;
        for (const QGeoCoordinate& c : zone.vertices) {
            QVariantMap pt;
            pt["lat"] = c.latitude();
            pt["lng"] = c.longitude();
            pt["alt"] = c.altitude();
            path.append(pt);
        }
        row["path"] = path;
        list.append(row);
    }
    return list;
}

void AirZonesBackend::addZoneVertex(QGeoCoordinate coord)
{
    if (!coord.isValid()) {
        _log(">> Invalid zone vertex coordinate.");
        return;
    }

    _draftZoneVertices.append(coord);
    _rebuildDisplayPoints();
    emit zonesChanged();
}

bool AirZonesBackend::finalizeCurrentZone()
{
    if (_draftZoneVertices.size() < 3) {
        _log(">> Zone draw failed: need at least 3 vertices.");
        return false;
    }

    ZoneData zone;
    zone.zoneId = _nextZoneId++;
    zone.vertices = _draftZoneVertices;
    _zones.append(zone);
    _draftZoneVertices.clear();

    _rebuildDisplayPoints();
    emit zonesChanged();

    _log(QString(">> Zone saved: id=%1 vertices=%2 totalZones=%3")
             .arg(zone.zoneId)
             .arg(zone.vertices.size())
             .arg(_zones.size()));
    return true;
}

void AirZonesBackend::clearAllZones()
{
    _zones.clear();
    _draftZoneVertices.clear();
    _nextZoneId = 1;

    _rebuildDisplayPoints();
    emit zonesChanged();
    _log(">> Airspace zones cleared.");
}

bool AirZonesBackend::uploadZones(int targetId)
{
    if (!_missionControl) {
        _log(">> Airspace upload failed: MissionControl is null.");
        return false;
    }

    if (!_hasValidOrigin()) {
        _log(">> Airspace upload failed: origin is invalid.");
        return false;
    }

    if (_zones.isEmpty()) {
        _log(">> Airspace upload skipped: no saved zones.");
        return false;
    }
    if (!_draftZoneVertices.isEmpty()) {
        _log(">> Airspace upload note: current drawing zone is not saved, only saved zones will be uploaded.");
    }

    const QByteArray clearPacket = PacketProtocol::packCommand(targetId, ProtocolEnum::Airspace_Clear, 0);
    bool allOk = _missionControl->sendCustomPayload(targetId, clearPacket, QString());
    if (!allOk) {
        _log(">> Airspace upload warning: clear command send failed.");
    }

    _log(QString(">> Airspace upload start: zones=%1 target=%2")
             .arg(_zones.size())
             .arg(targetId));

    for (const ZoneData& zone : _zones) {
        QList<QByteArray> packets;
        QString error;
        if (!_buildZoneFragments(zone, targetId, packets, error)) {
            _log(QString(">> Zone %1 build failed: %2").arg(zone.zoneId).arg(error));
            allOk = false;
            continue;
        }

        bool zoneOk = true;
        for (const QByteArray& pkt : packets) {
            if (!_missionControl->sendCustomPayload(targetId, pkt, QString())) {
                zoneOk = false;
                allOk = false;
            }
        }

        _log(QString(">> Zone %1 upload %2: frags=%3 verts=%4")
                 .arg(zone.zoneId)
                 .arg(zoneOk ? "ok" : "failed")
                 .arg(packets.size())
                 .arg(zone.vertices.size()));
    }

    return allOk;
}

void AirZonesBackend::_log(const QString& msg) const
{
    if (_missionControl) {
        _missionControl->appendLogMessage(msg);
    }
}

bool AirZonesBackend::_hasValidOrigin() const
{
    return std::isfinite(_originLat) &&
           std::isfinite(_originLng) &&
           std::isfinite(_originAlt) &&
           _originLat >= -90.0 && _originLat <= 90.0 &&
           _originLng >= -180.0 && _originLng <= 180.0;
}

void AirZonesBackend::_rebuildDisplayPoints()
{
    _zonePoints.clearAndDeleteContents();

    for (const ZoneData& zone : _zones) {
        for (const QGeoCoordinate& c : zone.vertices) {
            _zonePoints.append(new AirZonePointItem(c));
        }
    }

    for (const QGeoCoordinate& c : _draftZoneVertices) {
        _zonePoints.append(new AirZonePointItem(c));
    }
}

bool AirZonesBackend::_buildZoneBlobV1(const ZoneData& zone, QByteArray& outBlob, QString& outError) const
{
    if (!_hasValidOrigin()) {
        outError = "origin invalid";
        return false;
    }

    const int vertexCount = zone.vertices.size();
    if (vertexCount < 3) {
        outError = "zone vertex count < 3";
        return false;
    }
    if (vertexCount > std::numeric_limits<quint16>::max()) {
        outError = "zone vertex count too large";
        return false;
    }

    auto toMmInt32 = [](double meters, qint32& out) -> bool {
        const qint64 mm = qRound64(meters * 1000.0);
        if (mm < std::numeric_limits<qint32>::min() || mm > std::numeric_limits<qint32>::max()) {
            return false;
        }
        out = static_cast<qint32>(mm);
        return true;
    };

    qint32 minAltMm = 0;
    qint32 maxAltMm = 0;
    if (!toMmInt32(zone.minAlt, minAltMm) || !toMmInt32(zone.maxAlt, maxAltMm)) {
        outError = "altitude out of range";
        return false;
    }

    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << quint8(1);                              // version
    stream << quint8(zone.zoneType);                  // zone_type
    stream << quint8(zone.enabled ? 1 : 0);           // enabled
    stream << quint8(zone.level2d);                   // level2d
    stream << quint8(zone.levelH);                    // levelH
    stream << qint32(minAltMm);                       // minAlt(mm)
    stream << qint32(maxAltMm);                       // maxAlt(mm)
    stream << quint16(static_cast<quint16>(vertexCount));

    for (const QGeoCoordinate& coord : zone.vertices) {
        ProtocolPointENU enu;
        enu.e = 0.0;
        enu.n = 0.0;
        enu.u = 0.0;
        if (!PacketProtocol::coordToEnu(coord, _originLat, _originLng, _originAlt, enu)) {
            outError = "coordToEnu failed";
            return false;
        }

        qint32 eMm = 0;
        qint32 nMm = 0;
        if (!toMmInt32(enu.e, eMm) || !toMmInt32(enu.n, nMm)) {
            outError = "ENU out of int32 range";
            return false;
        }

        stream << qint32(eMm);
        stream << qint32(nMm);
    }

    outBlob = blob;
    return true;
}

bool AirZonesBackend::_buildZoneFragments(const ZoneData& zone, int targetId, QList<QByteArray>& outPackets, QString& outError) const
{
    QByteArray blob;
    if (!_buildZoneBlobV1(zone, blob, outError)) {
        return false;
    }

    const int total = (blob.size() + kMaxFragPayload - 1) / kMaxFragPayload;
    if (total <= 0 || total > 255) {
        outError = "fragment count out of range";
        return false;
    }

    const quint8 safeTargetId = static_cast<quint8>(qBound(0, targetId, 255));
    const quint8 totalU8 = static_cast<quint8>(total);

    outPackets.clear();
    outPackets.reserve(total);
    for (int idx = 0; idx < total; ++idx) {
        const int offset = idx * kMaxFragPayload;
        const int chunkSize = qMin(kMaxFragPayload, blob.size() - offset);
        const QByteArray chunk = blob.mid(offset, chunkSize);

        QByteArray packet;
        QDataStream stream(&packet, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint8(ProtocolEnum::Airspace_ZoneFrag);
        stream << safeTargetId;
        stream << quint16(zone.zoneId);
        stream << quint8(idx);
        stream << totalU8;
        stream << quint8(chunk.size());
        packet.append(chunk);

        outPackets.append(packet);
    }

    return true;
}
