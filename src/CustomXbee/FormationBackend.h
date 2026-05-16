#pragma once

#include <QGeoCoordinate>
#include <QObject>

#include "MissionControl.h"
#include "QmlObjectListModel.h"

class FormationRallyPointItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate CONSTANT)
public:
    explicit FormationRallyPointItem(const QGeoCoordinate& coord, QObject* parent = nullptr)
        : QObject(parent)
        , _coord(coord)
    {
    }

    QGeoCoordinate coordinate() const { return _coord; }

private:
    QGeoCoordinate _coord;
};

class FormationBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QmlObjectListModel* rallyPoints READ rallyPoints CONSTANT)
    Q_PROPERTY(int leaderId READ leaderId WRITE setLeaderId NOTIFY configChanged)
    Q_PROPERTY(double spacing READ spacing WRITE setSpacing NOTIFY configChanged)
    Q_PROPERTY(double safeSeparation READ safeSeparation WRITE setSafeSeparation NOTIFY configChanged)
    Q_PROPERTY(double loiterRadius READ loiterRadius WRITE setLoiterRadius NOTIFY configChanged)
    Q_PROPERTY(int shape READ shape WRITE setShape NOTIFY configChanged)

public:
    explicit FormationBackend(QObject* parent = nullptr);

    void setMissionControl(MissionControl* missionControl);

    int leaderId() const { return _leaderId; }
    double spacing() const { return _spacing; }
    double safeSeparation() const { return _safeSeparation; }
    double loiterRadius() const { return _loiterRadius; }
    int shape() const { return _shape; }

    void setLeaderId(int leaderId);
    void setSpacing(double spacing);
    void setSafeSeparation(double safeSeparation);
    void setLoiterRadius(double loiterRadius);
    void setShape(int shape);

    Q_INVOKABLE bool addRallyPoint(QGeoCoordinate coord);
    Q_INVOKABLE void clearRallyPoints();

    QmlObjectListModel* rallyPoints() { return &_rallyPoints; }

signals:
    void configChanged();

private:
    static bool _toMmInt32(double meters, qint32& out);
    bool _sendSwarmCommand();
    void _log(const QString& msg) const;

    MissionControl* _missionControl = nullptr;
    QmlObjectListModel _rallyPoints;
    quint16 _pointId = 1;
    int _leaderId = 1;
    double _spacing = 70.0;
    double _safeSeparation = 45.0;
    double _loiterRadius = 120.0;
    int _shape = 4; // TRAIL
    double _standoffDistance = 5000.0;
    double _altitudeStep = 20.0;
    double _desiredTargetTime = 0.0;
};
