#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <memory>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <array>

struct Order {
    uint64_t order_id;     // Unique order identifier
    bool is_buy;           // true = buy, false = sell
    double price;          // Limit price
    uint64_t quantity;     // Remaining quantity
    uint64_t timestamp_ns; // Order entry timestamp in nanoseconds
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};

// Memory Pool for cache-friendly allocation
template<typename T, size_t PoolSize = 50000>
class MemoryPool {
private:
    alignas(64) std::array<typename std::aligned_storage<sizeof(T), alignof(T)>::type, PoolSize> pool;
    std::array<bool, PoolSize> used;
    size_t next_free;
    
public:
    MemoryPool() : next_free(0) {
        used.fill(false);
    }
    
    template<typename... Args>
    T* allocate(Args&&... args) {
        for (size_t i = next_free; i < PoolSize; ++i) {
            if (!used[i]) {
                used[i] = true;
                next_free = i + 1;
                return new (reinterpret_cast<T*>(&pool[i])) T(std::forward<Args>(args)...);
            }
        }
        
        for (size_t i = 0; i < next_free; ++i) {
            if (!used[i]) {
                used[i] = true;
                next_free = i + 1;
                return new (reinterpret_cast<T*>(&pool[i])) T(std::forward<Args>(args)...);
            }
        }
        
        return nullptr;
    }
    
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        size_t index = reinterpret_cast<char*>(ptr) - reinterpret_cast<char*>(&pool[0]);
        index /= sizeof(typename std::aligned_storage<sizeof(T), alignof(T)>::type);
        
        if (index < PoolSize) {
            ptr->~T();
            used[index] = false;
            if (index < next_free) {
                next_free = index;
            }
        }
    }
};

// Internal order node for FIFO ordering
struct OrderNode {
    Order order;
    OrderNode* next;
    OrderNode* prev;
    
    OrderNode(const Order& o) : order(o), next(nullptr), prev(nullptr) {}
};

// Price level with FIFO order queue
class PriceLevelNode {
public:
    double price;
    uint64_t total_quantity;
    OrderNode* first_order;
    OrderNode* last_order;
    uint32_t order_count;
    
    PriceLevelNode(double p) : price(p), total_quantity(0), first_order(nullptr), 
                               last_order(nullptr), order_count(0) {}
    
    void add_order(OrderNode* order_node) {
        if (!first_order) {
            first_order = last_order = order_node;
        } else {
            last_order->next = order_node;
            order_node->prev = last_order;
            last_order = order_node;
        }
        total_quantity += order_node->order.quantity;
        order_count++;
    }
    
    void remove_order(OrderNode* order_node) {
        if (order_node->prev) {
            order_node->prev->next = order_node->next;
        } else {
            first_order = order_node->next;
        }
        
        if (order_node->next) {
            order_node->next->prev = order_node->prev;
        } else {
            last_order = order_node->prev;
        }
        
        total_quantity -= order_node->order.quantity;
        order_count--;
    }
    
    bool is_empty() const {
        return order_count == 0;
    }
    
    // For optional matching functionality
    uint64_t match_orders(uint64_t incoming_quantity, MemoryPool<OrderNode>& pool) {
        uint64_t remaining = incoming_quantity;
        OrderNode* current = first_order;
        
        while (current && remaining > 0) {
            uint64_t match_qty = std::min(remaining, current->order.quantity);
            
            current->order.quantity -= match_qty;
            total_quantity -= match_qty;
            remaining -= match_qty;
            
            if (current->order.quantity == 0) {
                OrderNode* to_remove = current;
                current = current->next;
                remove_order(to_remove);
                pool.deallocate(to_remove);
            } else {
                break;
            }
        }
        
        return incoming_quantity - remaining;
    }
};

class OrderBook {
private:
    // Bids: highest price first (descending order)
    std::map<double, std::unique_ptr<PriceLevelNode>, std::greater<double>> bids_map;
    // Asks: lowest price first (ascending order)
    std::map<double, std::unique_ptr<PriceLevelNode>, std::less<double>> asks_map;
    
    // Order lookup for O(1) cancel/amend
    std::unordered_map<uint64_t, OrderNode*> order_lookup;
    
