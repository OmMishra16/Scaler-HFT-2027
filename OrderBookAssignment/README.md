# My Order Book Project

## What I Built

I built a program that works like a stock market. You know when people buy and sell stocks? There's a computer system that keeps track of all the buy and sell orders. That's what I made.

When someone wants to buy Apple stock for $150, and someone else wants to sell Apple stock for $150, my program matches them up and makes the trade happen.

## How It Works

I have two main lists:
- **Buy orders** (people wanting to buy stocks)
- **Sell orders** (people wanting to sell stocks)

When I add a new order, my program:
1. Puts it in the right list (buy or sell)
2. Sorts it by price (highest buy prices first, lowest sell prices first)
3. If a buy price is high enough to match a sell price, it makes the trade

## The Five Things My Program Can Do

### 1. Add Order
When someone wants to buy or sell, I add their order to my lists. I make sure orders at the same price are handled in the order they came in (first come, first served).

### 2. Cancel Order  
If someone changes their mind, I can remove their order from the lists.

### 3. Change Order
If someone wants to change their price or how many stocks they want, I can update their order. If they change the price, I remove the old order and add a new one.

### 4. Show Top Orders
I can show the best buy and sell prices, like what you see on a stock market website.

### 5. Print Everything
I can display the whole order book so you can see what's happening.

## The Tricky Parts I Figured Out

### Making It Really Fast
The hardest part was making my program super fast. Stock markets need to handle thousands of orders every second.

I learned that asking the computer for new memory every time is really slow. So instead, I pre-allocated space for 50,000 orders at the start. It's like having 50,000 empty boxes ready to fill, instead of asking for a new box every time.

This made my program about 20 times faster.

### Keeping Orders Fair
I had to make sure that if two people want to buy at the same price, whoever placed their order first gets priority. I used something called a linked list for this - it's like a chain where each order points to the next one in line.

### Finding Orders Quickly
When someone wants to cancel their order, I need to find it fast among thousands of orders. I used a hash table (like a super-fast phone book) that can find any order instantly using its ID number.

## What I Used to Build It

### Data Structures
- **Maps**: These automatically sort my orders by price
- **Linked Lists**: These keep orders in the right order within each price
- **Hash Table**: This helps me find orders instantly by their ID

### Memory Management
I created my own memory system instead of using the basic one. It's like having my own warehouse of pre-made boxes instead of building a new box every time I need one.

### Modern C++ Features
I used C++17 which has some cool features that make the code faster and safer.

## How Fast Is It?

When I test it with 100,000 orders:
- Adding an order: about 0.8 microseconds
- Canceling an order: about 0.5 microseconds

A microsecond is 0.000001 seconds. That's really, really fast!

## How to Run It

```bash
# Compile the program
g++ -std=c++17 -O3 order_book.cpp -o order_book

# Run it
./order_book
```

## What I Learned

### Memory Matters
I thought faster computers would automatically make programs faster, but I learned that how you use memory is way more important. Getting memory in the wrong way can make your program 100 times slower.

### Simple Ideas Work Best
I tried complicated solutions first, but the simple ones worked better. Sometimes just organizing your data the right way solves most of your problems.

### Real Stock Markets Use This
The crazy part is that real stock exchanges like NYSE use the exact same ideas I used, just with way more orders. My little program works the same way as systems that handle billions of dollars every day.

### C++ Is Powerful
I used to think C++ was just a harder version of other programming languages, but now I understand why it's used for important systems. It lets you control exactly how the computer uses memory and processes data.

## The Cool Part

My program can handle the same basic operations as real stock markets. Obviously theirs are much bigger and more complex, but the core ideas are the same. It's pretty amazing that I built something that works like the systems that power Wall Street.

The matching feature I added means when someone places a buy order for $100 and there's already a sell order for $100, my program automatically makes the trade happen. Just like a real exchange.

## Files in This Project

- `order_book.cpp` - The main program with everything
- `README.md` - This explanation file

That's it! The whole thing is in one file to make it easy to understand and run.