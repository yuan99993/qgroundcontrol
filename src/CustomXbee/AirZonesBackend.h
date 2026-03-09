#pragma once

#include <QGeoCoordinate>
#include <QObject>
#include <QVariantList>
#include <limits>

#include "MissionControl.h"
#include "QmlObjectListModel.h"

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

class AirZonesBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlObjectListModel* zonePoints READ zonePoints CONSTANT)
    Q_PROPERTY(QVariantList zonePolygons READ zonePolygons NOTIFY zonesChanged)
    Q_PROPERTY(int zoneCount READ zoneCount NOTIFY zonesChanged)

public:
    explicit AirZonesBackend(QObject* parent = nullptr);

    void setMissionControl(MissionControl* missionControl);
    void setOrigin(double lat, double lng, double alt);

    QmlObjectListModel* zonePoints() { return &_zonePoints; }
    QVariantList zonePolygons() const;
    int zoneCount() const { return _zones.size(); }

    void addZoneVertex(QGeoCoordinate coord);
    bool finalizeCurrentZone();
    void clearAllZones();
    bool uploadZones(int targetId);

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

    void _log(const QString& msg) const;
    bool _hasValidOrigin() const;
    void _rebuildDisplayPoints();
    bool _buildZoneBlobV1(const ZoneData& zone, QByteArray& outBlob, QString& outError) const;
    bool _buildZoneFragments(const ZoneData& zone, int targetId, QList<QByteArray>& outPackets, QString& outError) const;

    MissionControl* _missionControl = nullptr;
    QmlObjectListModel _zonePoints;               // display points: saved zones + current drawing zone
    QList<QGeoCoordinate> _draftZoneVertices;     // vertices for the zone currently being drawn
    QList<ZoneData> _zones;                       // saved zones

    double _originLat = std::numeric_limits<double>::quiet_NaN();
    double _originLng = std::numeric_limits<double>::quiet_NaN();
    double _originAlt = std::numeric_limits<double>::quiet_NaN();
    quint16 _nextZoneId = 1;
};
