pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: root
    required property var result
    property var points: result.points || []
    property int hoverIndex: -1
    onPointsChanged: { hoverIndex = -1; plot.requestPaint() }
    implicitHeight: 230
    ColumnLayout {
        anchors.fill: parent; spacing: 5
        Label {
            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11
            text: root.result.error || (root.points.length ? qsTr("像元（列, 行）：%1 · 有效 %2 / %3 · %4").arg(root.result.pixel).arg(root.result.validCount).arg(root.points.length).arg(root.result.unit || "") : qsTr("选择时间维度后，点击像元查看变化曲线。"))
        }
        Canvas {
            id: plot
            objectName: "timeSeriesChart"
            Layout.fillWidth: true; Layout.fillHeight: true
            property real xMin: 0; property real xMax: 1
            property real yMin: 0; property real yMax: 1
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const c=getContext("2d"), w=width, h=height, left=62, right=w-12, top=12, bottom=h-42
                c.clearRect(0,0,w,h)
                c.fillStyle="#fafbfd";c.fillRect(0,0,w,h)
                if (!root.points.length || bottom <= top) return
                let xs=[],ys=[]
                for (let p of root.points) { if (isFinite(p.x)) xs.push(p.x); if (p.value !== null && p.value !== undefined && isFinite(p.value)) ys.push(p.value) }
                if (!xs.length) return
                xMin=xs.reduce((a,b)=>Math.min(a,b));xMax=xs.reduce((a,b)=>Math.max(a,b))
                yMin=ys.length?ys.reduce((a,b)=>Math.min(a,b)):0;yMax=ys.length?ys.reduce((a,b)=>Math.max(a,b)):1
                if(xMax===xMin)xMax=xMin+1
                if(yMax===yMin) { let pad=Math.max(1,Math.abs(yMin)*.05);yMin-=pad;yMax+=pad }
                const px=x=>left+(x-xMin)/(xMax-xMin)*(right-left), py=y=>bottom-(y-yMin)/(yMax-yMin)*(bottom-top)
                c.font="10px sans-serif";c.lineWidth=1
                for(let i=0;i<=4;i++) {let y=top+(bottom-top)*i/4;c.strokeStyle="#dce2ea";c.beginPath();c.moveTo(left,y);c.lineTo(right,y);c.stroke();c.fillStyle="#536174";c.textAlign="right";c.fillText(Number(yMax-(yMax-yMin)*i/4).toPrecision(4),left-6,y+4)}
                c.strokeStyle="#3574dc";c.lineWidth=2;c.beginPath();let connected=false
                for(let p of root.points) {if(p.value===null || p.value===undefined || !isFinite(p.value)) {connected=false;continue}if(connected)c.lineTo(px(p.x),py(p.value));else c.moveTo(px(p.x),py(p.value));connected=true}c.stroke()
                // Isolated observations remain visible; missing observations never connect.
                c.fillStyle="#3574dc";for(let p of root.points)if(p.value!==null && p.value!==undefined && isFinite(p.value)){c.beginPath();c.arc(px(p.x),py(p.value),2,0,Math.PI*2);c.fill()}
                c.fillStyle="#536174";c.textAlign="left";c.fillText(root.points[0].label.slice(0,19),left,bottom+17);c.textAlign="right";c.fillText(root.points[root.points.length-1].label.slice(0,19),right,bottom+31)
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true
                onPositionChanged: function(mouse) {
                    if (!root.points.length) return
                    const x=plot.xMin+(mouse.x-62)/Math.max(1,plot.width-74)*(plot.xMax-plot.xMin)
                    let best=0;for(let i=1;i<root.points.length;i++)if(Math.abs(root.points[i].x-x)<Math.abs(root.points[best].x-x))best=i
                    root.hoverIndex=best
                }
                onExited: root.hoverIndex=-1
            }
        }
        Label {
            Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 10
            text: root.hoverIndex >= 0 ? root.points[root.hoverIndex].label + " : " + (root.points[root.hoverIndex].value === null ? qsTr("缺测") : Number(root.points[root.hoverIndex].value).toPrecision(8)) : (root.result.timeUnit || qsTr("时间索引")) + (root.result.calendar ? " · " + root.result.calendar : "")
        }
    }
}
