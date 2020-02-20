#ifndef SEGMENT_H
#define SEGMENT_H

#include "mapitem.h"
#include <QBrush>
#include "waypointitem.h"
#include "graphicsline.h"

class Path : public MapItem
{
    Q_OBJECT
public:
    explicit Path(Point2DLatLon start, QColor color, int tile_size, double zoom, qreal z_value, double neutral_scale_zoom = 15, QObject *parent = nullptr);
    void addPoint(Point2DLatLon pos);
    void add_to_scene(QGraphicsScene* scene);
    virtual void setHighlighted(bool h);
    virtual void setZValue(qreal z);
signals:

public slots:

protected:
    virtual void updateGraphics();

private:
    QList<WaypointItem*> waypoints;
    QList<GraphicsLine*> lines;
    int line_widht;
    QColor line_color;
    bool highlighted;
    //QGraphicsLineItem* line;
};

#endif // SEGMENT_H