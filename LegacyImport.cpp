#include <filesystem>
#include <fstream>
#include <sstream>
#include "persistence/Repository.h"

namespace tx {

void importLegacyFiles(Database& db, const std::string& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path usersFile = fs::path(dir) / "users.txt", tradesFile = fs::path(dir) / "trades.csv";

    if (fs::exists(usersFile, ec)) {                                  // format: name salt hash account
        std::ifstream in(usersFile);
        std::string name, salt, hash; int account;
        db.run("BEGIN");
        while (in >> name >> salt >> hash >> account)
            db.run("INSERT OR IGNORE INTO users VALUES(?,?,?,?)", {name, salt, hash, (long long)account});
        db.run("COMMIT");
        fs::rename(usersFile, fs::path(dir) / "users.txt.imported", ec);
        std::printf("  import  : old users.txt copied into the database\n");
    }
    if (fs::exists(tradesFile, ec)) {                                 // format: id,ts,symbol,price,qty,B|S,buyer,seller,buyOrder,sellOrder,prevHash,hash
        SqliteRepository repo(db);
        std::ifstream in(tradesFile);
        std::string line;
        int n = 0;
        db.run("BEGIN");
        while (std::getline(in, line)) {
            if (line.empty() || line.rfind("id,", 0) == 0) continue;
            std::stringstream ss(line);
            std::string f[12];
            int k = 0;
            while (k < 12 && std::getline(ss, f[k], ',')) k++;
            if (k < 12) continue;
            try {
                Trade t;
                t.id = std::stol(f[0]); t.ts = (std::time_t)std::stoll(f[1]); t.symbol = f[2];
                t.price = std::stoll(f[3]); t.qty = std::stoi(f[4]); t.aggressor = f[5] == "B" ? Side::Buy : Side::Sell;
                t.buyerAcc = std::stoi(f[6]); t.sellerAcc = std::stoi(f[7]);
                t.buyOrderId = std::stol(f[8]); t.sellOrderId = std::stol(f[9]); t.prevHash = f[10]; t.hash = f[11];
                if (db.run("INSERT OR IGNORE INTO trades VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
                           {(long long)t.id, (long long)t.ts, t.symbol, (long long)t.price, (long long)t.qty, std::string(f[5]),
                            (long long)t.buyerAcc, (long long)t.sellerAcc, (long long)t.buyOrderId, (long long)t.sellOrderId, t.prevHash, t.hash})) n++;
            } catch (...) { /* damaged line: skipped, the hash chain check will report the gap */ }
        }
        db.run("COMMIT");
        fs::rename(tradesFile, fs::path(dir) / "trades.csv.imported", ec);
        std::printf("  import  : %d trades from the old trades.csv copied into the database\n", n);
    }
}

}  // namespace tx
