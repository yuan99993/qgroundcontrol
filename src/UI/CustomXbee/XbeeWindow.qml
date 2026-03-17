import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QGroundControl
import QGroundControl.Controls

Window {
    id: root
    width: 250
    height: 320
    minimumWidth: 250
    minimumHeight: 320
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

    //把前端文本框的经纬度值传给后端
    function applyOriginFromInputs() {
        const lat = txtOriginLat.text.trim().length > 0 ? Number(txtOriginLat.text) : NaN
        const lng = txtOriginLng.text.trim().length > 0 ? Number(txtOriginLng.text) : NaN
        const alt = txtOriginAlt.text.trim().length > 0 ? Number(txtOriginAlt.text) : 0
        SeadBackend.setOrigin(lat, lng, alt)    //更新经纬度到SEAD代码后端
        AirZonesBackend.qmlSetOrigin(lat, lng, alt)     //更新经纬度到禁飞区代码后端
        VrpBackend.qmlSetOrigin(lat, lng, alt)      //更新经纬度到VRP代码后端
    }

    //把输入框的数据作为VRP的高度值
    function applyVrpAltInput() {
        const alt = Number(txtVrpAlt.text)
        if (!isFinite(alt)) {
            return false
        }

        VrpBackend.draftPointAlt = alt
        return true
    }

    //日志栏显示ID和MAC地址
    function formatXbeeRoutesForLog() {
        const routes = MissionControl.getXbeeRoutes()
        if (!routes || routes.length === 0) {
            return "none"
        }
        const parts = []
        for (let i = 0; i < routes.length; i++) {
            parts.push(routes[i].id + ":" + routes[i].mac)
        }
        return parts.join("  ")
    }

    //让在输入框输入已存在ID时MAC框自动跳出该ID对应的MAC地址
    function queryXbeeRouteById() {
        const id = Number(txtRouteId.text)
        if (!Number.isInteger(id) || id < 1 || id > 255) {
            return
        }
        const knownMac = MissionControl.getXbeeRouteMac(id)
        if (knownMac.length > 0) {
            txtRouteMac.text = knownMac
        }
    }

    //将前端文本框中输入的ID和MAC传给后端
    function applyXbeeRouteFromInputs() {
        const id = Number(txtRouteId.text)
        const mac = txtRouteMac.text.trim()

        if (modeSelector.currentIndex !== 1) {
            MissionControl.appendLogMessage(">> Route save is available in Xbee mode only.")
            return
        }
        if (!Number.isInteger(id) || id < 1 || id > 255) {
            MissionControl.appendLogMessage(">> Route input invalid: ID must be 1-255.")
            return
        }
        if (mac.length === 0) {
            MissionControl.appendLogMessage(">> Route input invalid: MAC is empty.")
            return
        }
        if (!MissionControl.setXbeeRoute(id, mac)) { //设置ID和MAC地址
            return
        }

        queryXbeeRouteById()
        MissionControl.appendLogMessage(">> XBee routes: " + formatXbeeRoutesForLog())  //日志栏显示当前的ID和ID对应MAC地址
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 25
            spacing: 5

            ComboBox {
                id: modeSelector
                Layout.fillWidth: true
                Layout.preferredHeight: 25
                model: ["UDP", "Xbee"]
                font.pointSize: 8
                onCurrentIndexChanged: {
                    if (currentIndex === 1) {
                        MissionControl.appendLogMessage(">> XBee routes: " + root.formatXbeeRoutesForLog())
                    }
                }
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
            Layout.preferredHeight: modeSelector.currentIndex === 1 ? 34 : 0
            visible: modeSelector.currentIndex === 1
            color: "#252525"
            radius: 2

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 4

                Text { text: "Route:"; color: "#aaa"; font.pointSize: 8 }
                TextField {
                    id: txtRouteId
                    text: ""
                    placeholderText: "ID"
                    Layout.preferredWidth: 55
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                    onEditingFinished: root.queryXbeeRouteById()
                }
                TextField {
                    id: txtRouteMac
                    text: ""
                    placeholderText: "MAC Address"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                }
                Button {
                    text: "Save"
                    Layout.preferredWidth: 50
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    onClicked: root.applyXbeeRouteFromInputs()
                }
            }
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
                        Layout.preferredHeight: 25
                        font.pointSize: 8
                        leftPadding: 4
                    }
                    Text { text: "Port:"; color: "#aaa"; font.pointSize: 8 }
                    TextField {
                        id: txtPort
                        text: "14500"
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 25
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
                        Layout.preferredHeight: 25
                        font.pointSize: 8
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#252525"
            radius: 2

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 4

                Text { text: "Origin:"; color: "#aaa"; font.pointSize: 8 }
                TextField {
                    id: txtOriginLat
                    text: isFinite(SeadBackend.originLat) ? SeadBackend.originLat.toFixed(6) : ""
                    placeholderText: "lat"
                    Layout.preferredWidth: 55
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                    //onEditingFinished: root.applyOriginFromInputs()   //这个会触发不选择文本框就触发一次设置
                }
                TextField {
                    id: txtOriginLng
                    text: isFinite(SeadBackend.originLng) ? SeadBackend.originLng.toFixed(6) : ""
                    placeholderText: "lng"
                    Layout.preferredWidth: 55
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                    //onEditingFinished: root.applyOriginFromInputs()
                }
                TextField {
                    id: txtOriginAlt
                    text: isFinite(SeadBackend.originAlt) ? SeadBackend.originAlt.toFixed(1) : ""
                    placeholderText: "alt"
                    Layout.preferredWidth: 35
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                    //onEditingFinished: root.applyOriginFromInputs()
                }
                Button {
                    text: "Set"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    onClicked: root.applyOriginFromInputs()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: "#252525"
            radius: 2

            RowLayout {
                anchors.fill: parent
                anchors.margins: 2
                spacing: 4

                Text { text: "VRP Alt(rel):"; color: "#aaa"; font.pointSize: 8 }
                TextField {
                    id: txtVrpAlt
                    text: Number(VrpBackend.draftPointAlt).toFixed(1)
                    placeholderText: "rel m"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    font.pointSize: 8
                    leftPadding: 4
                    onTextChanged: root.applyVrpAltInput()
                    onEditingFinished: {
                        if (!root.applyVrpAltInput()) {
                            txtVrpAlt.text = Number(VrpBackend.draftPointAlt).toFixed(1)
                        }
                    }
                }
                }
        }
        GridLayout {
            columns: 2
            rows: 3
            Layout.fillWidth: true
            Layout.preferredHeight: 115
            columnSpacing: 5
            rowSpacing: 5

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
                onClicked: SeadBackend.sendSeadMission()
            }
            TaskButton {
                text: "禁飞区下发"
                borderColor: "#e67e22"
                onClicked: AirZonesBackend.qmlUploadZones(SeadBackend.targetUavId)  //
            }
            TaskButton {
                text: "禁飞区保存"
                borderColor: "#ff4b4b"
                onClicked: AirZonesBackend.qmlFinalizeCurrentZone()
            }
            TaskButton {
                text: "VRP下发"
                borderColor: "#9b59b6"
                onClicked: VrpBackend.uploadVrpResult(SeadBackend.waypointRadius)
            }
            TaskButton {
                text: "VRP分配"
                borderColor: "#16a085"
                onClicked: VrpBackend.runVrpAllocation()
            }
        }

        //日志栏部分放在Fly界面去了，这里不做显示
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: false        //让日志栏在这个窗口不再显示
            Layout.preferredHeight: 0       //不再显示
            visible: false                  //不再显示
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
