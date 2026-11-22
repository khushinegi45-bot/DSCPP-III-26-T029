#include "book/OrderBook.h"

namespace tx {

void OrderBook::add(const OrderPtr& o) {
    PriceLevel* lv = levels(o->side()).getOrCreate(o->price());
    OrderRef* ref = new OrderRef{o, nullptr, lv};
    try {
        ref->node = lv->queue.push(ref);
    } catch (...) { delete ref; throw; }
    lv->totalQty += o->remaining();
    index_.put(o->id(), ref);
}

bool OrderBook::bestPrice(Side side, Price& out) const {
    return levels(side).best(side == Side::Buy, &out) != nullptr;
}

OrderPtr OrderBook::best(Side side) const {
    Price p;
    PriceLevel* lv = levels(side).best(side == Side::Buy, &p);
    if (!lv) return nullptr;
    return static_cast<OrderRef*>(lv->queue.front())->order;
}

void OrderBook::removeRef(OrderRef* ref) {
    PriceLevel* lv = ref->level;
    Price price = ref->order->price();
    Side side = ref->order->side();
    lv->queue.remove(ref->node);
    if (lv->queue.size() == 0) levels(side).erase(price);      // empty-level cleanup (deletes lv)
    delete ref;
}

void OrderBook::applyFill(const OrderPtr& resting, int qty) {
    OrderRef* ref = index_.get(resting->id());
    if (!ref) return;
    ref->level->totalQty -= qty;
    resting->fill(qty);
    if (resting->remaining() <= 0) {
        index_.remove(resting->id());
        removeRef(ref);
    }
}

bool OrderBook::cancel(long id) {
    OrderRef* ref = index_.remove(id);
    if (!ref) return false;
    ref->level->totalQty -= ref->order->remaining();
    ref->order->cancel();
    removeRef(ref);
    return true;
}

OrderPtr OrderBook::find(long id) const {
    OrderRef* ref = index_.get(id);
    return ref ? ref->order : nullptr;
}

BookSnapshot OrderBook::snapshot(int depth) const {
    BookSnapshot s;
    s.symbol = symbol_;
    for (int side = 0; side < 2; side++) {
        const bool isBid = side == 0;
        const PriceLevels& t = levels(isBid ? Side::Buy : Side::Sell);
        auto& out = isBid ? s.bids : s.asks;
        Price p;
        PriceLevel* lv = t.best(isBid, &p);
        for (int i = 0; lv && i < depth; i++) {
            out.push_back(Level{p, lv->totalQty});
            Price np;
            lv = t.after(p, isBid, &np);
            p = np;
        }
    }
    return s;
}

void OrderBook::forEach(const std::function<void(const OrderPtr&)>& f) const {
    for (int side = 0; side < 2; side++) {
        const bool isBid = side == 0;
        const PriceLevels& t = levels(isBid ? Side::Buy : Side::Sell);
        Price p;
        PriceLevel* lv = t.best(isBid, &p);
        while (lv) {
            for (const DNode* n = lv->queue.head(); n; n = n->next)
                f(static_cast<OrderRef*>(n->data)->order);
            Price np;
            lv = t.after(p, isBid, &np);
            p = np;
        }
    }
}

}  // namespace tx
