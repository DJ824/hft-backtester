#pragma once

#include "qcustomplot/qcustomplot.h"

class InteractivePlot : public QCustomPlot {
Q_OBJECT
public:
    explicit InteractivePlot(QWidget *parent = nullptr);

signals:
    void userInteracted();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
};