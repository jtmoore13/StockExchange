#include "ExchangeOrder.h"
#include "core/Utils/OrderUtils.h"

using namespace TradingTypes;
using namespace Enums;


ExchangeOrder::ExchangeOrder(const std::string& symbol, const std::string& instrument, Qty qty, PriceType priceType,
    std::optional<Price> price, SideType sideType, OrderId orderId) :
        symbol_(symbol),
        instrument_(instrument),
        qty_(qty),
        qtyRemaining_(qty),
        sideType_(sideType),
        priceType_(priceType),
        price_(price.value_or(0)),
        orderId_(orderId),
        readableOrderId_(OrderUtils::GetReadableOrderId(orderId)),
        processedTime_(std::chrono::system_clock::now()),
        sessionId_(ExchangeUtils::GetSessionIdFromOrderId(orderId))
{ }


ExchangeOrder::~ExchangeOrder()
{ }


/*static*/ std::unique_ptr<ExchangeOrder> ExchangeOrder::MakeLimitOrder(const std::string& symbol, const std::string& instrument,
    Qty qty, Price price, SideType sideType, OrderId orderId)
{
    return std::unique_ptr<ExchangeOrder>(
        new ExchangeOrder(
            symbol,
            instrument,
            qty,
            PriceType::LIMIT,
            price,
            sideType,
            orderId
        )
    );
}


/*static*/ std::unique_ptr<ExchangeOrder> ExchangeOrder::MakeMarketOrder(const std::string& symbol, const std::string& instrument,
    Qty qty, SideType sideType, OrderId orderId)
{
    return std::unique_ptr<ExchangeOrder>(
        new ExchangeOrder(
            symbol,
            instrument,
            qty,
            PriceType::MARKET,
            std::nullopt,
            sideType,
            orderId
        )
    );
}


const std::string& ExchangeOrder::GetInstrument() const
{
    return instrument_;
}


Qty ExchangeOrder::GetQty() const
{
    return qty_;
}


Qty ExchangeOrder::GetQtyRemaining() const
{
    return qtyRemaining_;
}


PriceType ExchangeOrder::GetPriceType() const
{
    return priceType_;
}


SideType ExchangeOrder::GetSideType() const
{
    return sideType_;
}


Price ExchangeOrder::GetPrice() const
{
    return price_;
}


OrderId ExchangeOrder::GetOrderId() const
{
    return orderId_;
}


const std::string& ExchangeOrder::GetReadableOrderId() const
{
    return readableOrderId_;
}


const std::string& ExchangeOrder::GetSymbol() const
{
    return symbol_;
}


OrderSnapshot ExchangeOrder::Snapshot() const
{
    OrderSnapshot snapshot;
    snapshot.orderId = orderId_;
    snapshot.qty = qty_;
    snapshot.qtyRemaining = qtyRemaining_;
    snapshot.symbol = symbol_;
    snapshot.status = status_;
    return snapshot;
}