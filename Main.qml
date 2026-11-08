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

    Image { // 背景
        anchors.fill: parent
        source: "p7_bg_gd.png"
        fillMode: Image.PreserveAspectCrop
    }

    Item {
        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        width: columbula.implicitWidth*columbula.skeletonScale
        height: columbula.implicitHeight*columbula.skeletonScale

        SpineItem {
            id: columbula
            x: -leftPadding
            y: -topPadding
            width: implicitWidth*skeletonScale+leftPadding+rightPadding
            height: implicitHeight*skeletonScale+topPadding+bottomPadding/2
            skeletonScale: 0.6
            leftPadding: 103.52 + 50
            rightPadding: 84.37
            topPadding: 28.247
            bottomPadding: 38.203
            skeletonSource: "qrc:/shaonv.json"
            atlasSource: "qrc:/shaonv.atlas"
            animationName: "a2b2a"

            Rectangle { // 边框
                anchors.fill: parent
                color: transparent
                border.color: "red"
                border.width: 2 / parent.scale
            }
            ///*

            //*/
        }

        Rectangle { // 边框
            anchors.fill: parent
            color: transparent
            border.color: dodgerblue
            border.width: 1 / parent.scale
        }
        DragHandler { } // 拖拽处理


    }




}
