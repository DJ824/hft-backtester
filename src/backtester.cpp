#include "backtester.h"
#include <QDateTime>
#include <QCoreApplication>
#include <memory>
#include <iomanip>

Backtester::Backtester(DatabaseManager &db_manager,
                       const std::vector<message> &messages, const std::vector<message> &train_messages, QObject *parent)
        : QObject(nullptr), db_manager_(db_manager), messages_(messages), train_messages_(train_messages),
          first_update_(false), current_message_index_(0), running_(false) {

    qDebug() << QTime::currentTime().toString("hh:mm:ss.zzz")
             << "[Backtester] Backtester constructed on thread:" << QThread::currentThreadId();

    backtest_timer_ = new QTimer(this);
    connect(backtest_timer_, &QTimer::timeout, this, &Backtester::run_backtest);
    book_ = std::make_unique<Orderbook>(db_manager);
    train_book_ = std::make_unique<Orderbook>(db_manager);
    strategies_.push_back(std::make_unique<LinearModelStrategy>(db_manager_, book_.get()));
}

Backtester::~Backtester() {
    if (worker_thread_.isRunning()) {
        worker_thread_.quit();
        worker_thread_.wait();
    }
}

void Backtester::train_model() {
    std::cout << "fitting model..." << std::endl;

    train_message_index_ = 0;
    int64_t prev_seconds = 0;
    int ct = 0;

    auto parse_time = [](const std::string& time_str) {
        int hour = (time_str[11] - '0') * 10 + (time_str[12] - '0');
        int minute = (time_str[14] - '0') * 10 + (time_str[15] - '0');
        int second = (time_str[17] - '0') * 10 + (time_str[18] - '0');
        return hour * 3600 + minute * 60 + second;
    };

    while (train_message_index_ < train_messages_.size()) {
        const auto& msg = train_messages_[train_message_index_];
        train_book_->process_msg(msg);

        std::string curr_time = train_book_->get_formatted_time_fast();
       // std::cout << msg.id_ << std::endl;
        int64_t curr_seconds = parse_time(curr_time);

        if (curr_time >= train_start_time_) {
            if (prev_seconds == 0) {
                prev_seconds = curr_seconds;
            }

            if (curr_seconds - prev_seconds >= 1) {
                train_book_->calculate_voi();
                train_book_->add_mid_price();
                prev_seconds = curr_seconds;
            }
        }

        ++train_message_index_;

        if (curr_time >= train_end_time_) {
            break;
        }
    }
    std::cout << train_book_->voi_history_.size() << std::endl;

    book_->voi_history_ = std::move(train_book_->voi_history_);
    std::cout << book_->voi_history_.size() << std::endl;
    book_->mid_prices_ = std::move(train_book_->mid_prices_);

    auto* linear_strategy = dynamic_cast<LinearModelStrategy*>(strategies_[0].get());
    linear_strategy->fit_model();

    std::cout << "model fitted, processed " << train_message_index_ << " messages." << std::endl;
}

void Backtester::restart_backtest() {
    log("Restarting backtest");
    stop_backtest();
    reset_state();
    start_backtest();
}

void Backtester::reset_state() {
    current_message_index_ = 0;
    first_update_ = false;
    book_ = std::make_shared<Orderbook>(db_manager_);
    for (auto& strategy : strategies_) {
        strategy = std::make_unique<ImbalanceStrat>(db_manager_, book_.get());
    }
}


void Backtester::handleStartSignal() {
    start_backtest();
}

void Backtester::start_backtest() {
    qDebug() << QTime::currentTime().toString("hh:mm:ss.zzz")
             << "[Backtester] start_backtest called on thread:" << QThread::currentThreadId();

    if (!running_) {
        running_ = true;
        if (current_message_index_ == 0) {
            qDebug() << "[Backtester] Starting from the beginning...";
        } else if (current_message_index_ >= messages_.size()) {
            qDebug() << "[Backtester] Already completed. Not restarting.";
            return;
        } else {
            qDebug() << "[Backtester] Resuming from message index:" << current_message_index_;
        }

        backtest_timer_->start(0);
        emit backtest_started();
    } else {
        qDebug() << "[Backtester] Backtest is already running";
    }
}

void Backtester::stop_backtest() {
    log("stop_backtest called");
    if (backtest_timer_->isActive()) {
        backtest_timer_->stop();
    }
    running_ = false;
}


