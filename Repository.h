#pragma once
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>
#include "domain/Order.h"
#include "domain/Trade.h"

struct sqlite3;
struct sqlite3_stmt;

namespace tx {

// One line of a user's order history (every order sent, including rejected ones, and cancels).
struct OrderLogEntry {
    std::time_t ts = 0;
    long id = 0;
    int account = 0;
    std::string symbol, side, type;
    double price = 0;
    int qty = 0;
    std::string status, note;
};

struct SavedOrder { long id; int account; std::string symbol; Side side; long long price; int qty; };

// Storage interface for trades and open orders. The engine only knows this interface, not SQLite.
class IRepository {
public:
    virtual ~IRepository() = default;
    virtual void saveTrade(const Trade& t) = 0;
    virtual std::vector<Trade> loadTrades() = 0;
    // Resting limit orders of real users. Only the order as it was placed is stored; how much of it is already filled is
    // worked out from the saved trades after a restart.
    virtual void saveOpenOrder(const Order&) {}
    virtual void removeOpenOrder(long /*id*/) {}
    virtual std::vector<SavedOrder> loadOpenOrders() { return {}; }
    // Order history. If removeOpenOrderId is set, that resting order is deleted in the SAME all-or-nothing step, so a crash can
    // never leave a cancelled order alive. Returns false when nothing was saved.
    virtual bool saveOrderLog(const OrderLogEntry&, long /*removeOpenOrderId*/ = 0) { return true; }
    virtual std::vector<OrderLogEntry> loadOrderLog(size_t /*limit*/) { return {}; }     // newest first
};

// One SQLite file (data/tradex.db) holds the users and the trades. Every change is committed to disk before the call returns
// (synchronous=FULL), so a crash or power cut cannot lose a saved trade or leave half a row.
class Database {
public:
    using Arg = std::variant<long long, std::string>;
    using RowFn = std::function<void(sqlite3_stmt*)>;
    explicit Database(const std::string& file);          // opens or creates the file and the tables; throws if it cannot
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Runs one SQL statement. The values in `args` are bound to the ? marks (never pasted into the text, so no SQL injection).
    // onRow is called for every result row. Returns false (and prints the error) when the statement fails.
    bool run(const char* sql, const std::vector<Arg>& args = {}, const RowFn& onRow = nullptr);
    // Runs `work` as one transaction: everything in it is saved together or not at all (work returns false to undo).
    bool atomically(const std::function<bool()>& work);
    static std::string text(sqlite3_stmt* row, int column);
    static long long num(sqlite3_stmt* row, int column);
private:
    sqlite3* db_ = nullptr;
    std::recursive_mutex m_;                             // users and the engine share one connection: take turns
};

// Trades table (the hash-chained ledger).
class SqliteRepository : public IRepository {
public:
    explicit SqliteRepository(Database& db) : db_(db) {}
    void saveTrade(const Trade& t) override;
    std::vector<Trade> loadTrades() override;           // oldest first, hashes exactly as saved
    void saveOpenOrder(const Order& o) override;
    void removeOpenOrder(long id) override;
    std::vector<SavedOrder> loadOpenOrders() override;  // oldest first, so time priority is kept
    bool saveOrderLog(const OrderLogEntry& e, long removeOpenOrderId = 0) override;
    std::vector<OrderLogEntry> loadOrderLog(size_t limit) override;
private:
    Database& db_;
    int logWrites_ = 0;
};

// First start after an upgrade: copies data/users.txt and data/trades.csv of the old file-based version into the database
// and renames them to *.imported. Does nothing when those files are not there.
void importLegacyFiles(Database& db, const std::string& dir);

}  // namespace tx
