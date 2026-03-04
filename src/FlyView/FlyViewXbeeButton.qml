import QGroundControl
import QGroundControl.Controls
//新增的按钮文件
ToolStripAction {
    text:           qsTr("SEAD")
    iconSource:     "/res/link.svg"
    fullColorIcon:  true

    onTriggered: {
        if (typeof mainWindow !== "undefined" && mainWindow.openXbeeWindow) {
            mainWindow.openXbeeWindow()
        } else {
            console.warn("XBEE button: mainWindow.openXbeeWindow is unavailable")
        }
    }
}
