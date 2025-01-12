#include <QApplication>
#include <QDebug>
#include <QThread>
#include <QObject>
#include <QTime>
#include "backtester.h"
#include "parser.h"
#include "database.h"
#include "orderbook.h"
#include "book_gui.h"
#include "strategies/linear_model_strat.cpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <QCommandLineParser>
#include <QCommandLineOption>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("HFT Backtester");
    QCoreApplication::setApplicationVersion("1.0");

    try {
        QCommandLineParser cmdParser;
        cmdParser.setApplicationDescription("High Frequency Trading Backtester");
        cmdParser.addHelpOption();
        cmdParser.addVersionOption();

        QCommandLineOption inputFileOption(
                QStringList() << "i" << "input",
                "Input file path",
                "file"
        );
        cmdParser.addOption(inputFileOption);

        QCommandLineOption trainFileOption(
                QStringList() << "t" << "train",
                "Training file path",
                "file"
        );
        cmdParser.addOption(trainFileOption);

        cmdParser.process(app);

        QString inputFile = cmdParser.value(inputFileOption);
        QString trainFile = cmdParser.value(trainFileOption);

        if (inputFile.isEmpty()) inputFile = "es0529.csv";
        if (trainFile.isEmpty()) trainFile = "es0528.csv";

        qDebug() << "[Main] Initializing backtester...";
        DatabaseManager db_manager("127.0.0.1", 9009);

        qDebug() << "[Main] Loading market data files...";
        std::unique_ptr<Parser> dataParser;
        std::unique_ptr<Parser> trainParser;

        try {
            dataParser = std::make_unique<Parser>(inputFile.toStdString());
            trainParser = std::make_unique<Parser>(trainFile.toStdString());

            qDebug() << "[Main] Parsing files...";
            dataParser->parse();
            trainParser->parse();

            qDebug() << "[Main] Loaded" << dataParser->get_message_count() << "messages from market data";
            qDebug() << "[Main] Loaded" << trainParser->get_message_count() << "messages from training data";

        } catch (const ParserException& e) {
            qCritical() << "[Main] Parser error:" << e.what();
            return 1;
        }

        auto backtester = new Backtester(db_manager, dataParser->message_stream_,
                                         trainParser->message_stream_);

        auto gui = new BookGui();
        gui->show();

        QObject::connect(gui, &BookGui::start_backtest,
                         backtester, &Backtester::handleStartSignal,
                         Qt::QueuedConnection);

        QObject::connect(gui, &BookGui::stop_backtest,
                         backtester, &Backtester::stop_backtest,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::backtest_finished,
                         gui, &BookGui::on_backtest_finished,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::update_progress,
                         gui, &BookGui::update_progress,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::trade_executed,
                         gui, &BookGui::log_trade,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::update_chart,
                         gui, &BookGui::add_data_point,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::backtest_error,
                         gui, &BookGui::on_backtest_error,
                         Qt::QueuedConnection);

        QObject::connect(backtester, &Backtester::update_orderbook_stats,
                         gui, &BookGui::update_orderbook_stats,
                         Qt::QueuedConnection);

        QObject::connect(gui, &BookGui::restart_backtest,
                         backtester, &Backtester::restart_backtest,
                         Qt::QueuedConnection);

        QObject::connect(gui, &BookGui::strategy_changed,
                         backtester, &Backtester::on_strategy_changed,
                         Qt::QueuedConnection);

        qDebug() << "[Main] GUI initialized, starting event loop";
        return app.exec();

    } catch (const std::exception& e) {
        qCritical() << "[Main] Fatal error:" << e.what();
        return 1;
    }
}