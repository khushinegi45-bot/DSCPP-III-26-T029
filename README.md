# TradeX: a simulated stock exchange (2nd-year B.Tech CSE project)

TradeX is a small stock exchange that runs on one computer. Six fixed stocks trade all the time because bots place orders;
you register, log in and trade against them from a web browser. No real money is involved.

* **C** – the data structures: linked list, hash table, sorted linked list (price levels), SHA-256
* **C++17** – the exchange itself: classes, inheritance, matching engine, bots, threads
* **Two small libraries** (single header files in `backend/third_party/`, MIT licence): **cpp-httplib** for the web server and **nlohmann/json** for JSON
* **SQLite** (system library, `-lsqlite3`) – one file, `data/tradex.db`, keeps users and trades so nothing is lost on a restart or crash
* **HTML / CSS / JavaScript** – 11 pages, no framework, served by the C++ program

**Innovation: a SHA-256 hash-chained trade ledger.** Every saved trade stores `hash = SHA256(prev_hash + trade fields)`.
`GET /verify` re-reads the `trades` table, recomputes every hash, and tells you the first trade that was edited or removed.
The Receipts page has a **Verify Ledger Chain** button.

**Stretch: a live benchmark** of matching in RAM against a SQLite write (committed to disk) for every trade (Performance page).

---

## 1. Run it

You need `g++` (C++17), `gcc`, `make` and the SQLite development library (`sudo apt install libsqlite3-dev`; MSYS2: `pacman -S mingw-w64-x86_64-sqlite3`). On Windows use MSYS2 or Git Bash (CMake files are also included).

```bash
cd backend
make            # builds build/tradex
make test       # runs all tests
make run        # starts the server, then open http://localhost:8080
```

Options: `./build/tradex --port 8080 --web ../frontend --data ./data --seed 20000 --no-bench`

* `--seed N` is the number of resting bot orders created at start (default 20,000).
* `--no-bench` skips the start-up benchmark (it takes a few seconds in a background thread).
* Open the site, choose **Create account** (password: 8+ characters with letters and digits), and trade.

## 2. How it fits together

```
 Browser (11 pages)  --REST/JSON-->  cpp-httplib --> Routes ---> TradingEngine  (ONE mutex)
        ^                               |                          |- OrderBook x6  (sorted linked list of price
        '------ /stream (SSE) ----------'                          |                 levels, FIFO list per level)
                                                                   |- MatchingEngine (price-time priority)
        Bot thread (market makers + takers) ---------------------->|- Accounts, reserved funds, order log, candles
                                                                   '- hash-chained ledger --> SQLite: trades table
        UserManager (SQLite users table, salted SHA-256) + SessionManager (tokens, in memory)
```

**Concurrency (kept simple on purpose).** There is one mutex inside `TradingEngine`; every public method takes it, so the HTTP threads and the
bot thread never touch the books at the same time. There is one bot thread (cpp-httplib runs each request on a thread from its pool). Each live-stream connection runs its own loop that asks the
engine "what changed?" every 250 ms (a trade counter and a per-book version number). There is no hub and no condition variable.

**Matching rules.** Price-time priority (best price first, oldest first at the same price), partial fills, market orders sweep the book and the
unfilled rest is cancelled, trades happen at the *resting* order's price, buys reserve cash, sells reserve shares, no short selling.
Every user starts with $100,000 and 100 shares of each stock.

**Storage = one SQLite file, `data/tradex.db`.** Two tables: `users(name, salt, hash, account)` and `trades(id, ts, symbol, price, qty, ..., prev_hash, hash)`.
Every write is committed to disk before the call returns (`synchronous=FULL`, WAL mode), so a crash or power cut cannot lose a saved trade or leave half a row.
SQL values are always bound with `?` marks, never pasted into the text, so there is no SQL injection. On start the server loads the trades, checks the hash chain,
and replays them to rebuild balances. Bot-to-bot trades are not saved.

