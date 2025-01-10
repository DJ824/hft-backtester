#include "interactive_plot.h"
#include <limits>

InteractivePlot::InteractivePlot(QWidget *parent) : QCustomPlot(parent) {
    setInteraction(QCP::iRangeZoom, true);
    setInteraction(QCP::iRangeDrag, true);
    setMouseTracking(true);
}

void InteractivePlot::mousePressEvent(QMouseEvent *event) {
    QCustomPlot::mousePressEvent(event);
    emit userInteracted();
}

void InteractivePlot::mouseMoveEvent(QMouseEvent *event) {
    QCustomPlot::mouseMoveEvent(event);
    if (event->buttons() & Qt::LeftButton) {
        emit userInteracted();
    }

    bool foundDataPoint = false;
    QString tooltipText;

    const double distanceThreshold = 10.0;
    double minDistance = std::numeric_limits<double>::max();

    for (int i = 0; i < graphCount(); ++i) {
        QCPGraph *graph = this->graph(i);
        double x = xAxis->pixelToCoord(event->pos().x());
        double y = yAxis->pixelToCoord(event->pos().y());

        QCPDataContainer<QCPGraphData>::const_iterator it = graph->data()->constBegin();
        QCPDataContainer<QCPGraphData>::const_iterator end = graph->data()->constEnd();

        while (it != end) {
            double key = it->key;
            double value = it->value;
            double pixelKey = xAxis->coordToPixel(key);
            double pixelValue = yAxis->coordToPixel(value);
            double distance = qSqrt(qPow(pixelKey - event->pos().x(), 2) + qPow(pixelValue - event->pos().y(), 2));

            if (distance < distanceThreshold && distance < minDistance) {
                minDistance = distance;

                QString timeStr = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(key)).toString("hh:mm:ss");
                tooltipText = QString("Time: %2\nValue: %3")
                        .arg(timeStr)
                        .arg(value);

                foundDataPoint = true;
            }
            ++it;
        }
    }

    if (foundDataPoint) {
        QToolTip::showText(event->globalPos(), tooltipText, this);
    } else {
        QToolTip::hideText();
    }
}

void InteractivePlot::wheelEvent(QWheelEvent *event) {
    QCustomPlot::wheelEvent(event);
    emit userInteracted();
}

void InteractivePlot::leaveEvent(QEvent *event) {
    Q_UNUSED(event);
    QToolTip::hideText();
}