    // Memory pools for cache-friendly allocation
    MemoryPool<OrderNode> order_pool;
    MemoryPool<PriceLevelNode> level_pool;
    
    // Optional: Statistics for matching engine
    uint64_t total_volume;
    uint64_t trade_count;
    
    static constexpr double PRICE_EPSILON = 1e-8;
    
    uint64_t get_timestamp_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    // Optional: Basic matching when best_bid >= best_ask
    void try_match_order(OrderNode* incoming_order_node) {
        if (incoming_order_node->order.is_buy) {
            // Try to match buy order with asks
            while (!asks_map.empty() && incoming_order_node->order.quantity > 0) {
                auto it = asks_map.begin();
                if (incoming_order_node->order.price < it->second->price - PRICE_EPSILON) {
                    break; // No more matches possible
                }
                
                uint64_t matched = it->second->match_orders(incoming_order_node->order.quantity, order_pool);
                incoming_order_node->order.quantity -= matched;
                total_volume += matched;
                
                if (it->second->is_empty()) {
                    level_pool.deallocate(it->second.release());
                    asks_map.erase(it);
                }
                
                if (matched > 0) trade_count++;
            }
        } else {
            // Try to match sell order with bids
            while (!bids_map.empty() && incoming_order_node->order.quantity > 0) {
                auto it = bids_map.begin();
                if (incoming_order_node->order.price > it->second->price + PRICE_EPSILON) {
                    break; // No more matches possible
                }
                
                uint64_t matched = it->second->match_orders(incoming_order_node->order.quantity, order_pool);
                incoming_order_node->order.quantity -= matched;
                total_volume += matched;
                
                if (it->second->is_empty()) {
                    level_pool.deallocate(it->second.release());
                    bids_map.erase(it);
                }
                
                if (matched > 0) trade_count++;
            }
        }
    }
    
    void add_order_to_book(OrderNode* order_node) {
        if (order_node->order.is_buy) {
            // Add to bids
            auto it = bids_map.find(order_node->order.price);
            if (it != bids_map.end()) {
                // Price level exists, add to it
                it->second->add_order(order_node);
            } else {
                // Create new price level
                auto* level = level_pool.allocate(order_node->order.price);
                if (level) {
                    level->add_order(order_node);
                    bids_map.emplace(order_node->order.price, std::unique_ptr<PriceLevelNode>(level));
                }
            }
        } else {
            // Add to asks
            auto it = asks_map.find(order_node->order.price);
            if (it != asks_map.end()) {
                // Price level exists, add to it
                it->second->add_order(order_node);
            } else {
                // Create new price level
                auto* level = level_pool.allocate(order_node->order.price);
                if (level) {
                    level->add_order(order_node);
                    asks_map.emplace(order_node->order.price, std::unique_ptr<PriceLevelNode>(level));
                }
            }
        }
    }
    
public:
    OrderBook() : total_volume(0), trade_count(0) {}
    
    ~OrderBook() {
        // Clean up all orders and levels
        for (auto& [price, level] : bids_map) {
            OrderNode* current = level->first_order;
            while (current) {
                OrderNode* next = current->next;
                order_pool.deallocate(current);
                current = next;
            }
            level_pool.deallocate(level.release());
        }
        
        for (auto& [price, level] : asks_map) {
            OrderNode* current = level->first_order;
            while (current) {
                OrderNode* next = current->next;
                order_pool.deallocate(current);
                current = next;
            }
            level_pool.deallocate(level.release());
        }
    }
    
    // Insert a new order into the book
    void add_order(const Order& order) {
        // Create order node with timestamp if not provided
        Order order_with_ts = order;
        if (order_with_ts.timestamp_ns == 0) {
            order_with_ts.timestamp_ns = get_timestamp_ns();
        }
        
        OrderNode* order_node = order_pool.allocate(order_with_ts);
        if (!order_node) return; // Pool exhausted
        
        // Add to lookup table
        order_lookup[order.order_id] = order_node;
        
        // Optional: Try matching first (for extra credit)
        try_match_order(order_node);
        
        // If any quantity remains, add to book
        if (order_node->order.quantity > 0) {
            add_order_to_book(order_node);
        } else {
            // Fully matched, remove from lookup
            order_lookup.erase(order.order_id);
            order_pool.deallocate(order_node);
        }
    }
    
