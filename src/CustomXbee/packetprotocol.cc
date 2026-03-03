#include "packetprotocol.h"

#include <QDataStream>
#include <QtMath>

namespace {
const double kWgs84A = 6378137.0;
const double kWgs84B = 6356752.314245;
const double kWgs84E2 = 0.00669437999014;
const double kOriginLat = 34.122320;
const double kOriginLng = 108.829915;
const double kOriginAlt = 535.0;
}

// ========================== G2U Packing ==========================

QByteArray PacketProtocol::packCommand(int uavID, int msgID, int param)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian); // Python使用小端序

            // 对应 Python: bytearray([MsgID, UAV_ID, Param])
    stream << (quint8)msgID << (quint8)uavID << (quint8)param;
    return packet;
}

QByteArray PacketProtocol::packTakeoff(int uavID, int height)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << (quint8)ProtocolEnum::Takeoff << (quint8)uavID << (quint8)height;
    return packet;
}

QByteArray PacketProtocol::packGuidedPoint(int uavID, double e, double n, double u, int radius)
{
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

            // Header: [MsgID(5)][ID][Type(0)][Radius]
    stream << (quint8)ProtocolEnum::Waypoints
           << (quint8)uavID
           << (quint8)ProtocolEnum::guide_waypoint
           << (quint8)radius;

            // Body: [E][N][U] (int32, *1000)
    stream << (qint32)(e * 1000.0)
           << (qint32)(n * 1000.0)
           << (qint32)(u * 1000.0);

    return packet;
}

QByteArray PacketProtocol::wrapXbeeApiFrame(QByteArray destMac, QByteArray payload)
{
    QByteArray frameData;
    frameData.append((char)0x10); // Frame Type: Transmit Request
    frameData.append((char)0x01); // Frame ID

    if (destMac.size() != 8) destMac.resize(8);
    frameData.append(destMac);    // 64-bit Dest Address

    frameData.append((char)0xFF); // 16-bit Dest Addr (Unknown)
    frameData.append((char)0xFE);
    frameData.append((char)0x00); // Broadcast Radius
    frameData.append((char)0x00); // Options
    frameData.append(payload);    // RF Data

            // Calculate Checksum
    long sum = 0;
    for (char c : frameData) sum += (unsigned char)c;
    char checksum = 0xFF - (sum & 0xFF);

            // Final Packet: [0x7E][LenMSB][LenLSB][FrameData][Checksum]
    QByteArray finalPacket;
    finalPacket.append((char)0x7E);
    quint16 len = frameData.size();
    finalPacket.append((char)((len >> 8) & 0xFF));
    finalPacket.append((char)(len & 0xFF));
    finalPacket.append(frameData);
    finalPacket.append(checksum);

    return finalPacket;
}

// ========================== U2G Unpacking ==========================

bool PacketProtocol::parseUavStatus(const QByteArray& data, UavStatus& out)
{
    // Python struct: <BBBBBBdiiiiiii (42 bytes)
    if (data.size() < 42) return false;

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);

    quint8 msgId, uid, ftype, mode, arm, bat;
    stream >> msgId >> uid >> ftype >> mode >> arm >> bat;

            // 校验消息ID (0=Telemetry, 5=WaypointsACK, 18=SEAD)
    if (msgId != ProtocolEnum::Default && msgId != ProtocolEnum::SEAD_mission && msgId != ProtocolEnum::Waypoints)
        return false;

    out.uav_id = uid;
    out.frame_type = ftype;
    out.mode = mode;
    out.armed = (arm == 1);
    out.battery = bat;

    stream >> out.timestamp; // double (8 bytes)

    qint32 raw_e, raw_n, raw_u;
    qint32 raw_r, raw_p, raw_y;
    qint32 raw_spd;

    stream >> raw_e >> raw_n >> raw_u;
    stream >> raw_r >> raw_p >> raw_y;
    stream >> raw_spd;

            // === 精度还原 (Scaling) ===
            // 对应 Python: int(pos * 1e3)
    out.x = raw_e / 1000.0;
    out.y = raw_n / 1000.0;
    out.z = raw_u / 1000.0;

            // 对应 Python: int(rad * 1e6) -> 还原为度 (Deg)
            // C# Packets.cs: ... * 1e-6 * 180 / PI
    out.roll  = (raw_r / 1000000.0) * (180.0 / M_PI);
    out.pitch = (raw_p / 1000000.0) * (180.0 / M_PI);
    out.yaw   = (raw_y / 1000000.0) * (180.0 / M_PI);

            // 对应 Python: int(speed * 1e3)
    out.speed = raw_spd / 1000.0;

    return true;
}

