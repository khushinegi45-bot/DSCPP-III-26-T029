#include "auth/UserManager.h"
#include <cctype>
#include <cstdio>
#include <random>
#include <sstream>
#include "common/Types.h"
#include <sqlite3.h>
extern "C" {
#include "sha256.h"
}

namespace tx {

static std::string randomHex(int bytes) {
    static std::mt19937_64 rng{std::random_device{}()};         // note: not a cryptographic generator (fine for a college demo)
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    std::string out;
    char buf[3];
    for (int i = 0; i < bytes; i++) { std::snprintf(buf, sizeof buf, "%02x", (unsigned)(rng() & 0xff)); out += buf; }
    return out;
}

std::string PasswordHasher::makeSalt() { return randomHex(8); }

std::string PasswordHasher::hash(const std::string& salt, const std::string& password) {
    std::string x = salt + password;
    char out[65];
    for (int i = 0; i < 1000; i++) {                             // iterate to slow down guessing
        std::string in = salt + x;
        sha256_hex(reinterpret_cast<const unsigned char*>(in.data()), in.size(), out);
        x = out;
    }
    return x;
}

std::string UserManager::validateUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 20) return "Username: 3-20 letters, numbers or _";
    for (char c : u) if (!std::isalnum((unsigned char)c) && c != '_') return "Username: 3-20 letters, numbers or _";
    return "";
}
std::string UserManager::validatePassword(const std::string& p) {
    bool digit = false, alpha = false;
    for (char c : p) { if (std::isdigit((unsigned char)c)) digit = true; if (std::isalpha((unsigned char)c)) alpha = true; }
    if (p.size() < 8 || p.size() > 100 || !digit || !alpha) return "Password: min 8 characters, letters and digits";
    return "";
}
std::string UserManager::accountLabel(int account) {
    char b[24]; std::snprintf(b, sizeof b, "ACC-%06d", account); return b;
}

UserManager::UserManager(Database& db) : db_(db), users_(ht_create_str(256)), nextAccount_(FIRST_USER_ACCOUNT) { load(); }
UserManager::~UserManager() { ht_destroy(users_, [](void* p) { delete static_cast<Rec*>(p); }); }

void UserManager::load() {
    db_.run("SELECT name,salt,hash,account FROM users", {}, [this](sqlite3_stmt* r) {
        const std::string name = Database::text(r, 0);
        const int account = (int)Database::num(r, 3);
        ht_put_str(users_, name.c_str(), new Rec{name, Database::text(r, 1), Database::text(r, 2), account});
        if (account >= nextAccount_) nextAccount_ = account + 1;
    });
}

bool UserManager::registerUser(const std::string& user, const std::string& pass, std::string& err, int& account) {
    err = validateUsername(user);
    if (err.empty()) err = validatePassword(pass);
    if (!err.empty()) return false;
    std::lock_guard<std::mutex> g(m_);
    if (ht_get_str(users_, user.c_str())) { err = "Username already taken"; return false; }
    Rec* r = new Rec{user, PasswordHasher::makeSalt(), "", nextAccount_++};
    r->hash = PasswordHasher::hash(r->salt, pass);
    if (!db_.run("INSERT INTO users VALUES(?,?,?,?)", {r->name, r->salt, r->hash, (long long)r->account})) {
        delete r; err = "Could not save the account"; return false;      // nothing is kept in memory if the database refused it
    }
    ht_put_str(users_, user.c_str(), r);
    account = r->account;
    return true;
}

bool UserManager::login(const std::string& user, const std::string& pass, std::string& err, int& account) {
    std::lock_guard<std::mutex> g(m_);
    Rec* r = static_cast<Rec*>(ht_get_str(users_, user.c_str()));
    std::string h = PasswordHasher::hash(r ? r->salt : "0000000000000000", pass);   // same work whether or not the user exists
    unsigned diff = r ? 0 : 1;
    if (r) { if (h.size() != r->hash.size()) diff = 1; else for (size_t i = 0; i < h.size(); i++) diff |= (unsigned)(h[i] ^ r->hash[i]); }
    if (diff) { err = "Invalid username or password"; return false; }
    account = r->account;
    return true;
}

bool UserManager::changePassword(const std::string& user, const std::string& oldPass, const std::string& newPass, std::string& err) {
    err = validatePassword(newPass);
    if (!err.empty()) return false;
    std::lock_guard<std::mutex> g(m_);
    Rec* r = static_cast<Rec*>(ht_get_str(users_, user.c_str()));
    if (!r || PasswordHasher::hash(r->salt, oldPass) != r->hash) { err = "Current password is incorrect"; return false; }
    const std::string salt = PasswordHasher::makeSalt(), hash = PasswordHasher::hash(salt, newPass);
    if (!db_.run("UPDATE users SET salt=?, hash=? WHERE name=?", {salt, hash, r->name})) { err = "Could not save the new password"; return false; }
    r->salt = salt; r->hash = hash;
    return true;
}

static std::string tokenKey(const std::string& token) {
    char out[65];
    sha256_hex(reinterpret_cast<const unsigned char*>(token.data()), token.size(), out);
    return out;
}

SessionManager::SessionManager(Database& db) : db_(db), tokens_(ht_create_str(256)) {
    db_.run("DELETE FROM sessions WHERE created < strftime('%s','now') - 7*24*3600");       // expired logins
    db_.run("SELECT token_hash,account,username FROM sessions", {}, [this](sqlite3_stmt* r) {
        ht_put_str(tokens_, Database::text(r, 0).c_str(), new Session{(int)Database::num(r, 1), Database::text(r, 2)});
    });
}
SessionManager::~SessionManager() { ht_destroy(tokens_, [](void* p) { delete static_cast<Session*>(p); }); }

std::string SessionManager::create(int account, const std::string& username) {
    std::string token = randomHex(16);
    std::lock_guard<std::mutex> g(m_);
    const std::string key = tokenKey(token);
    db_.run("INSERT INTO sessions VALUES(?,?,?,strftime('%s','now'))", {key, (long long)account, username});
    Session* s = new Session{account, username};
    if (ht_put_str(tokens_, key.c_str(), s) <= 0) { delete s; }
    return token;
}
bool SessionManager::get(const std::string& token, Session& out) const {
    std::lock_guard<std::mutex> g(m_);
    Session* s = static_cast<Session*>(ht_get_str(tokens_, tokenKey(token).c_str()));
    if (!s) return false;
    out = *s;
    return true;
}

}  // namespace tx
