#pragma once
#include <QObject>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QDebug>
#include <QTime>
#include "strategy.h"
#include "orderbook.h"
#include "database.h"
#include "message.h"
#include "../src/strategies/linear_model_strat.cpp"
#include "../src/strategies/imbalance_strat.cpp"
#include <vector>
#include <memory>
#include <atomic>

class Backtester : public QObject {
Q_OBJECT

public:
    explicit Backtester(DatabaseManager& db_manager,
                        const std::vector<message>& messages,
                        const std::vector<message>& train_messages,
                        QObject* parent = nullptr);
    ~Backtester() override;

    void add_strategy(std::unique_ptr<Strategy> strategy);
    void train_model();

public slots:
    void start_backtest();
    void stop_backtest();
    void restart_backtest();
    void handleStartSignal();
    void on_strategy_changed(const QString& strategy_name, bool requires_fitting);

private slots:
    void run_backtest();

signals:
    void backtest_started();
    void backtest_finished();
    void update_progress(int progress);
    void trade_executed(const QString& timestamp, bool is_buy, int32_t price);
    void update_chart(qint64 timestamp, double bestBid, double bestAsk, int32_t pnl);
    void backtest_error(const QString& error_message);
    void update_orderbook_stats(double vwap, double imbalance, const QString& current_time, int32_t pnl);

private:
    void create_strategy(const QString& strategy_name);
    static constexpr int cooldown_ = 30;
    QTimer *backtest_timer_;
    std::shared_ptr<Orderbook> book_;
    std::unique_ptr<Orderbook> train_book_;
    size_t train_message_index_;

    std::unique_ptr<Strategy> strategy_;
    DatabaseManager& db_manager_;
    bool first_update_;
    size_t current_message_index_;
    const int UPDATE_INTERVAL = 1000;
    std::atomic<bool> running_;
    std::vector<message> messages_;
    std::vector<message> train_messages_;
    const std::string start_time_ = "2024-05-29 09:30:00.000";
    const std::string end_time_ = "2024-05-29 16:00:00.000";
    const std::string train_start_time_ = "2024-05-28 09:30:00.000";
    const std::string train_end_time_ = "2024-05-28 16:00:00.000";
    QThread worker_thread_;
    bool requires_fitting_;

    void update_gui();
    void process_message(const message& msg);
    void reset_state();

    void log(const QString& message) {
        qDebug() << QTime::currentTime().toString("hh:mm:ss.zzz")
                 << "[Backtester]" << message;
    }
};