    // Cancel an existing order by its ID
    bool cancel_order(uint64_t order_id) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false; // Order not found
        }
        
        OrderNode* order_node = it->second;
        
        // Remove from appropriate side
        if (order_node->order.is_buy) {
            auto level_it = bids_map.find(order_node->order.price);
            if (level_it != bids_map.end()) {
                level_it->second->remove_order(order_node);
                
                // Remove price level if empty
                if (level_it->second->is_empty()) {
                    level_pool.deallocate(level_it->second.release());
                    bids_map.erase(level_it);
                }
            }
        } else {
            auto level_it = asks_map.find(order_node->order.price);
            if (level_it != asks_map.end()) {
                level_it->second->remove_order(order_node);
                
                // Remove price level if empty
                if (level_it->second->is_empty()) {
                    level_pool.deallocate(level_it->second.release());
                    asks_map.erase(level_it);
                }
            }
        }
        
        // Remove from lookup and deallocate
        order_lookup.erase(it);
        order_pool.deallocate(order_node);
        return true;
    }
    
    // Amend an existing order's price or quantity
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
        auto it = order_lookup.find(order_id);
        if (it == order_lookup.end()) {
            return false; // Order not found
        }
        
        OrderNode* order_node = it->second;
        
        // If price changes → treat it as cancel + add
        if (std::abs(order_node->order.price - new_price) > PRICE_EPSILON) {
            Order new_order = order_node->order;
            new_order.price = new_price;
            new_order.quantity = new_quantity;
            
            cancel_order(order_id);
            add_order(new_order);
        } else {
            // Only quantity changes → update in place
            if (order_node->order.is_buy) {
                auto level_it = bids_map.find(order_node->order.price);
                if (level_it != bids_map.end()) {
                    level_it->second->total_quantity -= order_node->order.quantity;
                    level_it->second->total_quantity += new_quantity;
                    order_node->order.quantity = new_quantity;
                }
            } else {
                auto level_it = asks_map.find(order_node->order.price);
                if (level_it != asks_map.end()) {
                    level_it->second->total_quantity -= order_node->order.quantity;
                    level_it->second->total_quantity += new_quantity;
                    order_node->order.quantity = new_quantity;
                }
            }
        }
        
        return true;
    }
    
    // Get a snapshot of top N bid and ask levels (aggregated quantities)
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
        bids.clear();
        asks.clear();
        
        // Get top N bids (highest prices first)
        size_t count = 0;
        for (const auto& [price, level] : bids_map) {
            if (count >= depth) break;
            bids.push_back({price, level->total_quantity});
            count++;
        }
        
        // Get top N asks (lowest prices first)
        count = 0;
        for (const auto& [price, level] : asks_map) {
            if (count >= depth) break;
            asks.push_back({price, level->total_quantity});
            count++;
        }
    }
    
    // Print current state of the order book
    void print_book(size_t depth = 10) const {
        std::vector<PriceLevel> bid_levels, ask_levels;
        get_snapshot(depth, bid_levels, ask_levels);
        
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "\n=== ORDER BOOK ===" << std::endl;
        std::cout << "ASK    |  SIZE   |  PRICE" << std::endl;
        std::cout << "-------|---------|--------" << std::endl;
        
        // Print asks in reverse order (highest to lowest for display)
        for (auto it = ask_levels.rbegin(); it != ask_levels.rend(); ++it) {
            std::cout << std::setw(7) << it->total_quantity 
                      << " | " << std::setw(7) << it->total_quantity 
                      << " | " << std::setw(6) << it->price << std::endl;
        }
        
        std::cout << "-------|---------|--------" << std::endl;
        
        if (!bid_levels.empty() && !ask_levels.empty()) {
            double spread = ask_levels[0].price - bid_levels[0].price;
            std::cout << "SPREAD: " << spread << std::endl;
        }
        
        std::cout << "-------|---------|--------" << std::endl;
        std::cout << "BID    |  SIZE   |  PRICE" << std::endl;
        
        // Print bids (highest to lowest)
        for (const auto& level : bid_levels) {
            std::cout << std::setw(7) << level.total_quantity 
                      << " | " << std::setw(7) << level.total_quantity 
                      << " | " << std::setw(6) << level.price << std::endl;
        }
        
        std::cout << "\nTotal Volume: " << total_volume 
                  << ", Trades: " << trade_count 
                  << ", Active Orders: " << order_lookup.size() << std::endl;
    }
};

