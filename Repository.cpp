#include "persistence/Repository.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <sqlite3.h>

namespace tx {

Database::Database(const std::string& file) {
    if (sqlite3_open(file.c_str(), &db_) != SQLITE_OK) {
        std::string why = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        throw std::runtime_error("Cannot open database " + file + ": " + why);
    }
    run("PRAGMA journal_mode=WAL");
    run("PRAGMA synchronous=FULL");
    run("CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY, salt TEXT NOT NULL, hash TEXT NOT NULL, account INTEGER NOT NULL)");
    run("CREATE TABLE IF NOT EXISTS trades(id INTEGER PRIMARY KEY, ts INTEGER, symbol TEXT, price INTEGER, qty INTEGER, aggressor TEXT,"
        " buyer_acc INTEGER, seller_acc INTEGER, buy_order INTEGER, sell_order INTEGER, prev_hash TEXT, hash TEXT)");
    run("CREATE TABLE IF NOT EXISTS open_orders(id INTEGER PRIMARY KEY, account INTEGER, symbol TEXT, side TEXT, price INTEGER, qty INTEGER)");
    run("CREATE TABLE IF NOT EXISTS order_log(seq INTEGER PRIMARY KEY AUTOINCREMENT, ts INTEGER, id INTEGER, account INTEGER, symbol TEXT, side TEXT,"
        " type TEXT, price_cents INTEGER, qty INTEGER, status TEXT, note TEXT)");
    run("CREATE TABLE IF NOT EXISTS sessions(token_hash TEXT PRIMARY KEY, account INTEGER, username TEXT, created INTEGER)");
}

Database::~Database() { sqlite3_close(db_); }

bool Database::run(const char* sql, const std::vector<Arg>& args, const RowFn& onRow) {
    std::lock_guard<std::recursive_mutex> g(m_);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) { std::fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db_)); return false; }
    for (size_t i = 0; i < args.size(); i++) {
        if (const long long* n = std::get_if<long long>(&args[i])) sqlite3_bind_int64(st, (int)i + 1, *n);
        else sqlite3_bind_text(st, (int)i + 1, std::get<std::string>(args[i]).c_str(), -1, SQLITE_TRANSIENT);
    }
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW)
        if (onRow) onRow(st);
    if (rc != SQLITE_DONE) std::fprintf(stderr, "SQL error: %s\n", sqlite3_errmsg(db_));
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

bool Database::atomically(const std::function<bool()>& work) {
    std::lock_guard<std::recursive_mutex> g(m_);
    if (!run("BEGIN")) return false;
    if (work()) return run("COMMIT");
    run("ROLLBACK");
    return false;
}

std::string Database::text(sqlite3_stmt* row, int column) {
    const unsigned char* p = sqlite3_column_text(row, column);
    return p ? reinterpret_cast<const char*>(p) : "";
}
long long Database::num(sqlite3_stmt* row, int column) { return sqlite3_column_int64(row, column); }

void SqliteRepository::saveTrade(const Trade& t) {
    db_.run("INSERT INTO trades VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            {(long long)t.id, (long long)t.ts, t.symbol, (long long)t.price, (long long)t.qty, std::string(t.aggressor == Side::Buy ? "B" : "S"),
             (long long)t.buyerAcc, (long long)t.sellerAcc, (long long)t.buyOrderId, (long long)t.sellOrderId, t.prevHash, t.hash});
}

std::vector<Trade> SqliteRepository::loadTrades() {
    std::vector<Trade> out;
    db_.run("SELECT id,ts,symbol,price,qty,aggressor,buyer_acc,seller_acc,buy_order,sell_order,prev_hash,hash FROM trades ORDER BY id", {},
            [&](sqlite3_stmt* r) {
                Trade t;
                t.id = (long)Database::num(r, 0); t.ts = (std::time_t)Database::num(r, 1); t.symbol = Database::text(r, 2);
                t.price = Database::num(r, 3); t.qty = (int)Database::num(r, 4);
                t.aggressor = Database::text(r, 5) == "B" ? Side::Buy : Side::Sell;
                t.buyerAcc = (int)Database::num(r, 6); t.sellerAcc = (int)Database::num(r, 7);
                t.buyOrderId = (long)Database::num(r, 8); t.sellOrderId = (long)Database::num(r, 9);
                t.prevHash = Database::text(r, 10); t.hash = Database::text(r, 11);
                out.push_back(t);
            });
    return out;
}

void SqliteRepository::saveOpenOrder(const Order& o) {
    db_.run("INSERT OR REPLACE INTO open_orders VALUES(?,?,?,?,?,?)",
            {(long long)o.id(), (long long)o.account(), o.symbol(), std::string(o.side() == Side::Buy ? "B" : "S"), (long long)o.price(), (long long)o.quantity()});
}

void SqliteRepository::removeOpenOrder(long id) { db_.run("DELETE FROM open_orders WHERE id=?", {(long long)id}); }

std::vector<SavedOrder> SqliteRepository::loadOpenOrders() {
    std::vector<SavedOrder> out;
    db_.run("SELECT id,account,symbol,side,price,qty FROM open_orders ORDER BY id", {}, [&](sqlite3_stmt* r) {
        out.push_back({(long)Database::num(r, 0), (int)Database::num(r, 1), Database::text(r, 2),
                       Database::text(r, 3) == "B" ? Side::Buy : Side::Sell, Database::num(r, 4), (int)Database::num(r, 5)});
    });
    return out;
}

bool SqliteRepository::saveOrderLog(const OrderLogEntry& e, long removeOpenOrderId) {
    const bool ok = db_.atomically([&] {
        bool good = db_.run("INSERT INTO order_log(ts,id,account,symbol,side,type,price_cents,qty,status,note) VALUES(?,?,?,?,?,?,?,?,?,?)",
                            {(long long)e.ts, (long long)e.id, (long long)e.account, e.symbol, e.side, e.type, (long long)std::llround(e.price * 100),
                             (long long)e.qty, e.status, e.note});
        if (good && removeOpenOrderId) good = db_.run("DELETE FROM open_orders WHERE id=?", {(long long)removeOpenOrderId});
        return good;
    });
    if (ok && ++logWrites_ % 1000 == 0)                                  // keep only the newest 5,000 lines
        db_.run("DELETE FROM order_log WHERE seq <= (SELECT MAX(seq) FROM order_log) - 5000");
    return ok;
}

std::vector<OrderLogEntry> SqliteRepository::loadOrderLog(size_t limit) {
    std::vector<OrderLogEntry> out;
    db_.run("SELECT ts,id,account,symbol,side,type,price_cents,qty,status,note FROM order_log ORDER BY seq DESC LIMIT ?", {(long long)limit},
            [&](sqlite3_stmt* r) {
                OrderLogEntry e;
                e.ts = (std::time_t)Database::num(r, 0); e.id = (long)Database::num(r, 1); e.account = (int)Database::num(r, 2);
                e.symbol = Database::text(r, 3); e.side = Database::text(r, 4); e.type = Database::text(r, 5);
                e.price = Database::num(r, 6) / 100.0; e.qty = (int)Database::num(r, 7); e.status = Database::text(r, 8); e.note = Database::text(r, 9);
                out.push_back(e);
            });
    return out;
}

}  // namespace tx
