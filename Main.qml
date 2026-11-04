import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtSpine

ApplicationWindow {
    id: window
    width: 2048
    height: 1080
    minimumWidth: 640
    minimumHeight: 720
    visible: true
    title: qsTr("QtSpineExample")
    property color transparent: "transparent"
    property color dodgerblue: "dodgerblue"

    Image { // 背景
        anchors.fill: parent
        source: "p7_bg_gd.png"
        fillMode: Image.PreserveAspectCrop
    }

    SpineItem {
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: implicitWidth * 1.5
        height: implicitHeight * 1.5
        scale: 0.6
        skeletonSource: "qrc:/shaonv.json"
        atlasSource: "qrc:/shaonv.atlas"
        animationName: "a2b2a"
        DragHandler { } // 拖拽处理
        /*
        Rectangle { // 边框
            anchors.fill: parent
            color: transparent
            border.color: dodgerblue
            border.width: 1 / parent.scale
        }
        */
    }
}