// Performance benchmark
void performance_benchmark() {
    OrderBook book;
    const size_t num_orders = 100000;
    
    std::cout << "\n=== PERFORMANCE BENCHMARK ===" << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Add orders
    for (size_t i = 0; i < num_orders; ++i) {
        Order order;
        order.order_id = i;
        order.is_buy = (i % 2 == 0);
        order.price = 100.0 + (i % 200) * 0.01;
        order.quantity = 100 + (i % 500);
        order.timestamp_ns = 0;
        
        book.add_order(order);
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    // Cancel half the orders
    for (size_t i = 0; i < num_orders / 2; ++i) {
        book.cancel_order(i * 2);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto add_time = std::chrono::duration_cast<std::chrono::microseconds>(mid - start);
    auto cancel_time = std::chrono::duration_cast<std::chrono::microseconds>(end - mid);
    
    std::cout << "Added " << num_orders << " orders in " << add_time.count() << " µs" << std::endl;
    std::cout << "Cancelled " << num_orders/2 << " orders in " << cancel_time.count() << " µs" << std::endl;
    std::cout << "Average add latency: " << std::fixed << std::setprecision(3) 
              << (double)add_time.count() / num_orders << " µs/order" << std::endl;
    std::cout << "Average cancel latency: " << std::fixed << std::setprecision(3) 
              << (double)cancel_time.count() / (num_orders/2) << " µs/order" << std::endl;
}

// Main demonstration
int main() {
    OrderBook book;
    
    std::cout << "Low-Latency Order Book - Final Implementation" << std::endl;
    std::cout << "Features: Memory pools, FIFO ordering, optional matching engine" << std::endl;
    
    // Test all required operations
    Order order1{1, false, 100.05, 1000, 0};  // SELL 1000@100.05
    Order order2{2, false, 100.10, 800, 0};   // SELL 800@100.10
    Order order3{3, true, 99.95, 500, 0};     // BUY 500@99.95
    Order order4{4, true, 99.90, 300, 0};     // BUY 300@99.90
    
    std::cout << "\n1. Building initial order book..." << std::endl;
    book.add_order(order1);
    book.add_order(order2);
    book.add_order(order3);
    book.add_order(order4);
    book.print_book(5);
    
    std::cout << "\n2. Testing matching with aggressive order..." << std::endl;
    Order order5{5, true, 100.08, 1200, 0};  // BUY 1200@100.08 - should match
    book.add_order(order5);
    book.print_book(5);
    
    std::cout << "\n3. Testing cancel operation..." << std::endl;
    bool canceled = book.cancel_order(4);
    std::cout << "Cancel order 4: " << (canceled ? "SUCCESS" : "FAILED") << std::endl;
    book.print_book(5);
    
    std::cout << "\n4. Testing amend operation (quantity only)..." << std::endl;
    bool amended = book.amend_order(3, 99.95, 800);  // Change quantity
    std::cout << "Amend order 3 quantity: " << (amended ? "SUCCESS" : "FAILED") << std::endl;
    book.print_book(5);
    
    std::cout << "\n5. Testing amend operation (price change)..." << std::endl;
    amended = book.amend_order(2, 99.85, 800);  // Change price - causes matching
    std::cout << "Amend order 2 price: " << (amended ? "SUCCESS" : "FAILED") << std::endl;
    book.print_book(5);
    
    std::cout << "\n6. Testing snapshot functionality..." << std::endl;
    std::vector<PriceLevel> bids, asks;
    book.get_snapshot(3, bids, asks);
    
    std::cout << "Top 3 bids: ";
    for (const auto& level : bids) {
        std::cout << level.total_quantity << "@" << std::fixed << std::setprecision(2) << level.price << " ";
    }
    std::cout << "\nTop 3 asks: ";
    for (const auto& level : asks) {
        std::cout << level.total_quantity << "@" << std::fixed << std::setprecision(2) << level.price << " ";
    }
    std::cout << std::endl;
    
    // Performance benchmark
    performance_benchmark();
    
    return 0;
}