void Backtester::update_gui() {
    int32_t bid = book_->get_best_bid_price();
    int32_t ask = book_->get_best_ask_price();
    std::string time_string = book_->get_formatted_time_fast();
    int32_t pnl = strategies_[0]->get_pnl();

    QDateTime date_time = QDateTime::fromString(QString::fromStdString(time_string), "yyyy-MM-dd HH:mm:ss.zzz");
    qint64 timestamp = date_time.toMSecsSinceEpoch();

    emit update_chart(timestamp, bid, ask, pnl);
    emit update_orderbook_stats(book_->vwap_, book_->imbalance_, QString::fromStdString(time_string), pnl);

}

void Backtester::run_backtest() {
    if (current_message_index_ == 0) {
        log("Starting backtest from the beginning...");
    } else {
        log(QString("Resuming backtest from message index: %1").arg(current_message_index_));
    }

    train_model();


    running_ = true;
    first_update_ = false;

    QElapsedTimer total_timer;
    total_timer.start();

    QElapsedTimer update_timer;
    update_timer.start();

    auto parse_time = [](const std::string& time_str) {
        int hour = (time_str[11] - '0') * 10 + (time_str[12] - '0');
        int minute = (time_str[14] - '0') * 10 + (time_str[15] - '0');
        int second = (time_str[17] - '0') * 10 + (time_str[18] - '0');
        return hour * 3600 + minute * 60 + second;
    };

    int64_t prev_seconds = 0;
    int64_t prev_trade = 0;
    std::cout << messages_.size() << std::endl;

    while (running_ && current_message_index_ < messages_.size()) {

        const auto &msg = messages_[current_message_index_];
        book_->process_msg(msg);

        std::string curr_time = book_->get_formatted_time_fast();
        int64_t curr_seconds = parse_time(curr_time);

        if (curr_time >= start_time_ && update_timer.elapsed() >= 15) {
            update_gui();
            update_timer.restart();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 15);
        }

        if (current_message_index_ < 14000 && !first_update_) {
            //book_->calculate_vols();
            //first_update_ = true;
        }

        if (current_message_index_ % 100 == 0 && current_message_index_ > 2000) {
            db_manager_.update_limit_orderbook(book_->bids_, book_->offers_);
            //book_->calculate_vols();

        }

        if (curr_time >= start_time_) {
            if (prev_seconds == 0) {
                prev_seconds = curr_seconds;
            }

            if (curr_seconds - prev_seconds >= 1) {

                for (auto &strategy: strategies_) {
                   // book_->calculate_vols();
                    //book_->calculate_imbalance();

                    /*
                    if (curr_seconds - prev_trade >= 300 && strategy->get_position() != 0) {
                        if (strategy->get_position() > 0) {
                            strategy->execute_trade(false, book_->get_best_ask_price(), 1);
                            strategy->trade_queue_.pop();
                            emit trade_executed(QString::fromStdString(curr_time), false, book_->get_best_ask_price());
                        } else if (strategy->get_position() < 0) {
                            strategy->execute_trade(true, book_->get_best_ask_price(), 1);
                            strategy->trade_queue_.pop();
                            emit trade_executed(QString::fromStdString(curr_time), true, book_->get_best_bid_price());

                        }
                    }
                    */

                    strategy->on_book_update();

                    while (!strategy->trade_queue_.empty()) {
                        auto [is_buy, price] = strategy->trade_queue_.front();
                        strategy->trade_queue_.pop();
                        emit trade_executed(QString::fromStdString(curr_time), is_buy, price);
                        prev_trade = curr_seconds;
                    }
                }

                prev_seconds = curr_seconds;
            }
        }

        if (current_message_index_ % 10000 == 0) {
            int progress = static_cast<int>((static_cast<double>(current_message_index_) / messages_.size()) * 100);
            emit update_progress(progress);
        }

        /*
        if (curr_time >= end_time_) {
            log("Reached end time, stopping backtest");
            break;
        }
        */

        if (current_message_index_ == messages_.size() - 1) {
            break;
        }

        ++current_message_index_;
    }

    log(QString("run_backtest finished. Processed %1 messages in %2 seconds")
                .arg(current_message_index_)
                .arg(total_timer.elapsed() / 1000.0));

    emit update_progress(100);

    for (const auto &strategy: strategies_) {
        log(QString("Strategy PNL: %1, Final position: %2")
                    .arg(strategy->get_pnl())
                    .arg(strategy->get_position()));
    }

    running_ = false;
    emit backtest_finished();
    stop_backtest();
}