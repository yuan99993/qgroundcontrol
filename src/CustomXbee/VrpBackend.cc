#include "VrpBackend.h"

#include <QDataStream>
#include <QHash>
#include <QRandomGenerator>
#include <QVariantMap>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int kPopulationSize = 300;
constexpr int kCrossoverNum = 200;
constexpr int kMutationNum = 96;
constexpr int kElitismNum = 4;
constexpr int kIterationTimes = 100;
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

    struct UavState {
        int id = 0;
        ProtocolPointENU startPos {0.0, 0.0, 0.0};
    };

    QVector<UavState> uavStates;
    uavStates.reserve(uavs.size());
    for (const QVariant& item : uavs) {
        const QVariantMap row = item.toMap();
        UavState state;
        state.id = row.value("id").toInt();
        state.startPos.e = row.value("e").toDouble();
        state.startPos.n = row.value("n").toDouble();
        state.startPos.u = row.value("u").toDouble();
        uavStates.push_back(state);
    }

    const int uavCount = uavStates.size();
    const int targetCount = _targets.size();
    if (uavCount <= 0 || targetCount <= 0) {
        _log(">> VRP allocate failed: empty UAVs or targets.");
        return false;
    }

    QVector<ProtocolPointENU> targetsEnu;
    targetsEnu.reserve(targetCount);
    for (int i = 0; i < targetCount; ++i) {
        const QGeoCoordinate& coord = _targets[i];
        const double relAlt = (i < _targetRelAlts.size() && std::isfinite(_targetRelAlts[i])) ? _targetRelAlts[i] : 0.0;
        // 目标点的平面位置来自地图，经纬度转 ENU；高度则直接采用用户输入的相对高度。
        // 这样文本框里填 10m，机载端收到的 U 就严格是 10m，不混入同高点投影到 ENU 切平面时的微小曲率偏差。
        const QGeoCoordinate coordAtOriginAlt(coord.latitude(), coord.longitude(), _originAlt);

        ProtocolPointENU enu {0.0, 0.0, 0.0};
        if (!PacketProtocol::coordToEnu(coordAtOriginAlt, _originLat, _originLng, _originAlt, enu)) {
            _log(">> VRP allocate failed: coordToEnu failed.");
            return false;
        }
        enu.u = relAlt;
        targetsEnu.push_back(enu);
    }

    QVector<QVector<double>> baseCost(targetCount + 1, QVector<double>(targetCount + 1, 0.0));
    for (int i = 1; i <= targetCount; ++i) {
        for (int j = 1; j <= targetCount; ++j) {
            baseCost[i][j] = _distance2d(targetsEnu[i - 1], targetsEnu[j - 1]);
        }
    }

    QVector<QVector<QVector<double>>> costTables(
        uavCount, QVector<QVector<double>>(targetCount + 1, QVector<double>(targetCount + 1, 0.0)));
    for (int u = 0; u < uavCount; ++u) {
        costTables[u] = baseCost;
        for (int t = 1; t <= targetCount; ++t) {
            const double d = _distance2d(uavStates[u].startPos, targetsEnu[t - 1]);
            costTables[u][0][t] = d;
            costTables[u][t][0] = d;
        }
    }

    QHash<int, int> uavIdToIndex;
    QVector<int> uavIds;
    uavIds.reserve(uavCount);
    for (int i = 0; i < uavCount; ++i) {
        uavIdToIndex.insert(uavStates[i].id, i);
        uavIds.push_back(uavStates[i].id);
    }

    struct GAChromosome {
        QVector<int> orderGene;
        QVector<int> assignGene;
        double fitness = 0.0;
        double weight = 0.0;
    };

    auto randInt = [](int maxExclusive) -> int {
        return (maxExclusive > 0) ? QRandomGenerator::global()->bounded(maxExclusive) : 0;
    };
    auto randProb = []() -> double {
        return QRandomGenerator::global()->generateDouble();
    };

    auto makeInitialPopulation = [&]() -> QVector<GAChromosome> {
        QVector<GAChromosome> population;
        population.reserve(kPopulationSize);
        for (int i = 0; i < kPopulationSize; ++i) {
            GAChromosome c;
            c.orderGene.resize(targetCount);
            c.assignGene.resize(targetCount);

            for (int j = 0; j < targetCount; ++j) {
                c.orderGene[j] = j + 1;
            }
            for (int j = targetCount - 1; j > 0; --j) {
                const int k = randInt(j + 1);
                qSwap(c.orderGene[j], c.orderGene[k]);
            }

            for (int j = 0; j < targetCount; ++j) {
                c.assignGene[j] = uavIds[randInt(uavCount)];
            }

            population.push_back(c);
        }
        return population;
    };

    auto evaluateFitness = [&](QVector<GAChromosome>& population) {
        double fitnessSum = 0.0;

        for (GAChromosome& c : population) {
            QVector<int> uavState(uavCount, 0);
            QVector<double> uavCost(uavCount, 0.0);

            for (int j = 0; j < targetCount; ++j) {
                const int uavId = c.assignGene[j];
                const int uavIdx = uavIdToIndex.value(uavId, -1);
                const int target = c.orderGene[j];
                if (uavIdx < 0 || target < 1 || target > targetCount) {
                    continue;
                }

                uavCost[uavIdx] += costTables[uavIdx][uavState[uavIdx]][target];
                uavState[uavIdx] = target;
            }

            for (int u = 0; u < uavCount; ++u) {
                uavCost[u] += costTables[u][uavState[u]][0];
            }

            const double maxCost = *std::max_element(uavCost.begin(), uavCost.end());
            c.fitness = (maxCost > 1e-9) ? (1.0 / maxCost) : 1e9;
            fitnessSum += c.fitness;
        }

        if (fitnessSum <= 1e-12) {
            const double step = 1.0 / qMax(1, population.size());
            double acc = 0.0;
            for (GAChromosome& c : population) {
                acc += step;
                c.weight = acc;
            }
            return;
        }

        double acc = 0.0;
        for (GAChromosome& c : population) {
            acc += (c.fitness / fitnessSum);
            c.weight = acc;
        }
    };

    auto selectRoulette = [&](const QVector<GAChromosome>& population, int count) -> QVector<GAChromosome> {
        QVector<GAChromosome> selected;
        selected.reserve(count);
        for (int i = 0; i < count; ++i) {
            const double p = randProb();
            bool found = false;
            for (const GAChromosome& c : population) {
                if (c.weight >= p) {
                    selected.push_back(c);
                    found = true;
                    break;
                }
            }
            if (!found && !population.isEmpty()) {
                selected.push_back(population.last());
            }
        }
        return selected;
    };

    auto crossover = [&](const GAChromosome& parent1, const GAChromosome& parent2) -> QVector<GAChromosome> {
        GAChromosome offspring1;
        GAChromosome offspring2;
        offspring1.orderGene = QVector<int>(targetCount, 0);
        offspring1.assignGene = QVector<int>(targetCount, 0);
        offspring2.orderGene = QVector<int>(targetCount, 0);
        offspring2.assignGene = QVector<int>(targetCount, 0);

        int cut0 = randInt(targetCount);
        int cut1 = randInt(targetCount);
        if (cut0 > cut1) {
            std::swap(cut0, cut1);
        }

        for (int k = cut0; k < cut1; ++k) {
            offspring2.orderGene[k] = parent1.orderGene[k];
            offspring2.assignGene[k] = parent1.assignGene[k];
            offspring1.orderGene[k] = parent2.orderGene[k];
            offspring1.assignGene[k] = parent2.assignGene[k];
        }

        for (int i = 0; i < targetCount; ++i) {
            if (!offspring1.orderGene.contains(parent1.orderGene[i])) {
                const int idx = offspring1.orderGene.indexOf(0);
                if (idx >= 0) {
                    offspring1.orderGene[idx] = parent1.orderGene[i];
                    offspring1.assignGene[idx] = parent1.assignGene[i];
                }
            }
            if (!offspring2.orderGene.contains(parent2.orderGene[i])) {
                const int idx = offspring2.orderGene.indexOf(0);
                if (idx >= 0) {
                    offspring2.orderGene[idx] = parent2.orderGene[i];
                    offspring2.assignGene[idx] = parent2.assignGene[i];
                }
            }
        }

        return {offspring1, offspring2};
    };

    auto mutate = [&](const GAChromosome& in) -> GAChromosome {
        GAChromosome out = in;
        if (targetCount <= 0) {
            return out;
        }

        if (randProb() > 0.5) {
            const int idx = randInt(targetCount);
            out.assignGene[idx] = uavIds[randInt(uavCount)];
        } else {
            int rev0 = randInt(targetCount);
            int rev1 = randInt(targetCount);
            if (rev0 > rev1) {
                std::swap(rev0, rev1);
            }
            if (rev1 > rev0) {
                std::reverse(out.orderGene.begin() + rev0, out.orderGene.begin() + rev1);
                std::reverse(out.assignGene.begin() + rev0, out.assignGene.begin() + rev1);
            }
        }
        return out;
    };

    auto elitism = [&](const QVector<GAChromosome>& population) -> QVector<GAChromosome> {
        QVector<GAChromosome> sorted = population;
        std::sort(sorted.begin(), sorted.end(), [](const GAChromosome& a, const GAChromosome& b) {
            return a.fitness > b.fitness;
        });
        const int takeN = qMin(kElitismNum, sorted.size());
        QVector<GAChromosome> out;
        out.reserve(takeN);
        for (int i = 0; i < takeN; ++i) {
            out.push_back(sorted[i]);
        }
        return out;
    };

    QVector<GAChromosome> population = makeInitialPopulation();
    if (population.isEmpty()) {
        _log(">> VRP allocate failed: population init failed.");
        return false;
    }
    evaluateFitness(population);

    for (int iter = 0; iter < kIterationTimes; ++iter) {
        QVector<GAChromosome> nextPopulation;
        nextPopulation.reserve(kPopulationSize);

        const QVector<GAChromosome> elites = elitism(population);
        for (const GAChromosome& c : elites) {
            nextPopulation.push_back(c);
        }

        for (int j = 0; j < kCrossoverNum; j += 2) {
            const QVector<GAChromosome> parents = selectRoulette(population, 2);
            if (parents.size() < 2) {
                break;
            }
            const QVector<GAChromosome> children = crossover(parents[0], parents[1]);
            for (const GAChromosome& c : children) {
                nextPopulation.push_back(c);
            }
        }

        for (int j = 0; j < kMutationNum; ++j) {
            const QVector<GAChromosome> parent = selectRoulette(population, 1);
            if (parent.isEmpty()) {
                break;
            }
            nextPopulation.push_back(mutate(parent[0]));
        }

        if (nextPopulation.isEmpty()) {
            _log(">> VRP allocate failed: next population empty.");
            return false;
        }

        while (nextPopulation.size() > kPopulationSize) {
            nextPopulation.removeLast();
        }
        while (nextPopulation.size() < kPopulationSize) {
            nextPopulation.push_back(population[randInt(population.size())]);
        }

        population = nextPopulation;
        evaluateFitness(population);
    }

    const auto bestIt = std::max_element(population.begin(), population.end(), [](const GAChromosome& a, const GAChromosome& b) {
        return a.fitness < b.fitness;
    });
    if (bestIt == population.end()) {
        _log(">> VRP allocate failed: no best chromosome.");
        return false;
    }

    _assignedRoutes.clear();
    _assignedRoutes.reserve(uavCount);
    QHash<int, int> uavIdToRoute;
    for (int i = 0; i < uavCount; ++i) {
        AssignedRoute route;
        route.uavId = uavStates[i].id;
        route.lastPos = uavStates[i].startPos;
        _assignedRoutes.push_back(route);
        uavIdToRoute.insert(route.uavId, i);
    }

    const GAChromosome& best = *bestIt;
    for (int j = 0; j < targetCount; ++j) {
        const int targetIdx = best.orderGene[j] - 1;
        const int uavId = best.assignGene[j];
        const int routeIdx = uavIdToRoute.value(uavId, -1);
        if (routeIdx < 0 || targetIdx < 0 || targetIdx >= targetsEnu.size()) {
            continue;
        }

        AssignedRoute& route = _assignedRoutes[routeIdx];
        const ProtocolPointENU targetEnu = targetsEnu[targetIdx];
        route.routeDistance += _distance2d(route.lastPos, targetEnu);
        route.lastPos = targetEnu;
        route.targetIndices.push_back(targetIdx);
        route.pointsEnu.push_back(targetEnu);
    }

    _log(QString(">> VRP allocated(GA): targets=%1 uavs=%2 iter=%3")
             .arg(_targets.size())
             .arg(_assignedRoutes.size())
             .arg(kIterationTimes));

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
