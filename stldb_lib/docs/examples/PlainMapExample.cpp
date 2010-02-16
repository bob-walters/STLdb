/*
 * PlainMapExample.cpp
 *
 *  Created on: Dec 30, 2009
 *      Author: bobw
 */

//[ types
#include <string>
#include <map>
#include <cassert>

typedef long date_t;  // days since epoch.

// customer record for my shoe store
struct customer_t {
	std::string name;
	int shoe_size;
	date_t last_purchase_date;
};

// account record for customers.
struct account_t {
	int account_num;
	std::string customer_name;
	date_t account_start_date;
	long balance;
	date_t last_payment_date;

	void add_purchase( double amount ) {
		balance += amount;
	}
	void add_payment( double amount ) {
		balance -= amount;
	}
};
//]

//[ maptypes
// "table" types in my database.
typedef std::map<std::string,customer_t> customer_map_t;
typedef std::map<std::string,account_t> account_map_t;

typedef customer_map_t::iterator customer_ref;
typedef account_map_t::iterator account_ref;
//]

//[ main
// operations...
int main(int argc, const char *argv[])
{
	std::string name = "John Smith";

	// 1) create database
	customer_map_t  cust_map;
	account_map_t account_map;

	// create a customer and account within the database
	customer_t cust;
	cust.name = name;
	cust.shoe_size = 8;
	cust_map.insert(std::make_pair(name, cust));

	assert( account_map.find(name) == account_map.end() );
	account_t account;
	account.account_num = account_map.size();  // next available
	account.customer_name = name;
	account.account_start_date = 39785; // ~2009
	account.balance = 0.0;
	account.last_payment_date = -1; // never
	account_map.insert(std::make_pair(name, account));

	// transaction 1: find a customer by name, and confirm that it
	// has an account with us.
	customer_ref custref = cust_map.find(name);
	assert(custref->second.shoe_size == 8);
	account_ref accountref = account_map.find(name);
	assert(accountref != account_map.end());

	// transaction 2: add a purchase to an account by name.
	accountref = account_map.find(name);
	accountref->second.add_purchase(100.0);
	assert(accountref->second.balance == 100.0);

	// transaction 3: add a payment to an account.
	accountref->second.add_payment(60.0);
	assert(accountref->second.balance == 40.0);

	// transaction 4: remove the account.
	account_map.erase(accountref);
}
//]