Two more tables make a restart invisible to users:
* `open_orders` – each resting limit order of a real user is saved when it starts resting and deleted when it is filled or cancelled. Only the order as placed is stored; after a restart the filled part is worked out from the saved trades (`quantity - traded quantity`), so a crash between a trade and its order update can never leave a wrong remaining quantity. Orders go back into the books oldest-first, so time priority is kept.
* `order_log` – the order history page (every order a user sent, including rejected ones, and cancels). The newest 5,000 lines are kept and reloaded at start.
* `sessions` – login tokens are stored as a SHA-256 of the token (a copy of the database does not give out working logins) and expire after 7 days, so users stay logged in across a restart.

**Cancel is all-or-nothing.** Cancelling writes the history line and deletes the resting order in one SQLite transaction (`Database::atomically`), *before* the order leaves the in-memory book. If the database refuses, the order stays exactly as it was and the user gets an error; if the program crashes, the order is either still fully alive or fully cancelled, never brought back.

**Upgrading from the file version.** If `data/users.txt` or `data/trades.csv` exist, the first start copies them into the database and renames them to `*.imported` (`persistence/LegacyImport.cpp`). The hash chain survives the copy.

## 3. Where each course concept is

| Concept | File | Used for |
|---|---|---|
| Doubly linked list (C) | `c_ds/dlist.c` | FIFO queue of orders at one price; O(1) removal when an order is cancelled |
| Hash table (C, int and string keys) | `c_ds/hashtable.c` | order id -> order (O(1) cancel); username -> account; token -> session |
| Sorted linked list (C) | `c_ds/sorted_list.c` | price levels of a book (best price at one end) |
| SHA-256 (C) | `c_ds/sha256.c` | password hashing and the trade ledger chain |
| Classes, inheritance, polymorphism | `domain/Order.h` (`Order` -> `LimitOrder`, `MarketOrder`), `bots/Bots.h` (`Bot` -> `MarketMakerBot`, `RandomTakerBot`), `market/PriceSource.h`, `persistence/Repository.h` (`IRepository` -> `SqliteRepository`) | |
| Encapsulation, RAII | `book/Containers.h` | C++ wrappers that own and free the C structures |
| Threads and a mutex | `engine/TradingEngine.*`, `bots/Bots.cpp`, `api/Routes.cpp` (stream loop) | one mutex, one bot thread, one polling loop per live stream |
| `std::chrono` | `common/Metrics.h` | latency timers; p50 / p95 / p99 / max |

Code size: about 2,100 lines of C++, 480 lines of C, 420 lines of tests, plus the two libraries in `third_party/` (not our code).

**If the examiner asks about complexity:** inserting a price level into the sorted linked list is O(n) in the number of price levels on that side (it walks
past the better prices); finding the best price is O(1). Each side only has a few hundred levels, so this is fast. A balanced tree would make insert
O(log n); it was left out to keep the project small.

## 4. API

JSON everywhere. Protected routes need `Authorization: Bearer <token>` (the stream takes `?token=` because browsers cannot send headers to an EventSource).

| Route | Auth | Purpose |
|---|---|---|
| `POST /register`, `POST /login` | – | returns `token`, `username`, `account_id` |
| `POST /password` | yes | change password (`old_password`, `new_password`) |
| `GET /quotes` | – | last, open, high, low, change, volume of the 6 stocks |
| `GET /book/:sym` | – | top 12 bid and ask levels |
| `GET /candles/:sym` | – | 60 one-minute candles built from real trades |
| `POST /order` | yes | `symbol, side, orderType (LIMIT/MARKET), price, qty` |
| `DELETE /order/:id` | yes | cancel one of your resting orders |
| `GET /orders` | yes | your open orders |
| `GET /orderlog` | yes | every order you sent and its outcome (including rejected ones) |
| `GET /portfolio` | yes | cash, value, holdings, profit and loss, value over time |
| `GET /trades`, `GET /receipts` | yes | your trades / receipts (`order_id`, `receipt_number`, `trade_id`) |
| `GET /verify` | – | checks the SHA-256 chain of the `trades` table |
| `GET /metrics` | – | flat counters: `orders_per_sec, p50/p95/p99/max_match_us, ram_bytes, open_orders, ...` and the RAM-vs-disk benchmark |
| `GET /stream?symbol=` | yes | Server-Sent Events: `snapshot`, `book`, `trade` (with `mine`), `fill` |

