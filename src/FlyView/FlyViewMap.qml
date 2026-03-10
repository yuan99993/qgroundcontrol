import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning
import QtQuick.Dialogs
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlyView
import QGroundControl.FlightMap


FlightMap {
    id:                         _root
    allowGCSLocationCenter:     true
    allowVehicleLocationCenter: !_keepVehicleCentered
    planView:                   false
    zoomLevel:                  QGroundControl.flightMapZoom
    center:                     QGroundControl.flightMapPosition

    property Item   pipView
    property Item   pipState:                   _pipState
    property var    rightPanelWidth
    property var    planMasterController
    property bool   pipMode:                    false   // true: map is shown in a small pip mode
    property var    toolInsets                          // Insets for the center viewport area

    property var    _activeVehicle:             QGroundControl.multiVehicleManager.activeVehicle
    property var    _planMasterController:      planMasterController
    property var    _geoFenceController:        planMasterController.geoFenceController
    property var    _rallyPointController:      planMasterController.rallyPointController
    property var    _activeVehicleCoordinate:   _activeVehicle ? _activeVehicle.coordinate : QtPositioning.coordinate()
    property real   _toolButtonTopMargin:       parent.height - mainWindow.height + (ScreenTools.defaultFontPixelHeight / 2)
    property real   _toolsMargin:               ScreenTools.defaultFontPixelWidth * 0.75
    property var    _flyViewSettings:           QGroundControl.settingsManager.flyViewSettings
    property bool   _keepMapCenteredOnVehicle:  _flyViewSettings.keepMapCenteredOnVehicle.rawValue

    property bool   _disableVehicleTracking:    false
    property bool   _keepVehicleCentered:       pipMode ? true : false
    property bool   _saveZoomLevelSetting:      true
    property var    _airZonePolygons:           []      //新增禁飞区用

    //禁飞区相关函数：把“各种格式的点列表”统一转换成地图可画的坐标路径
    function _toCoordPath(rawPath, closeLoop) {
        const out = []
        if (!rawPath) {
            return out
        }

        let total = 0
        if (rawPath.length !== undefined) {
            total = rawPath.length
        } else if (rawPath.count !== undefined) {
            total = rawPath.count
        }

        for (let i = 0; i < total; i++) {
            let c
            if (rawPath.get !== undefined) {
                c = rawPath.get(i)
            } else {
                c = rawPath[i]
            }
            if (c && c.coordinate !== undefined) {
                c = c.coordinate
            }

            let lat
            let lng
            let alt = 0

            if (c) {
                lat = (typeof c.latitude === "function") ? c.latitude() : c.latitude
                lng = (typeof c.longitude === "function") ? c.longitude() : c.longitude
                if (c.altitude !== undefined) {
                    alt = (typeof c.altitude === "function") ? c.altitude() : c.altitude
                }
                if (lat === undefined && c.lat !== undefined) {
                    lat = (typeof c.lat === "function") ? c.lat() : c.lat
                }
                if (lng === undefined && c.lng !== undefined) {
                    lng = (typeof c.lng === "function") ? c.lng() : c.lng
                }
            }

            if (lat !== undefined && lng !== undefined) {
                out.push(QtPositioning.coordinate(lat, lng, alt !== undefined ? alt : 0))
            }
        }
        if (closeLoop && out.length > 2) {
            out.push(out[0])
        }
        return out
    }

    //禁飞区函数：从后端拉最新禁飞区列表
    function _refreshAirZonePolygons() {
        _airZonePolygons = AirZonesBackend.qmlGetZonePolygons()
    }

    function _adjustMapZoomForPipMode() {
        _saveZoomLevelSetting = false
        if (pipMode) {
            if (QGroundControl.flightMapZoom > 3) {
                zoomLevel = QGroundControl.flightMapZoom - 3
            }
        } else {
            zoomLevel = QGroundControl.flightMapZoom
        }
        _saveZoomLevelSetting = true
    }

    onPipModeChanged: _adjustMapZoomForPipMode()

    onVisibleChanged: {
        if (visible) {
            // Synchronize center position with Plan View
            center = QGroundControl.flightMapPosition
        }
    }

    onZoomLevelChanged: {
        if (_saveZoomLevelSetting) {
            QGroundControl.flightMapZoom = _root.zoomLevel
        }
    }
    onCenterChanged: {
        QGroundControl.flightMapPosition = _root.center
    }

    // We track whether the user has panned or not to correctly handle automatic map positioning
    onMapPanStart:  _disableVehicleTracking = true
    onMapPanStop:   panRecenterTimer.restart()

    function pointInRect(point, rect) {
        return point.x > rect.x &&
                point.x < rect.x + rect.width &&
                point.y > rect.y &&
                point.y < rect.y + rect.height;
    }

    property real _animatedLatitudeStart
    property real _animatedLatitudeStop
    property real _animatedLongitudeStart
    property real _animatedLongitudeStop
    property real animatedLatitude
    property real animatedLongitude

    onAnimatedLatitudeChanged: _root.center = QtPositioning.coordinate(animatedLatitude, animatedLongitude)
    onAnimatedLongitudeChanged: _root.center = QtPositioning.coordinate(animatedLatitude, animatedLongitude)

    NumberAnimation on animatedLatitude { id: animateLat; from: _animatedLatitudeStart; to: _animatedLatitudeStop; duration: 1000 }
    NumberAnimation on animatedLongitude { id: animateLong; from: _animatedLongitudeStart; to: _animatedLongitudeStop; duration: 1000 }

    function animatedMapRecenter(fromCoord, toCoord) {
        _animatedLatitudeStart = fromCoord.latitude
        _animatedLongitudeStart = fromCoord.longitude
        _animatedLatitudeStop = toCoord.latitude
        _animatedLongitudeStop = toCoord.longitude
        animateLat.start()
        animateLong.start()
    }

    // returns the rectangle formed by the four center insets
    // used for checking if vehicle is under ui, and as a target for recentering the view
    function _insetCenterRect() {
        return Qt.rect(toolInsets.leftEdgeCenterInset,
                       toolInsets.topEdgeCenterInset,
                       _root.width - toolInsets.leftEdgeCenterInset - toolInsets.rightEdgeCenterInset,
                       _root.height - toolInsets.topEdgeCenterInset - toolInsets.bottomEdgeCenterInset)
    }

    // returns the four rectangles formed by the 8 corner insets
    // used for detecting if the vehicle has flown under the instrument panel, virtual joystick etc
    function _insetCornerRects() {
        var rects = {
        "topleft":      Qt.rect(0,0,
                               toolInsets.leftEdgeTopInset,
                               toolInsets.topEdgeLeftInset),
        "topright":     Qt.rect(_root.width-toolInsets.rightEdgeTopInset,0,
                               toolInsets.rightEdgeTopInset,
                               toolInsets.topEdgeRightInset),
        "bottomleft":   Qt.rect(0,_root.height-toolInsets.bottomEdgeLeftInset,
                               toolInsets.leftEdgeBottomInset,
                               toolInsets.bottomEdgeLeftInset),
        "bottomright":  Qt.rect(_root.width-toolInsets.rightEdgeBottomInset,_root.height-toolInsets.bottomEdgeRightInset,
                               toolInsets.rightEdgeBottomInset,
                               toolInsets.bottomEdgeRightInset)}
        return rects
    }

    function recenterNeeded() {
        var vehiclePoint = _root.fromCoordinate(_activeVehicleCoordinate, false /* clipToViewport */)
        var centerRect = _insetCenterRect()
        //return !pointInRect(vehiclePoint,insetRect)

        // If we are outside the center inset rectangle, recenter
        if(!pointInRect(vehiclePoint, centerRect)){
            return true
        }

        // if we are inside the center inset rectangle
        // then additionally check if we are underneath one of the corner inset rectangles
        var cornerRects = _insetCornerRects()
        if(pointInRect(vehiclePoint, cornerRects["topleft"])){
            return true
        } else if(pointInRect(vehiclePoint, cornerRects["topright"])){
            return true
        } else if(pointInRect(vehiclePoint, cornerRects["bottomleft"])){
            return true
        } else if(pointInRect(vehiclePoint, cornerRects["bottomright"])){
            return true
        }

        // if we are inside the center inset rectangle, and not under any corner elements
        return false
    }

    function updateMapToVehiclePosition() {
        if (animateLat.running || animateLong.running) {
            return
        }
        // We let FlightMap handle first vehicle position
        if (!_keepMapCenteredOnVehicle && firstVehiclePositionReceived && _activeVehicleCoordinate.isValid && !_disableVehicleTracking) {
            if (_keepVehicleCentered) {
                _root.center = _activeVehicleCoordinate
            } else {
                if (firstVehiclePositionReceived && recenterNeeded()) {
                    // Move the map such that the vehicle is centered within the inset area
                    var vehiclePoint = _root.fromCoordinate(_activeVehicleCoordinate, false /* clipToViewport */)
                    var centerInsetRect = _insetCenterRect()
                    var centerInsetPoint = Qt.point(centerInsetRect.x + centerInsetRect.width / 2, centerInsetRect.y + centerInsetRect.height / 2)
                    var centerOffset = Qt.point((_root.width / 2) - centerInsetPoint.x, (_root.height / 2) - centerInsetPoint.y)
                    var vehicleOffsetPoint = Qt.point(vehiclePoint.x + centerOffset.x, vehiclePoint.y + centerOffset.y)
                    var vehicleOffsetCoord = _root.toCoordinate(vehicleOffsetPoint, false /* clipToViewport */)
                    animatedMapRecenter(_root.center, vehicleOffsetCoord)
                }
            }
        }
    }

    on_ActiveVehicleCoordinateChanged: {
        if (_keepMapCenteredOnVehicle && _activeVehicleCoordinate.isValid && !_disableVehicleTracking) {
            _root.center = _activeVehicleCoordinate
        }
    }

    PipState {
        id:         _pipState
        pipView:    _root.pipView
        isDark:     _isFullWindowItemDark
    }

    Timer {
        id:         panRecenterTimer
        interval:   10000
        running:    false
        onTriggered: {
            _disableVehicleTracking = false
            updateMapToVehiclePosition()
        }
    }

    Timer {
        interval:       500
        running:        true
        repeat:         true
        onTriggered:    updateMapToVehiclePosition()
    }

    //新增：页面刚加载完成时，先拉一次后端数据
    Component.onCompleted: _refreshAirZonePolygons()

    //监听后端 configChanged 信号。每次新增顶点、保存禁飞区、清空、修改相关配置后，前端自动重新取 getZonePolygons()
    Connections {
        target: AirZonesBackend
        ignoreUnknownSignals: true
        function onZonesChanged() {
            _refreshAirZonePolygons()
        }
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: _refreshAirZonePolygons()
    }

    QGCMapPalette { id: mapPal; lightColors: isSatelliteMap }

    Connections {
        target:                 _missionController
        ignoreUnknownSignals:   true
        function onNewItemsFromVehicle() {
            var visualItems = _missionController.visualItems
            if (visualItems && visualItems.count !== 1) {
                mapFitFunctions.fitMapViewportToMissionItems()
                firstVehiclePositionReceived = true
            }
        }
    }

    MapFitFunctions {
        id:                         mapFitFunctions // The name for this id cannot be changed without breaking references outside of this code. Beware!
        map:                        _root
        usePlannedHomePosition:     false
        planMasterController:       _planMasterController
    }

    ObstacleDistanceOverlayMap {
        id: obstacleDistance
        showText: !pipMode
    }

    // Add trajectory lines to the map
    MapPolyline {
        id:         trajectoryPolyline
        line.width: 3
        line.color: "red"
        z:          QGroundControl.zOrderTrajectoryLines
        visible:    !pipMode

        Connections {
            target:                 QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) {
                trajectoryPolyline.path = _activeVehicle ? _activeVehicle.trajectoryPoints.list() : []
            }
        }

        Connections {
            target:                             _activeVehicle ? _activeVehicle.trajectoryPoints : null
            function onPointAdded(coordinate) { trajectoryPolyline.addCoordinate(coordinate) }
            function onUpdateLastPoint(coordinate) { trajectoryPolyline.replaceCoordinate(trajectoryPolyline.pathLength() - 1, coordinate) }
            function onPointsCleared() { trajectoryPolyline.path = [] }
        }
    }

    // Add the vehicles to the map
    MapItemView {
        model: QGroundControl.multiVehicleManager.vehicles
        delegate: VehicleMapItem {
            vehicle:        object
            coordinate:     object.coordinate
            map:            _root
            size:           pipMode ? ScreenTools.defaultFontPixelHeight : ScreenTools.defaultFontPixelHeight * 3
            z:              QGroundControl.zOrderVehicles
        }
    }
    // Add distance sensor view
    MapItemView{
        model: QGroundControl.multiVehicleManager.vehicles
        delegate: ProximityRadarMapView {
            vehicle:        object
            coordinate:     object.coordinate
            map:            _root
            z:              QGroundControl.zOrderVehicles
        }
    }
    // Add ADSB vehicles to the map
    MapItemView {
        model: QGroundControl.adsbVehicleManager.adsbVehicles
        delegate: VehicleMapItem {
            coordinate:     object.coordinate
            altitude:       object.altitude
            callsign:       object.callsign
            heading:        object.heading
            alert:          object.alert
            map:            _root
            size:           pipMode ? ScreenTools.defaultFontPixelHeight : ScreenTools.defaultFontPixelHeight * 2.5
            z:              QGroundControl.zOrderVehicles
        }
    }

    // Add the items associated with each vehicles flight plan to the map
    Repeater {
        model: QGroundControl.multiVehicleManager.vehicles

        PlanMapItems {
            map:                    _root
            largeMapView:           !pipMode
            planMasterController:   masterController
            vehicle:                _vehicle

            property var _vehicle: object

            PlanMasterController {
                id: masterController
                Component.onCompleted: startStaticActiveVehicle(object)
            }
        }
    }

    // Allow custom builds to add map items
    CustomMapItems {
        map:            _root
        largeMapView:   !pipMode
    }

    GeoFenceMapVisuals {
        map:                    _root
        myGeoFenceController:   _geoFenceController
        interactive:            false
        planView:               false
        homePosition:           _activeVehicle && _activeVehicle.homePosition.isValid ? _activeVehicle.homePosition :  QtPositioning.coordinate()
    }

    // Rally points on map
    MapItemView {
        model: _rallyPointController.points

        delegate: MapQuickItem {
            id:             itemIndicator
            anchorPoint.x:  sourceItem.anchorPointX
            anchorPoint.y:  sourceItem.anchorPointY
            coordinate:     object.coordinate
            z:              QGroundControl.zOrderMapItems

            sourceItem: MissionItemIndexLabel {
                id:         itemIndexLabel
                label:      qsTr("R", "rally point map item label")
            }
        }
    }

    // Camera trigger points
    MapItemView {
        model: _activeVehicle ? _activeVehicle.cameraTriggerPoints : 0

        delegate: CameraTriggerIndicator {
            coordinate:     object.coordinate
            z:              QGroundControl.zOrderTopMost
        }
    }

    // GoTo Location forward flight circle visuals
    QGCMapCircleVisuals {
        id:                 fwdFlightGotoMapCircle
        mapControl:         parent
        mapCircle:          _fwdFlightGotoMapCircle
        radiusLabelVisible: true
        visible:            gotoLocationItem.visible && _activeVehicle &&
                            _activeVehicle.inFwdFlight &&
                            !_activeVehicle.orbitActive

        property alias coordinate: _fwdFlightGotoMapCircle.center
        property alias radius: _fwdFlightGotoMapCircle.radius
        property alias clockwiseRotation: _fwdFlightGotoMapCircle.clockwiseRotation

        Component.onCompleted: {
            // Only allow editing the radius, not the position
            centerDragHandleVisible = false

            globals.guidedControllerFlyView.fwdFlightGotoMapCircle = this
        }

        Binding {
            target: _fwdFlightGotoMapCircle
            property: "center"
            value: gotoLocationItem.coordinate
        }

        function startLoiterRadiusEdit() {
            _fwdFlightGotoMapCircle.interactive = true
        }

        // Called when loiter edit is confirmed
        function actionConfirmed() {
            _fwdFlightGotoMapCircle.interactive = false
            _fwdFlightGotoMapCircle._commitRadius()
        }

        // Called when loiter edit is cancelled
        function actionCancelled() {
            _fwdFlightGotoMapCircle.interactive = false
            _fwdFlightGotoMapCircle._restoreRadius()
        }

        QGCMapCircle {
            id:                 _fwdFlightGotoMapCircle
            interactive:        false
            showRotation:       true
            clockwiseRotation:  true

            property real _defaultLoiterRadius: _flyViewSettings.forwardFlightGoToLocationLoiterRad.value
            property real _committedRadius;

            onCenterChanged: {
                radius.rawValue = _defaultLoiterRadius
                // Don't commit the radius in case this operation is undone
            }

            Component.onCompleted: {
                radius.rawValue = _defaultLoiterRadius
                _commitRadius()
            }

            function _commitRadius() {
                _committedRadius = radius.rawValue
            }

            function _restoreRadius() {
                radius.rawValue = _committedRadius
            }
        }
    }

    // GoTo Location visuals
    MapQuickItem {
        id:             gotoLocationItem
        visible:        false
        z:              QGroundControl.zOrderMapItems
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY
        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("Go here", "Go to location waypoint")
        }

        property bool inGotoFlightMode: _activeVehicle ? _activeVehicle.flightMode === _activeVehicle.gotoFlightMode : false

        property var _committedCoordinate: null

        onInGotoFlightModeChanged: {
            if (!inGotoFlightMode && gotoLocationItem.visible) {
                // Hide goto indicator when vehicle falls out of guided mode
                hide()
            }
        }

        function show(coord) {
            gotoLocationItem.coordinate = coord
            gotoLocationItem.visible = true
        }

        function hide() {
            gotoLocationItem.visible = false
        }

        function actionConfirmed() {
            _commitCoordinate()

            // Commit the new radius which possibly changed
            fwdFlightGotoMapCircle.actionConfirmed()

            // We leave the indicator visible. The handling for onInGuidedModeChanged will hide it.
        }

        function actionCancelled() {
            _restoreCoordinate()

            // Also restore the loiter radius
            fwdFlightGotoMapCircle.actionCancelled()
        }

        function _commitCoordinate() {
            // Must deep copy
            _committedCoordinate = QtPositioning.coordinate(
                coordinate.latitude,
                coordinate.longitude
            );
        }

        function _restoreCoordinate() {
            if (_committedCoordinate) {
                coordinate = _committedCoordinate
            } else {
                hide()
            }
        }
    }

    // Orbit editing visuals
    QGCMapCircleVisuals {
        id:             orbitMapCircle
        mapControl:     parent
        mapCircle:      _mapCircle
        visible:        false

        property alias center:              _mapCircle.center
        property alias clockwiseRotation:   _mapCircle.clockwiseRotation
        readonly property real defaultRadius: 30

        Connections {
            target: QGroundControl.multiVehicleManager
            function onActiveVehicleChanged(activeVehicle) {
                if (!activeVehicle) {
                    orbitMapCircle.visible = false
                }
            }
        }

        function show(coord) {
            _mapCircle.radius.rawValue = defaultRadius
            orbitMapCircle.center = coord
            orbitMapCircle.visible = true
        }

        function hide() {
            orbitMapCircle.visible = false
        }

        function actionConfirmed() {
            // Live orbit status is handled by telemetry so we hide here and telemetry will show again.
            hide()
        }

        function actionCancelled() {
            hide()
        }

        function radius() {
            return _mapCircle.radius.rawValue
        }

        Component.onCompleted: globals.guidedControllerFlyView.orbitMapCircle = orbitMapCircle

        QGCMapCircle {
            id:                 _mapCircle
            interactive:        true
            radius.rawValue:    30
            showRotation:       true
            clockwiseRotation:  true
        }
    }

    // ROI Location visuals
    MapQuickItem {
        id:             roiLocationItem
        visible:        _activeVehicle && _activeVehicle.isROIEnabled
        z:              QGroundControl.zOrderMapItems
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY

        Connections {
            target: _activeVehicle
            function onRoiCoordChanged(centerCoord) {
                roiLocationItem.show(centerCoord)
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: (position) => {
                position = Qt.point(position.x, position.y)
                var clickCoord = _root.toCoordinate(position, false /* clipToViewPort */)
                // For some strange reason using mainWindow in mapToItem doesn't work, so we use globals.parent instead which also gets us mainWindow
                position = mapToItem(globals.parent, position)
                var dropPanel = roiEditDropPanelComponent.createObject(mainWindow, { clickRect: Qt.rect(position.x, position.y, 0, 0) })
                dropPanel.open()
            }
        }

        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("ROI here", "Make this a Region Of Interest")
        }

        //-- Visibilty controlled by actual state
        function show(coord) {
            roiLocationItem.coordinate = coord
        }
    }

    // Orbit telemetry visuals
    QGCMapCircleVisuals {
        id:             orbitTelemetryCircle
        mapControl:     parent
        mapCircle:      _activeVehicle ? _activeVehicle.orbitMapCircle : null
        visible:        _activeVehicle ? _activeVehicle.orbitActive : false
    }

    MapQuickItem {
        id:             orbitCenterIndicator
        anchorPoint.x:  sourceItem.anchorPointX
        anchorPoint.y:  sourceItem.anchorPointY
        coordinate:     _activeVehicle ? _activeVehicle.orbitMapCircle.center : QtPositioning.coordinate()
        visible:        orbitTelemetryCircle.visible && !gotoLocationItem.visible

        sourceItem: MissionItemIndexLabel {
            checked:    true
            index:      -1
            label:      qsTr("Orbit", "Orbit waypoint")
        }
    }

    QGCPopupDialogFactory {
        id: roiEditPositionDialogFactory

        dialogComponent: roiEditPositionDialogComponent
    }

    Component {
        id: roiEditPositionDialogComponent

        EditPositionDialog {
            title:                  qsTr("Edit ROI Position")
            coordinate:             roiLocationItem.coordinate
            onCoordinateChanged: {
                roiLocationItem.coordinate = coordinate
                _activeVehicle.guidedModeROI(coordinate)
            }
        }
    }

    Component {
        id: roiEditDropPanelComponent

        DropPanel {
            id: roiEditDropPanel

            sourceComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelWidth / 2

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Cancel ROI")
                        onClicked: {
                            _activeVehicle.stopGuidedModeROI()
                            roiEditDropPanel.close()
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Edit Position")
                        onClicked: {
                            roiEditPositionDialogFactory.open({ showSetPositionFromVehicle: false })
                            roiEditDropPanel.close()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: mapClickDropPanelComponent

        DropPanel {
            id: mapClickDropPanel

            property var mapClickCoord

            sourceComponent: Component {
                ColumnLayout {
                    spacing: ScreenTools.defaultFontPixelWidth / 2

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Go to location")
                        visible:            globals.guidedControllerFlyView.showGotoLocation
                        onClicked: {
                            mapClickDropPanel.close()
                            gotoLocationItem.show(mapClickCoord)

                            if ((_activeVehicle.flightMode == _activeVehicle.gotoFlightMode) && !_flyViewSettings.goToLocationRequiresConfirmInGuided.value) {
                                globals.guidedControllerFlyView.executeAction(globals.guidedControllerFlyView.actionGoto, mapClickCoord, gotoLocationItem)
                                gotoLocationItem.actionConfirmed() // Still need to call this to commit the new coordinate and radius
                            } else {
                                globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionGoto, mapClickCoord, gotoLocationItem)
                            }
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Orbit at location")
                        visible:            globals.guidedControllerFlyView.showOrbit
                        onClicked: {
                            mapClickDropPanel.close()
                            orbitMapCircle.show(mapClickCoord)
                            globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionOrbit, mapClickCoord, orbitMapCircle)
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("ROI at location")
                        visible:            globals.guidedControllerFlyView.showROI
                        onClicked: {
                            mapClickDropPanel.close()
                            globals.guidedControllerFlyView.executeAction(globals.guidedControllerFlyView.actionROI, mapClickCoord, 0, false)
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Set home here")
                        visible:            globals.guidedControllerFlyView.showSetHome
                        onClicked: {
                            mapClickDropPanel.close()
                            globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionSetHome, mapClickCoord)
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Set Estimator Origin")
                        visible:            globals.guidedControllerFlyView.showSetEstimatorOrigin
                        onClicked: {
                            mapClickDropPanel.close()
                            globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionSetEstimatorOrigin, mapClickCoord)
                        }
                    }

                    QGCButton {
                        Layout.fillWidth:   true
                        text:               qsTr("Set Heading")
                        visible:            globals.guidedControllerFlyView.showChangeHeading
                        onClicked: {
                            mapClickDropPanel.close()
                            globals.guidedControllerFlyView.confirmAction(globals.guidedControllerFlyView.actionChangeHeading, mapClickCoord)
                        }
                    }

                    ColumnLayout {
                        spacing: 0
                        QGCLabel { text: qsTr("Lat: %1").arg(mapClickCoord.latitude.toFixed(6)) }
                        QGCLabel { text: qsTr("Lon: %1").arg(mapClickCoord.longitude.toFixed(6)) }
                    }
                }
            }
        }
    }

    // onMapClicked: (position) => {
    //     if (!globals.guidedControllerFlyView.guidedUIVisible &&
    //         (globals.guidedControllerFlyView.showGotoLocation || globals.guidedControllerFlyView.showOrbit ||
    //          globals.guidedControllerFlyView.showROI || globals.guidedControllerFlyView.showSetHome ||
    //          globals.guidedControllerFlyView.showSetEstimatorOrigin)) {

    //         position = Qt.point(position.x, position.y)
    //         var clickCoord = _root.toCoordinate(position, false /* clipToViewPort */)
    //         // For some strange reason using mainWindow in mapToItem doesn't work, so we use globals.parent instead which also gets us mainWindow
    //         position = _root.mapToItem(globals.parent, position)
    //         var dropPanel = mapClickDropPanelComponent.createObject(mainWindow, { mapClickCoord: clickCoord, clickRect: Qt.rect(position.x, position.y, 0, 0) })
    //         dropPanel.open()
    //     }
    // }

    MapScale {
        id:                 mapScale
        anchors.margins:    _toolsMargin
        anchors.left:       parent.left
        anchors.top:        parent.top
        mapControl:         _root
        visible:            !ScreenTools.isTinyScreen && QGroundControl.corePlugin.options.flyView.showMapScale && mapControl.pipState.state === mapControl.pipState.windowState

        property real centerInset: visible ? parent.height - y : 0
    }

    // 判断是否在Fly页面
    property bool _isPrimaryFlyMap: mapName === "FlightDisplayView" && !pipMode && globals.isFlyPageActive
    // 左键点击保留为空，或根据需要添加逻辑
    onMapClicked: (position) => {
        // 首先执行 QGC 原生的左键逻辑
        if (!globals.guidedControllerFlyView.guidedUIVisible &&
            (globals.guidedControllerFlyView.showGotoLocation || globals.guidedControllerFlyView.showOrbit ||
             globals.guidedControllerFlyView.showROI || globals.guidedControllerFlyView.showSetHome ||
             globals.guidedControllerFlyView.showSetEstimatorOrigin)) {

            var pos = Qt.point(position.x, position.y)
            var clickCoord = _root.toCoordinate(pos, false)
            pos = _root.mapToItem(globals.parent, pos)
            var dropPanel = mapClickDropPanelComponent.createObject(mainWindow, { mapClickCoord: clickCoord, clickRect: Qt.rect(pos.x, pos.y, 0, 0) })
            dropPanel.open()
        }
    }

    onMapRightClicked: (position) => {
        // 只有在飞行主地图且非小窗口时才弹出 SEAD 菜单
        if (_isPrimaryFlyMap) {
            var coordinate = _root.toCoordinate(position, false)
            seadContextMenu.clickCoordinate = coordinate
            seadContextMenu.popup()
        }
    }

    onMapPressAndHold: (position) => {
        if (_isPrimaryFlyMap) {
            var coordinate = _root.toCoordinate(position, false)
            seadContextMenu.clickCoordinate = coordinate
            seadContextMenu.popup()
        }
    }

    QGCMenu {
        id: seadContextMenu
        property var clickCoordinate

        background: Rectangle {
            implicitWidth:  ScreenTools.defaultFontPixelWidth * 15
            color:          "#4DF5F5F5" // 浅灰 + 约30%不透明度
            border.color:   "#BCBCBC"
            radius:         4
        }

        QGCMenuItem {
            text: "插入 SEAD 任务点"
            onTriggered: SeadBackend.addSeadPoint(seadContextMenu.clickCoordinate)
        }
        QGCMenuItem {
            text: "中途插入任务点"
            onTriggered: SeadBackend.insertTask(seadContextMenu.clickCoordinate)
        }
        QGCMenuItem {
            text: "增加禁飞区顶点"
            onTriggered: AirZonesBackend.qmlAddZoneVertex(seadContextMenu.clickCoordinate)
        }
        QGCMenuSeparator { }
        QGCMenuItem {
            text: "清除所有点位"
            onTriggered: { SeadBackend.clearAllSeadPoints();/*SEAD点清理*/ AirZonesBackend.qmlClearAllZones(); /*禁飞区清理*/}
        }
    }



    // src/FlyView/FlyViewMap.qml

    // --- 1. SEAD 初始任务点 (黄色) ---
    MapItemView {
        model: SeadBackend.missionPoints
        delegate: MapQuickItem {
            coordinate:     object.coordinate
            anchorPoint:    Qt.point(sourceItem.width/2, sourceItem.height/2)
            z:              QGroundControl.zOrderMapItems
            sourceItem: Rectangle {
                width: 14; height: 14; color: "yellow"; radius: 7; border.color: "black"
                QGCLabel { text: "S"; anchors.centerIn: parent; font.pixelSize: 10; color: "black" }
            }
        }
    }

    // --- 2. 中途插入点 (浅蓝色) ---
    MapItemView {
        model: SeadBackend.insertPoints
        delegate: MapQuickItem {
            coordinate:     object.coordinate
            anchorPoint:    Qt.point(sourceItem.width/2, sourceItem.height/2)
            z:              QGroundControl.zOrderMapItems
            sourceItem: Rectangle {
                width: 14; height: 14; color: "lightblue"; radius: 7; border.color: "black"
                QGCLabel { text: "I"; anchors.centerIn: parent; font.pixelSize: 10; color: "black" }
            }
        }
    }

    // --- 3. 禁飞区边界点 (红色) ---
    MapItemView {
        model: AirZonesBackend.zonePoints
        delegate: MapQuickItem {
            coordinate:     object.coordinate
            anchorPoint:    Qt.point(sourceItem.width/2, sourceItem.height/2)
            z:              QGroundControl.zOrderMapItems
            sourceItem: Rectangle {
                width: 12; height: 12; color: "red"; radius: 6; border.color: "white"
            }
        }
    }

    //画禁飞区填充面
    MapItemView {
        model: _airZonePolygons
        delegate: MapPolygon {
            path: _root._toCoordPath(modelData.path, false)
            color: "#22FF4444"
            border.width: 1
            border.color: "#AAFF6666"
            z: QGroundControl.zOrderMapItems
        }
    }

    //画禁飞区外边界线
    MapItemView {
        model: _airZonePolygons
        delegate: MapPolyline {
            path: _root._toCoordPath(modelData.path, true)
            line.width: 1
            line.color: "#FFFF2222"
            z: QGroundControl.zOrderTopMost - 1
        }
    }

    //在每个禁飞区首点放标签（Z1/Z2/...）
    MapItemView {
        model: _airZonePolygons
        delegate: MapQuickItem {
            property var _path: _root._toCoordPath(modelData.path, false)
            visible: _path.length > 0
            coordinate: _path.length > 0 ? _path[0] : QtPositioning.coordinate()
            anchorPoint: Qt.point(sourceItem.width / 2, sourceItem.height / 2)
            z: QGroundControl.zOrderMapItems + 2
            sourceItem: Rectangle {
                width: 28
                height: 16
                radius: 3
                color: "#CC111111"
                border.width: 1
                border.color: "#FFFF6666"
                QGCLabel {
                    anchors.centerIn: parent
                    text: "Z" + modelData.zoneId
                    font.pixelSize: 10
                    color: "#FFFFFF"
                }
            }
        }
    }
}
