#pragma once

#include <QGeoCoordinate>
#include <QObject>
#include <QVector>
#include <limits>

#include "MissionControl.h"
#include "QmlObjectListModel.h"
#include "packetprotocol.h"

class VrpPointItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate CONSTANT)
public:
    explicit VrpPointItem(const QGeoCoordinate& coord, QObject* parent = nullptr)
        : QObject(parent)
        , _coord(coord)
    {
    }

    QGeoCoordinate coordinate() const { return _coord; }

private:
    QGeoCoordinate _coord;
};

class VrpBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlObjectListModel* vrpPoints READ vrpPoints CONSTANT)
    Q_PROPERTY(double draftPointAlt READ draftPointAlt WRITE setDraftPointAlt NOTIFY draftPointAltChanged)

public:
    explicit VrpBackend(QObject* parent = nullptr);

    void setMissionControl(MissionControl* missionControl);
    void setOrigin(double lat, double lng, double alt);
    void setDraftPointAlt(double alt);
    double draftPointAlt() const { return _draftPointAlt; }

    Q_INVOKABLE void qmlSetOrigin(double lat, double lng, double alt) { setOrigin(lat, lng, alt); }
    Q_INVOKABLE void addVrpPoint(QGeoCoordinate coord);
    Q_INVOKABLE void addVrpPointWithAlt(QGeoCoordinate coord, double alt);
    Q_INVOKABLE void clearAllVrpPoints();
    Q_INVOKABLE bool runVrpAllocation();
    Q_INVOKABLE bool uploadVrpResult(int waypointRadius = 15);

    QmlObjectListModel* vrpPoints() { return &_vrpPoints; }

signals:
    void draftPointAltChanged();

private:
    struct AssignedRoute {
        int uavId = 0;
        ProtocolPointENU lastPos {0.0, 0.0, 0.0};
        QVector<int> targetIndices;
        QVector<ProtocolPointENU> pointsEnu;
        double routeDistance = 0.0;
    };

    static double _distance2d(const ProtocolPointENU& a, const ProtocolPointENU& b);
    static bool _toMmInt32(double meters, qint32& out);
    bool _hasValidOrigin() const;
    bool _buildGuideWaypointsPacket(int uavId,
                                    const QVector<ProtocolPointENU>& pointsEnu,
                                    int waypointRadius,
                                    QByteArray& outPacket,
                                    QString& outError) const;
    void _log(const QString& msg) const;

    MissionControl* _missionControl = nullptr;
    QmlObjectListModel _vrpPoints;
    QList<QGeoCoordinate> _targets;
    QVector<double> _targetRelAlts;
    QVector<AssignedRoute> _assignedRoutes;

    double _originLat = std::numeric_limits<double>::quiet_NaN();
    double _originLng = std::numeric_limits<double>::quiet_NaN();
    double _originAlt = std::numeric_limits<double>::quiet_NaN();
    double _draftPointAlt = 0.0;
};



