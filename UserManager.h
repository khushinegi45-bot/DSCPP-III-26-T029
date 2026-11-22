#pragma once
#include <mutex>
#include <string>
#include "hashtable.h"
#include "persistence/Repository.h"

namespace tx {

// Salted, iterated SHA-256 (the C implementation in c_ds/sha256.c).
// For a real product use bcrypt or argon2; say so in the report.
class PasswordHasher {
public:
    static std::string makeSalt();
    static std::string hash(const std::string& salt, const std::string& password);
};

// Accounts are stored in the `users` table of the database (name, salt, hash, account number) and copied into a C hash table
// with string keys at start, so a login does not need a query.
class UserManager {
public:
    explicit UserManager(Database& db);
    ~UserManager();
    UserManager(const UserManager&) = delete;
    UserManager& operator=(const UserManager&) = delete;

    static std::string validateUsername(const std::string& u);   // "" when valid, else the reason
    static std::string validatePassword(const std::string& p);
    static std::string accountLabel(int account);                // 1001 -> "ACC-001001"

    bool registerUser(const std::string& user, const std::string& pass, std::string& err, int& account);
    bool login(const std::string& user, const std::string& pass, std::string& err, int& account);
    bool changePassword(const std::string& user, const std::string& oldPass, const std::string& newPass, std::string& err);

private:
    struct Rec { std::string name, salt, hash; int account; };
    void load();

    Database& db_;
    HashTable* users_;
    int nextAccount_;
    mutable std::mutex m_;
};

// Login tokens. They are kept in the `sessions` table (as a SHA-256 of the token, so a copy of the database does not hand out
// working logins) and last 7 days, so users stay logged in across a restart.
class SessionManager {
public:
    struct Session { int account = 0; std::string username; };
    explicit SessionManager(Database& db);
    ~SessionManager();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    std::string create(int account, const std::string& username);
    bool get(const std::string& token, Session& out) const;
private:
    Database& db_;
    HashTable* tokens_;
    mutable std::mutex m_;
};

}  // namespace tx
