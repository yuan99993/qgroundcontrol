import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QGroundControl
import QGroundControl.Controls

Window {
    id: root
    width: 240
    height: 280
    minimumWidth: 240
    minimumHeight: 280
    title: "SEAD Control"
    visible: true
    color: "#1A1A1A"

    property color qgcGreen: "#b2d732"
    property color accentBlue: "#00a8ff"
    property bool hasActiveUavs: MissionControl.activeUavCount > 0

    //日志跟踪最低行的功能函数
    function scrollLogToBottom() {
        Qt.callLater(function() {
            const targetY = Math.max(0, logText.contentHeight - logFlick.height)
            logFlick.contentY = targetY
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 7
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            spacing: 5

            ComboBox {
                id: modeSelector
                Layout.fillWidth: true
                Layout.preferredHeight: 25
                model: ["UDP", "Xbee"]
                font.pointSize: 8
            }

            Button {
                id: connBtn
                Layout.preferredWidth: 55
                Layout.preferredHeight: 25
                text: MissionControl.isConnected ? "OFF" : "ON"
                font.pointSize: 8
                background: Rectangle {
                    color: MissionControl.isConnected ? "#ff4b4b" : qgcGreen
                    radius: 2
                }
                onClicked: {
                    if (MissionControl.isConnected) {
                        MissionControl.disconnectAll()
                    } else {
                        if (modeSelector.currentIndex === 0) {
                            MissionControl.connectUdp(txtIp.text, parseInt(txtPort.text))
                        } else {
                            MissionControl.connectXbee(cmbPort.currentText)
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: "UAV Active: " + MissionControl.activeUavCount
            color: hasActiveUavs ? "#7CFC00" : "#aaaaaa"
            font.pointSize: 8
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#252525"
            radius: 2

            StackLayout {
                anchors.fill: parent
                anchors.margins: 2
                currentIndex: modeSelector.currentIndex

                RowLayout {
                    spacing: 4
                    Text { text: "IP:"; color: "#aaa"; font.pointSize: 8 }
                    TextField {
                        id: txtIp
                        text: "192.168.129.128"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        font.pointSize: 8
                        leftPadding: 4
                    }
                    Text { text: "Port:"; color: "#aaa"; font.pointSize: 8 }
                    TextField {
                        id: txtPort
                        text: "14500"
                        Layout.preferredWidth: 45
                        Layout.preferredHeight: 30
                        font.pointSize: 8
                        leftPadding: 4
                    }
                }

                RowLayout {
                    spacing: 4
                    Text { text: "Port:"; color: "#aaa"; font.pointSize: 8 }
                    ComboBox {
                        id: cmbPort
                        model: MissionControl.getSerialPorts()
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        font.pointSize: 8
                    }
                }
            }
        }

        GridLayout {
            columns: 2
            rows: 2
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            columnSpacing: 7
            rowSpacing: 7

            component TaskButton: Button {
                property color borderColor: "white"
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                font.bold: true
                font.pointSize: 8
                background: Rectangle {
                    color: !parent.enabled ? "#1b2430" : (parent.down ? "#111" : "#2C3E50")
                    radius: 2
                    border.color: parent.borderColor
                    border.width: 1
                    opacity: parent.enabled ? 1.0 : 0.45
                }
                contentItem: Text {
                    text: parent.text
                    color: parent.enabled ? "white" : "#8f8f8f"
                    font: parent.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TaskButton {
                text: "OFFBOARD"
                borderColor: accentBlue
                onClicked: MissionControl.sendMode(0, "GUIDED")
            }
            TaskButton {
                text: "SEAD 下发"
                borderColor: qgcGreen
                onClicked: SeadManager.sendSeadMission()
            }
            TaskButton {
                text: "禁飞区下发"
                borderColor: "#e67e22"
                onClicked: SeadManager.uploadZones()
            }
            TaskButton {
                text: "禁飞区绘制"
                borderColor: "#ff4b4b"
                onClicked: SeadManager.saveZonesToFile()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            radius: 2
            border.color: "#333"
            Flickable {
                id: logFlick
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: logText.contentHeight + 6
                boundsBehavior: Flickable.StopAtBounds

                Behavior on contentY {
                    NumberAnimation {
                        duration: 120
                        easing.type: Easing.OutCubic
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                TextEdit {
                    id: logText
                    x: 4
                    y: 2
                    width: logFlick.width - 8
                    text: ">> Ready"
                    color: "#00FF00"
                    font.family: "Consolas"
                    font.pixelSize: 12
                    wrapMode: TextEdit.WrapAnywhere
                    readOnly: true
                    selectByMouse: true
                }
            }
        }
    }

    Connections {
        target: MissionControl
        function onLogMessage(msg) {
            logText.text = logText.text + "\n" + msg
            root.scrollLogToBottom()
        }
    }

    Component.onCompleted: scrollLogToBottom()
}
