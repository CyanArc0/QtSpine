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

            x: -leftPadding
            y: -topPadding
            width: skeletonWidth*skeletonScale+leftPadding+rightPadding
            height: skeletonHeight*skeletonScale+topPadding+bottomPadding

            skeletonSource: "qrc:/shaonv.json"
            atlasSource: "qrc:/shaonv.atlas"
            animationName: "a2b2a"

            skeletonScale: 1.0

            leftPadding: 103.52 + 50
            rightPadding: 84.37
            topPadding: 28.247
            bottomPadding: 38.203

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