QString PacketProtocol::parseInfoMessage(const QByteArray& data, int& outUavID)
{
    // Format: [MsgID(44)][UAV_ID][Len][String...]
    if (data.size() < 3) return "";
    if ((quint8)data[0] != ProtocolEnum::info) return "";

    outUavID = (quint8)data[1];
    int len = (quint8)data[2];

    if (data.size() < 3 + len) return "";

    return QString::fromUtf8(data.mid(3, len));
}

ProtocolPointECEF PacketProtocol::llaToEcef(double lat, double lng, double alt)
{
    const double latRad = qDegreesToRadians(lat);
    const double lngRad = qDegreesToRadians(lng);
    const double sinLat = qSin(latRad);
    const double cosLat = qCos(latRad);
    const double sinLng = qSin(lngRad);
    const double cosLng = qCos(lngRad);

    const double N = kWgs84A / qSqrt(1.0 - kWgs84E2 * sinLat * sinLat);

    ProtocolPointECEF ecef;
    ecef.x = (N + alt) * cosLat * cosLng;
    ecef.y = (N + alt) * cosLat * sinLng;
    ecef.z = (N * (1.0 - kWgs84E2) + alt) * sinLat;
    return ecef;
}

bool PacketProtocol::coordToEnu(const QGeoCoordinate& coord,
                                double originLat,
                                double originLng,
                                double originAlt,
                                ProtocolPointENU& outEnu)
{
    if (!coord.isValid()) {
        return false;
    }

    const double alt = qIsNaN(coord.altitude()) ? originAlt : coord.altitude();
    const ProtocolPointECEF ref = llaToEcef(originLat, originLng, originAlt);
    const ProtocolPointECEF p = llaToEcef(coord.latitude(), coord.longitude(), alt);

    const double dx = p.x - ref.x;
    const double dy = p.y - ref.y;
    const double dz = p.z - ref.z;

    const double lat0 = qDegreesToRadians(originLat);
    const double lng0 = qDegreesToRadians(originLng);
    const double sinLat = qSin(lat0);
    const double cosLat = qCos(lat0);
    const double sinLng = qSin(lng0);
    const double cosLng = qCos(lng0);

    outEnu.e = -sinLng * dx + cosLng * dy;
    outEnu.n = -sinLat * cosLng * dx - sinLat * sinLng * dy + cosLat * dz;
    outEnu.u = cosLat * cosLng * dx + cosLat * sinLng * dy + sinLat * dz;
    return true;
}

ProtocolOrigin PacketProtocol::defaultOrigin()
{
    ProtocolOrigin origin;
    origin.lat = kOriginLat;
    origin.lng = kOriginLng;
    origin.alt = kOriginAlt;
    return origin;
}

ProtocolPointLLA PacketProtocol::enuToLla(double e,
                                          double n,
                                          double u,
                                          double originLat,
                                          double originLng,
                                          double originAlt)
{
    const ProtocolPointECEF refEcef = llaToEcef(originLat, originLng, originAlt);

    const double radLat = qDegreesToRadians(originLat);
    const double radLng = qDegreesToRadians(originLng);
    const double sLat = qSin(radLat);
    const double cLat = qCos(radLat);
    const double sLng = qSin(radLng);
    const double cLng = qCos(radLng);

    const double dx = -sLng * e - cLng * sLat * n + cLng * cLat * u;
    const double dy = cLng * e - sLng * sLat * n + sLng * cLat * u;
    const double dz = cLat * n + sLat * u;

    const double x = refEcef.x + dx;
    const double y = refEcef.y + dy;
    const double z = refEcef.z + dz;

    const double p = qSqrt(x * x + y * y);
    const double theta = qAtan2(z * kWgs84A, p * kWgs84B);
    const double sTheta = qSin(theta);
    const double cTheta = qCos(theta);

    const double e2Prime = (kWgs84A * kWgs84A - kWgs84B * kWgs84B) / (kWgs84B * kWgs84B);

    const double lat = qAtan2(
        z + e2Prime * kWgs84B * sTheta * sTheta * sTheta,
        p - kWgs84E2 * kWgs84A * cTheta * cTheta * cTheta
    );
    const double lng = qAtan2(y, x);
    const double N = kWgs84A / qSqrt(1.0 - kWgs84E2 * qSin(lat) * qSin(lat));
    const double alt = (p / qCos(lat)) - N;

    ProtocolPointLLA lla;
    lla.lat = qRadiansToDegrees(lat);
    lla.lng = qRadiansToDegrees(lng);
    lla.alt = alt;
    return lla;
}
