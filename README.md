# HFT Backtesting Suite 
This project encompasses a backtesting suite using MBO orderbook data from databento. Able to get market data either using Databento API, or csv files downloaded from them. Currently, I only included the ability to backtest 2 instruments at a time (ES, MNQ), as the data is expensive lol (if someone from databento reads this can i have free credits) 

https://youtu.be/98Tl0HQqu6M to see in action 

## Orderbook 
- comprised of 3 pieces, the order object which represents the individual limit order, 
the limit object which represents all the orders at a certain price level, and the main orderbook
- 3 main operations, add order, modify order, and remove order, when a market order is submitted, the way the data is formatted 
is that it adjusts the size of the limit order that got executed on, cancelling if size was depleted 
- in this design, we have 2 <std::map<uint32_t, Limit*, comparator>> to represent the orderbook with bids being in descending order and offers being in ascending order
- each limit object is comprised of a double linked list of order objects, and we store individual pointers to order objects in a custom open address table ([another repo for statistics](https://github.com/DJ824/open-address-table)) by order_id for o(1) access to each limit order
- pointers to limit objects are also stored in a std::unordered_map<std::pair<int32_t, bool>, MapLimit*, boost::hash<std::pair<int32_t, bool>>> hashing the combination of price,bool to determine which side of the book the limit belongs to. thus, we have o(1) + cost of hash access to each limit object

## Concurrent Backtester 
- implemented the ability to backtest multiple instruments concurrently, using an map of instrument configs, where each instrument config contains a
pointer to a backtester instance, thus we have 1 backtester instance per instrument, the config also contains information needed to backtest such as the market data and instrument id 
- use std::async to launch 1 thread per backtester instance, placing a lock on console for readability 

## General Backtester 
- the backtester contains 2 orderbook instances, one for training a linear model on previous market data, and one for the actual backtesting 
- also have a shared_pointer to the database connection pool (more on this below), and a pointer to the strategy we would like to execute 
- to backtest, we first check if the strategy we selected requires fitting of a linear model, if so it first parses the previous days market data, fits the model, and then conducts the backtest

## Strategy 
- we use inheritance to model the strategy, as each strategy shares some general characteristics, along with some specialities 
- the strategy has a shared_ptr to the database connection pool, passed from the backtester, and is responsible for acquiring connections from the pool 
- the strategy also has a pointer to the orderbook for which the backtesting is occuring on, passed from the backtester 
- I have included 2 strategies, one that uses a combination of orderbook imbalance and vwap, and another which uses a linear regression model on order book imbalance

## Database 
- QuestDB using their influx line protocol to send trade logs via tcp  
- to prevent hogging of tcp lanes, implemented a connection pool, with each strategy instance having their own connection to the database 
- the trade logs are passed from the async_logger class which is running on its own thread using a lock free queue to the database 

## Async Logger 
- the async logger is responsible for sending trade logs to the database, in addition to the console and a csv file 
- we have 1 thread per output destination, and a lock free queue to store the trade logs


Previously, when backtesting single instruments I had included a realtime GUI as well using QT studio. https://www.youtube.com/shorts/Nl-m4L0scYs
Currently on the todo list to support concurrent backtesting for the gui. 

## References 
- https://databento.com/docs/examples/algo-trading/high-frequency for strategy pnl calcuations 
- https://databento.com/docs/examples/algo-trading/high-frequency for orderbook design 
- https://pdfcoffee.com/darryl-shenorderimbalancestrategypdf-pdf-free.html for the linear regression strategy 


