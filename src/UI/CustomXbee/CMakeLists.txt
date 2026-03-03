# 1. 定义一个静态库，名字叫 CustomXbee
qt_add_library(CustomXbee STATIC)

# 2. 定义 QML 模块，绑定到 CustomXbee 这个库上
qt_add_qml_module(CustomXbee
    URI QGroundControl.CustomXbee   # <--- 注意这里改成了 .CustomXbee
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        XbeeWindow.qml
    NO_PLUGIN
)
