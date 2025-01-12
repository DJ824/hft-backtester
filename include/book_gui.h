#pragma once

#include <QWidget>
#include <QTimer>
#include <QScrollBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include "interactive_plot.h"

class BookGui : public QWidget {
Q_OBJECT

public:
    explicit BookGui(QWidget *parent = nullptr);

public slots:
    void add_data_point(qint64 timestamp, double bestBid, double bestAsk, double pnl);
    void update_progress(int progress);
    void log_trade(const QString& timestamp, bool is_buy, int32_t price);
    void on_backtest_finished();
    void on_backtest_error(const QString& error_message);
    void update_orderbook_stats(double vwap, double imbalance, const QString& current_time, int32_t pnl);

signals:
    void start_backtest();
    void stop_backtest();
    void restart_backtest();

private:
    InteractivePlot *m_price_plot;
    InteractivePlot *m_pnl_plot;
    QCPGraph *m_bid_graph;
    QCPGraph *m_ask_graph;
    QCPGraph *m_pnl_graph;
    QCPGraph *m_buy_trades_graph;
    QCPGraph *m_sell_trades_graph;

    QTimer *m_update_timer;
    QScrollBar *m_horizontal_scroll_bar;

    QPushButton *m_start_button;
    QPushButton *m_stop_button;
    QPushButton *m_restart_button;
    QPushButton *m_chart_toggle_button;
    bool m_charts_enabled;
    QProgressBar *m_progress_bar;
    QTableWidget *m_trade_log_table;
    QTableWidget *m_orderbook_stats_table;
    QHBoxLayout *m_button_layout;

    bool m_auto_scroll;
    bool m_user_scrolling;

    static constexpr int UPDATE_INTERVAL = 500;

    void setup_plots();
    void setup_scroll_bar();
    void setup_buttons();
    void setup_trade_log();
    void setup_orderbook_stats();
    void clear_orderbook_stats();
    void update_plots();
    void update_price_plot();
    void update_pnl_plot();
    void update_scroll_bar();
    void style_plot(QCustomPlot *plot);
    void clear_data();

private slots:
    void handle_horizontal_scroll_bar_value_changes(int value);
    void handle_start_button_click();
    void handle_stop_button_click();
    void handle_restart_button_click();
    void on_user_interacted();
    void reset_zoom();

protected:
    void resizeEvent(QResizeEvent *event) override;
};