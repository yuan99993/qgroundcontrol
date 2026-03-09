#pragma once

#include <QByteArray>
#include <QGeoCoordinate>
#include <QString>

// === 1. 枚举定义 (严格对应 communication_info.py) ===
namespace ProtocolEnum {
/**
 * @brief 消息ID枚举，定义了不同类型的消息标识
 */
enum Message_ID {
    Default = 0,        // U2G 遥测
    Mode_Change = 1,    // 模式变更
    Arm = 2,           // 解锁/上锁
    Takeoff = 3,       // 起飞
    Time_Synchronize = 4, // 时间同步
    Waypoints = 5,     // 航点
    Comm_u2gFreq = 6,
    Record_Time = 7,
    Mission_Abort = 8,
    Origin_Correction = 9,
    SEAD = 17,
    SEAD_mission = 18,
    Task_Insert = 19,
    Airspace_Clear = 20,
    Airspace_ZoneFrag = 21,
    Airspace_ZoneRemove = 22,
    Airspace_Ack = 23,
    info = 44           // 文本消息
};

enum Mode {
    STABILIZE = 0,
    ALT_HOD = 2,
    AUTO = 3,
    GUIDED = 4,
    LOITER = 5,
    RTL = 6,
    POSITION = 8,
    LAND = 9,
    POSHOLD = 16
};

enum Armed {
    disarmed = 0,
    armed = 1
};

enum WaypointMethod {
    guide_waypoint = 0,
    guide_WPwithHeading = 3
};
}

// === 2. 飞机状态结构体 (对应 pack_u2g_packet_default) ===
struct UavStatus {
    int uav_id;
    int frame_type; // 0:Quad, 1:Fixed
    int mode;
    bool armed;
    int battery;
    double timestamp;

    // 物理量 (已还原精度)
    double x, y, z;       // ENU (m)
    double roll, pitch, yaw; // 角度 (Deg)
    double speed;         // m/s
};

struct ProtocolPointLLA {
    double lat;
    double lng;
    double alt;
};

struct ProtocolPointECEF {
    double x;
    double y;
    double z;
};

struct ProtocolPointENU {
    double e;
    double n;
    double u;
};

struct ProtocolOrigin {
    double lat;
    double lng;
    double alt;
};

class PacketProtocol
{
   public:
    // ================== G2U 打包 (发送) ==================

    /**
     * @brief // 打包通用3字节指令: [msg_id, uav_id, param]，用于模式切换/解锁等简单命令。
     *
     * @param uavID 目标无人机ID，0表示广播给所有无人机
     * @param msgID 消息ID，参考 ProtocolEnum::Message_ID
     * @param param 参数值,即打包的命令数值
     * @return QByteArray
     */
    static QByteArray packCommand(int uavID, int msgID, int param);

        // 打包起飞命令: [Takeoff, uav_id, height]，height 单位为米(整数)。
    static QByteArray packTakeoff(int uavID, int height);

        // 打包单点引导任务: [Waypoints, uav_id, method, radius, E, N, U]，ENU按毫米(int32)发送。
    static QByteArray packGuidedPoint(int uavID, double e, double n, double u, int radius);

        // 将业务payload封装为XBee API Tx帧(0x10)，补齐长度和校验和后可直接串口发送。
    static QByteArray wrapXbeeApiFrame(QByteArray destMac, QByteArray payload);

    // ================== U2G 解包 (接收) ==================

        /// 解析机载遥测包(<BBBBBBdiiiiiii>)并还原物理量单位，成功返回true。
    static bool parseUavStatus(const QByteArray& data, UavStatus& out);

        // 解析文本信息包: [info, uav_id, len, utf8_bytes...]，返回消息字符串。
    static QString parseInfoMessage(const QByteArray& data, int& outUavID);

        // LLA到ECEF基础转换函数，供ENU<->LLA转换内部复用
    static ProtocolPointECEF llaToEcef(double lat, double lng, double alt);
        // 将经纬高(LLA)按给定原点转换为ENU坐标(米)，用于任务点下发前坐标统一。
    static bool coordToEnu(const QGeoCoordinate& coord,
                           double originLat,
                           double originLng,
                           double originAlt,
                           ProtocolPointENU& outEnu);

    static ProtocolOrigin defaultOrigin();
    static void setOrigin(double lat, double lng, double alt);      //由于发包在这里，在SeadBackend中获取到了经纬度，用于后续打包坐标
    static bool hasOrigin();
        // 将ENU坐标(米)按给定原点反算为经纬高(LLA)，用于遥测显示。
    static ProtocolPointLLA enuToLla(double e,
                                     double n,
                                     double u,
                                     double originLat,
                                     double originLng,
                                     double originAlt);
};
