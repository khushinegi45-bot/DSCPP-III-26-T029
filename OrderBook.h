#pragma once
#include <functional>
#include <memory>
#include <string>
#include "book/Containers.h"

namespace tx {

// All resting orders of ONE symbol.
// Bids and asks are separate sorted price levels (C sorted linked list);
// each price level holds a FIFO queue (C doubly linked list); an order index (C hash table) finds an order by id.
class OrderBook {
public:
    explicit OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    void add(const OrderPtr& o);                         // rest a limit order in the book
    bool bestPrice(Side side, Price& out) const;         // best resting price on that side
    OrderPtr best(Side side) const;                      // first order in the queue of the best price
    void applyFill(const OrderPtr& resting, int qty);    // fill a resting order; removes it when done
    bool cancel(long id);                                // O(1) lookup through the order index
    OrderPtr find(long id) const;
    BookSnapshot snapshot(int depth) const;
    void forEach(const std::function<void(const OrderPtr&)>& f) const;
    size_t size() const { return index_.size(); }
    const std::string& symbol() const { return symbol_; }

private:
    PriceLevels& levels(Side s) { return s == Side::Buy ? bids_ : asks_; }
    const PriceLevels& levels(Side s) const { return s == Side::Buy ? bids_ : asks_; }
    void removeRef(OrderRef* ref);                       // unlink from queue, delete empty level, free the ref

    std::string symbol_;
    PriceLevels bids_, asks_;
    OrderIndex index_;
};

}  // namespace tx
