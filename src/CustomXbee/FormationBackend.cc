#include "FormationBackend.h"

#include <QtGlobal>
#include <cmath>
#include <limits>

#include "packetprotocol.h"

namespace {
QString formationShapeName(int shape)
{
    switch (shape) {
    case 1:
        return QStringLiteral("VEE");
    case 2:
        return QStringLiteral("ECHELON_LEFT");
    case 3:
        return QStringLiteral("ECHELON_RIGHT");
    case 4:
        return QStringLiteral("TRAIL");
    case 5:
        return QStringLiteral("TRIANGLE");
    case 6:
        return QStringLiteral("WEDGE_WIDE");
    case 7:
        return QStringLiteral("ARROW");
    case 8:
        return QStringLiteral("INVERTED_VEE");
    default:
        return QString("UNKNOWN(%1)").arg(shape);
    }
}
}

FormationBackend::FormationBackend(QObject* parent)
    : QObject(parent)
{
}

void FormationBackend::setMissionControl(MissionControl* missionControl)
{
    _missionControl = missionControl;
}

void FormationBackend::setLeaderId(int leaderId)
{
    const int boundedLeaderId = qBound(0, leaderId, 255);
    if (_leaderId == boundedLeaderId) {
        return;
    }
    _leaderId = boundedLeaderId;
    emit configChanged();
}

void FormationBackend::setSpacing(double spacing)
{
    if (!std::isfinite(spacing) || spacing <= 0.0 || std::fabs(_spacing - spacing) < 1e-9) {
        return;
    }
    _spacing = spacing;
    emit configChanged();
}

void FormationBackend::setSafeSeparation(double safeSeparation)
{
    if (!std::isfinite(safeSeparation) || safeSeparation <= 0.0 || std::fabs(_safeSeparation - safeSeparation) < 1e-9) {
        return;
    }
    _safeSeparation = safeSeparation;
    emit configChanged();
}

void FormationBackend::setLoiterRadius(double loiterRadius)
{
    if (!std::isfinite(loiterRadius) || loiterRadius <= 0.0 || std::fabs(_loiterRadius - loiterRadius) < 1e-9) {
        return;
    }
    _loiterRadius = loiterRadius;
    emit configChanged();
}

void FormationBackend::setShape(int shape)
{
    const int boundedShape = qBound(1, shape, 8);
    if (_shape == boundedShape) {
        return;
    }
    _shape = boundedShape;
    emit configChanged();
}

bool FormationBackend::addRallyPoint(QGeoCoordinate coord)
{
    if (!_missionControl) {
        _log(">> Formation rally point failed: MissionControl is null.");
        return false;
    }

    if (!coord.isValid()) {
        _log(">> Formation rally point failed: invalid coordinate.");
        return false;
    }

    if (!PacketProtocol::hasOrigin()) {
        _log(">> Formation rally point failed: origin is invalid.");
        return false;
    }

    const ProtocolOrigin origin = PacketProtocol::defaultOrigin();
    const QGeoCoordinate mapCoord(coord.latitude(), coord.longitude());
    ProtocolPointENU enu {0.0, 0.0, 0.0};
    if (!PacketProtocol::coordToEnu(mapCoord, origin.lat, origin.lng, origin.alt, enu)) {
        _log(">> Formation rally point failed: coordToEnu failed.");
        return false;
    }

    qint32 eMm = 0;
    qint32 nMm = 0;
    qint32 uMm = 0;
    qint32 radiusMm = 0;
    if (!_toMmInt32(enu.e, eMm) ||
        !_toMmInt32(enu.n, nMm) ||
        !_toMmInt32(0.0, uMm) ||
        !_toMmInt32(_loiterRadius, radiusMm)) {
        _log(">> Formation rally point failed: ENU/radius out of int32 range.");
        return false;
    }

    if (!_sendSwarmCommand()) {
        _log(">> Formation config send failed.");
        return false;
    }

    const QByteArray packet = PacketProtocol::packFormationPoint(
        0,
        _pointId,
        enu.e,
        enu.n,
        0.0,
        _loiterRadius
    );

    if (!_missionControl->sendCustomPayloadByRouteTable(packet, "FORMATION_POINT")) {
        _log(">> Formation rally point send failed.");
        return false;
    }

    _rallyPoints.clearAndDeleteContents();
    _rallyPoints.append(new FormationRallyPointItem(mapCoord));

    _log(QString(">> Formation rally point sent: pointId=%1 E=%2 N=%3 U=%4 loiter=%5")
             .arg(_pointId)
             .arg(enu.e, 0, 'f', 1)
             .arg(enu.n, 0, 'f', 1)
             .arg(0.0, 0, 'f', 1)
             .arg(_loiterRadius, 0, 'f', 1));
    return true;
}

void FormationBackend::clearRallyPoints()
{
    _rallyPoints.clearAndDeleteContents();
}

bool FormationBackend::_toMmInt32(double meters, qint32& out)
{
    if (!std::isfinite(meters)) {
        return false;
    }

    const qint64 mm = qRound64(meters * 1000.0);
    if (mm < std::numeric_limits<qint32>::min() || mm > std::numeric_limits<qint32>::max()) {
        return false;
    }

    out = static_cast<qint32>(mm);
    return true;
}

bool FormationBackend::_sendSwarmCommand()
{
    if (!_missionControl) {
        return false;
    }

    qint32 spacingMm = 0;
    qint32 standoffMm = 0;
    qint32 safeSeparationMm = 0;
    qint32 altitudeStepMm = 0;
    if (!_toMmInt32(_spacing, spacingMm) ||
        !_toMmInt32(_standoffDistance, standoffMm) ||
        !_toMmInt32(_safeSeparation, safeSeparationMm) ||
        !_toMmInt32(_altitudeStep, altitudeStepMm)) {
        _log(">> Formation config failed: parameter out of int32 range.");
        return false;
    }

    const QByteArray packet = PacketProtocol::packSwarmCommand(
        0,
        true,
        _shape,
        _leaderId,
        _spacing,
        _standoffDistance,
        _safeSeparation,
        _altitudeStep,
        _desiredTargetTime
    );

    if (!_missionControl->sendCustomPayloadByRouteTable(packet, "SWARM_COMMAND")) {
        return false;
    }

    _log(QString(">> Formation config sent: shape=%1 leader=%2 spacing=%3 safeSep=%4 loiter=%5")
             .arg(formationShapeName(_shape))
             .arg(_leaderId)
             .arg(_spacing, 0, 'f', 1)
             .arg(_safeSeparation, 0, 'f', 1)
             .arg(_loiterRadius, 0, 'f', 1));
    return true;
}

void FormationBackend::_log(const QString& msg) const
{
    if (_missionControl) {
        _missionControl->appendLogMessage(msg);
    }
}
