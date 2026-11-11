import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
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
    property color blue: "blue"
    property color red: "red"

    Image { // 背景
        anchors.fill: parent
        source: "p7_bg_gd.png"
        fillMode: Image.PreserveAspectCrop
    }

    property real itemScale: 0.6
    Item {
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: columbina.skeletonWidth*columbina.skeletonScale
        height: columbina.skeletonHeight*columbina.skeletonScale

        scale: itemScale

        SpineItem {
            id: columbina

            // 内容居中于 item，item 与包裹层的尺寸一致
            anchors.fill: parent

            skeletonSource: "qrc:/shaonv.json"
            atlasSource: "qrc:/shaonv.atlas"
            animationName: "a2b2a"

            skeletonScale: 1.0

            //clip: true

            ///*
            Rectangle { // 边框
                anchors.fill: parent
                color: transparent
                border.color: red
                border.width: 1 / itemScale
            }
            //*/
        }
        ///*
        Rectangle { // 边框
            anchors.fill: parent
            color: transparent
            border.color: blue
            border.width: 1 / itemScale
        }
        //*/
        DragHandler { } // 拖拽处理
    }
}
