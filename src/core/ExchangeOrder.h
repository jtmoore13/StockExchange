#pragma once
#include "SharedTradingTypes.h"
#include <chrono>
#include <string>
#include <optional>
#include <memory>


class ExchangeOrder
{
public:
    ~ExchangeOrder(); // needs to be public so unique_ptr can call it

    static std::unique_ptr<ExchangeOrder> MakeLimitOrder(const std::string& symbol, const std::string& instrument,
        TradingTypes::Qty qty, TradingTypes::Price price, Enums::SideType sideType, TradingTypes::OrderId orderId);
    static std::unique_ptr<ExchangeOrder> MakeMarketOrder(const std::string& symbol, const std::string& instrument,
        TradingTypes::Qty qty, Enums::SideType sideType, TradingTypes::OrderId orderId);

    const std::string& GetInstrument() const;
    TradingTypes::Qty GetQty() const;
    TradingTypes::Qty GetQtyRemaining() const;
    Enums::PriceType GetPriceType() const;
    Enums::SideType GetSideType() const;
    double GetPrice() const;
    TradingTypes::OrderId GetOrderId() const;
    const std::string& GetReadableOrderId() const;
    const std::string& GetSymbol() const;

    TradingTypes::OrderSnapshot Snapshot() const;

private:
    // easier to just make these friends instead of exposing setters
    friend class Exchange;
    friend class MatchingEngine;

    ExchangeOrder() = delete;
    // make ctor private to force use of static factory fxns
    ExchangeOrder(const std::string& symbol, const std::string& instrument, TradingTypes::Qty qty, Enums::PriceType priceType, std::optional<double> price,
        Enums::SideType sideType, TradingTypes::OrderId orderId);

    std::string symbol_; // created by ExchangeServer
    std::string instrument_;
    TradingTypes::Qty qty_ = 0;
    TradingTypes::Qty qtyRemaining_ = 0;
    Enums::SideType sideType_ = Enums::SideType::BUY;
    Enums::PriceType priceType_ = Enums::PriceType::LIMIT;
    double price_ = 0;

    // populated by Exchange
    TradingTypes::OrderId orderId_;
    std::string readableOrderId_; // just for ease
    
    std::chrono::system_clock::time_point processedTime_;
    Enums::OrderStatus status_ = Enums::OrderStatus::Unfilled;

    TradingTypes::SessionId sessionId_;
};