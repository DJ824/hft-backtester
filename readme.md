# High Frequency Trading Backtesting Suite 🚀

This project implements a backtesting suite designed for MBO data from databento. The system is built with performance and real-time analysis in mind, featuring comprehensive orderbook management, strategy implementation, and visualization tools.

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.2+-green.svg)](https://www.qt.io/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A high-performance backtesting framework designed for high-frequency trading using Market by Order (MBO) data from Databento.

## 🌟 Features

- **High-Performance Orderbook Engine**
- **Real-time Visualization Dashboard**
- **Multi-threaded Architecture**
- **Advanced Trading Strategies**
- **Time Series Database Integration**
- **Asynchronous Logging System**

## 🏗️ Architecture

### Order Book Implementation

The orderbook is the key component of any trading/backtesting system, especially in high frequency scenarios. The system uses the following design:

- An order object that represents individual order messages as received from the CME's matching engine. We use a struct to hold information such as order id, time received, price, quantity, and message type (add/modify/cancel/trade). In addition, we store pointers to order objects in a `std::unordered_map<uint64_t (order_id), order*>`, providing O(1) access to each individual order object.

- A limit object, which is comprised of a doubly linked list of order objects. We use a doubly linked list for quick access to the first and last order in the queue. We use another `std::unordered_map<std::pair<int32_t, bool>, Limit *, boost::hash<std::pair<int32_t, bool>>>` to store pointers to limit objects (bool determines side). The cost/benefit of hashing the pair is still under evaluation.

- The main orderbook is represented by 2 `std::map<uint32_t, limit*>`, one for the bid side and one for the ask side, for which we use templates to define.


### Order Book Operations

The orderbook supports the following core operations:

- Add Order - adds a limit order to the orderbook, time complexity O(log(n)) worst case
- Cancel Order - cancels an order from the orderbook, time complexity O(1) due to the unordered map we can just adjust the next and prev pointers
- Modify Order - modifies an existing order, if the size has been increased and/or price changes, requeues the order, time complexity O(log(n)) worst case
- Trade Order - executes a market order, time complexity O(log(n) + m) worst case

<img width="769" alt="image" src="https://github.com/user-attachments/assets/8c2e4505-78e2-4374-b3e9-344ca1f85fb6">

Market Order Implementation:
When a market trade message arrives, the system processes the complete order lifecycle. For example, when a market trade occurs at timestamp 11 on the Ask side at price 5300.25 for 2 lots, subsequent messages at the same timestamp reflect the orderbook updates. This includes fill messages for affected orders (e.g., order IDs 16 and 17) followed by cancellation messages for those same IDs. Rather than just reading the effect of the market order, the implementation actively processes the trade by accessing the relevant limit object and iterating through orders until the market order size is depleted.

### Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Add Order | O(log n) | Worst case for new price level |
| Cancel Order | O(1) | Using direct order pointer access |
| Modify Order | O(log n) | Worst case for price change |
| Trade Order | O(log n + m) | m = number of orders to match |

## 📊 Trading Strategies

### 1. Order Imbalance Strategy

The first strategy is an imbalance/vwap strategy. We calculate orderbook imbalance based on the first 20/40/80/x levels of each side of the order book, which is accomplished using SIMD instructions. We also calculate VWAP with every trade order message processed, and pass these values to the strategy, which executes a buy order if (imbalance > 0 && mid_price < vwap), and a sell order if (imbalance < 0 && mid_price > vwap).

Key features:
- SIMD-optimized imbalance calculation
- Multi-level order book depth analysis
- VWAP-based signal generation

### 2. Linear Regression Strategy

The second strategy is a linear regression model based strategy, which uses just one X variable for now: orderbook imbalance. This version takes the imbalance of the best bid and offer. We first fit the model by collecting data from the previous day, then construct our X and Y vectors. The data is collected every 1 second, and our model incorporates the last 5 seconds of data in making price predictions, which is stored in the X vector (independent variable).

We then calculate the average price change over the forecast window, which we can adjust to our choosing, storing this in the Y vector (dependent variable). We then run a QR decomposition on these vectors to solve for the coefficients, and store them to use in our strategy.

The strategy uses these coefficients, along with the current orderbook imbalance as defined for this strat, and calculates a predicted price change. If the change is greater/less than our threshold (the minimum price change we want to see), a trade is executed.

Features:
- Historical order imbalance training
- QR decomposition for coefficient estimation
- Configurable forecast windows
- Mean reversion capture through lagged terms

## 🧵 Multi-threading Architecture

### Multithreading Overview

This application takes advantage of the power of multithreading in specific scenarios to improve performance. Here is the breakdown:

Core Threads:
- Main/GUI thread - responsible for GUI event processing and user interactions
- Backtester thread - responsible for processing the market data messages, executing the trading strategy, and emitting signals to the GUI

Database Management Threads:
- Database Thread - responsible for sending trade logs to the database
- Orderbook Thread - responsible for sending order book snapshots to the database (every 100 messages)

Logging Threads:
- Console Thread - responsible for sending trade logs to the console
- CSV Logger Thread - responsible for writing trade logs to a CSV file
- Database Logger Thread - responsible for sending trade logs to the lock-free queue in the database class

### Thread Overview

1. **Main/GUI Thread**
    - GUI event processing
    - User interaction handling
    - Chart updates

2. **Backtester Thread**
    - Market data processing
    - Strategy execution
    - Signal emission

3. **Database Threads**
    - Trade log persistence
    - Orderbook snapshot storage
    - Asynchronous writes

4. **Logger Threads**
    - Console output
    - CSV file writing
    - Database logging

## 🖥️ GUI Components

The GUI is written using the Qt suite for C++. It has the ability to interact with the backtester with stop/start/restart buttons, and supports interactive scrolling and zooming of the price and PNL charts. We also include a trade log along with a window that displays orderbook data, and markers on the price chart for the trades our strategy has executed.

Features include:
- Real-time price charts with Bid/Ask visualization
- P&L performance tracking
- Interactive zoom/scroll functionality
- Trade execution markers
- Order book depth display
- Strategy control panel

### Charts
- Price chart with bid/ask spread
- Volume analysis
- P&L visualization
- Trade markers for strategy execution points

### Controls
- Start/Stop/Restart functionality
- Parameter adjustment interface
- Real-time statistics display


## 📈 Performance Optimization

### Memory Management
- Lock-free queues for inter-thread communication
- Object pools for Order and Limit objects
- Efficient memory allocation patterns

### Computation
- SIMD instructions for orderbook calculations
- O(1) order access via hash maps
- Optimized price level management

### Database
- Asynchronous write operations
- Batch processing for snapshots
- Efficient indexing strategies`


## 📖 References 
- https://web.archive.org/web/20110219163448/http://howtohft.wordpress.com/2011/02/15/how-to-build-a-fast-limit-order-book/ (orderbook design)
- https://databento.com/docs/examples/algo-trading/high-frequency/book-skew-and-trading-rule (strategy pnl calculations)
- https://www.scribd.com/document/360964571/Darryl-Shen-OrderImbalanceStrategy-pdf (linear regression model 