## 5. Tests (`make test`)

* `test_ds` – the C linked list, hash table and sorted list are compared with `std::map` / `std::unordered_map` over tens of thousands of random operations (the sorted list also runs its own consistency check), plus SHA-256 test vectors.
* `test_engine` – FIFO and partial fills, market sweeps, price priority, cancel, reserved funds, restart replay, resting orders (partly filled / cancelled) restored after a restart, order history restored, a cancel the database refuses changes nothing, ledger chain, **editing a row of the `trades` table is detected**, the chain continues correctly after a restart, candles, and the live-feed polling API.
* `test_services` – logins surviving a restart, import of old `users.txt`/`trades.csv`, users and passwords (including a SQL-injection-style username), database storage, hash-chain break on a deleted trade, bots really cause trades.

The tests also pass under AddressSanitizer + UBSan and under ThreadSanitizer, and the full server ran clean under ThreadSanitizer with several
clients trading, streaming and verifying at the same time; stopping the server with live streams open takes about 2 seconds. All 11 pages were opened in a real
browser against the running server with no console errors.

## 6. Benchmarks are measured, not estimated

`make bench` (and the Performance page, measured once when the server starts) runs the **same 1,500 orders** against a book of 20,000 resting orders, twice:
once with everything in memory and once with a SQLite write for every trade that involves a user.

Sample from the development machine (yours will differ): about 5 µs per order in RAM and about 180 µs per order with a database write per trade (roughly 36x slower;
on a real laptop disk the gap is usually larger). That is why the engine matches in memory and writes to the database only after a trade happens.
Latency numbers time the matching step inside the engine; they do not include HTTP or the network.

## 7. Limitations (state these in the viva)

* **Bot orders are not saved.** User trades, balances, resting user orders and logins all survive a restart, but the bots' own orders are re-seeded. A restored user order is placed back in the book without matching; it can match later when new orders arrive. Only the newest 5,000 order-history lines are kept.
* **The ledger only covers saved trades** (trades with at least one real user). It proves the rows were not edited; it cannot prove that the *last* rows were not deleted, and anyone who can edit the database could also recompute every hash (there are no signatures).
* **Simulated market only.** Prices come from a random walk plus bot orders; there is no real market data. Candles only cover the time since the server started (earlier minutes are flat).
* **Passwords:** salted, iterated SHA-256 (1,000 rounds). A real product would use bcrypt or argon2. Salts and tokens use `std::mt19937_64`, which is not a cryptographic random generator.
* **One mutex** means one slow request delays the others. That is fine at this size and easy to explain; a real exchange would shard by symbol.
* **Web server:** cpp-httplib is set up without HTTPS and with a pool of 64 threads, and every open live stream keeps one thread busy, so about 60 simultaneous browser tabs would starve other requests. Do not put it on the internet.
* **Minimal database.** Users, trades, resting user orders, order history and login tokens are in SQLite (no indexes beyond the primary keys); everything is loaded into memory at start. One shared connection with a mutex, so database calls take turns. There is **no admin role**: anyone who can read `data/tradex.db` on disk can read it (password hashes are salted, but it is not encrypted). Old `users.txt` / `trades.csv` files are imported once (see section 2).
* **Libraries:** cpp-httplib 0.18.3 and nlohmann/json 3.11.3 are copied unchanged into `backend/third_party/` (MIT licences included, files checked against the official releases). **Check that your course allows them;** the C data structures, engine, bots, ledger and storage are all our own code.

## 8. Folder map

```
backend/c_ds/        C data structures            backend/src/domain/    Order, Trade, Account, Ledger (hash chain)
backend/src/book/    OrderBook + C++ wrappers     backend/src/engine/    TradingEngine, MatchingEngine, Benchmark
backend/src/bots/    bots                         backend/src/market/    price source
backend/src/auth/    users, passwords, sessions   backend/src/persistence/  SQLite + legacy import
backend/src/api/     routes + JSON helpers        backend/tests/         test_ds, test_engine, test_services, bench
backend/third_party/ cpp-httplib, nlohmann/json   frontend/            the 11 pages
data/ (created at run time) tradex.db
```
