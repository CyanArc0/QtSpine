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
    title: qsTr("Columbina")
    property color transparent: "transparent"
    property color dodgerblue: "dodgerblue"

    Image { // 背景
        anchors.fill: parent
        source: "resources/p7_bg_gd.png"
        fillMode: Image.PreserveAspectCrop
    }
    SpinePlayer {
        id: columbina

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        //width: columbina.skeletonWidth
        //height: columbina.skeletonHeight

        skeletonSource: "qrc:/shaonv.json"
        atlasSource: "qrc:/shaonv.atlas"

        skinName: "default"
        animationName: "lizi"
        loop: true

        Component.onCompleted: {
            playAnimation("a")
        }


        scale: (Math.sqrt(5) - 1) / 2

        /*
        Rectangle { // 边框
            //anchors.fill: parent
            x: columbina.contentRect.x
            y: columbina.contentRect.y
            width: columbina.contentRect.width
            height: columbina.contentRect.height
            color: transparent
            border.color: dodgerblue
            border.width: 1 / parent.scale
        }
        */
        DragHandler { // 拖拽处理
            target: columbina
        }
    }
}
