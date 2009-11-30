#ifndef STLDB_TEST_TRANSMAP_STRING_OPS_
#define STLDB_TEST_TRANSMAP_STRING_OPS_

#include "perf_test.h"
#include <stldb/sync/wait_policy.h>

// Concept of entry_maker
/*
struct entry_maker_t {
	  key_type make_key(int keynum);
	  mapped_type make_value(int keynum);
};
*/

// map_type is required to be some instantiation of trans_map<>
template <class database_type, class map_type, class entry_maker_t>
class insert_trans_operation : public trans_operation
{
public:
	insert_trans_operation(database_type &db, int inserts_per_txn, entry_maker_t &entry_maker)
		: db(db)
		, entry_maker(entry_maker), txnSize(inserts_per_txn), last_key(0)
		, inserts(0), dups(0)
		{ }

	virtual void operator()() {
		timer t("insert_trans_operation");

		map_type &map = *db.getMap();
		stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());

		// Start a transaction
		Transaction *txn = db.beginTransaction(0);

		{ // lock scope
			scoped_lock<typename map_type::upgradable_mutex_type> lock_holder(map.mutex());

			for (int j=0; j<txnSize; j++) {
				// insert the pair
				std::pair<typename map_type::iterator, bool> result
					= map.insert( std::make_pair<
							typename map_type::key_type,
							typename map_type::mapped_type> (
									entry_maker.make_key(last_key),
									entry_maker.make_value(last_key) ), *txn );
				last_key++;

				if (result.second)
					inserts++;
				else
					dups++;
			}
		}

		db.commit(txn);
	}

	virtual void print_totals() {
		std::cout << "insert_trans_op: inserts: " << inserts << ", dups: " << dups << ", last_key: " << last_key << std::endl;
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int txnSize;
	int last_key;
	char sampleData[2048];
	int inserts, dups;
};

template <class database_type, class map_type, class entry_maker_t>
class find_trans_operation : public trans_operation
{
public:
	find_trans_operation(database_type &db, int highest_key, int finds_per_txn, entry_maker_t &maker)
		: db(db)
		, entry_maker(maker), highest_key(highest_key)
		, txnSize(finds_per_txn), hits(0), misses(0)
		{ }

	virtual void operator()() {
		timer t("find_trans_operation");

		map_type &map = *db.getMap();
		//stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());

		// Start a transaction
		Transaction *txn = db.beginTransaction(0);

		{ // lock scope
			sharable_lock<typename map_type::upgradable_mutex_type> lock_holder(map.mutex());

			int j = static_cast<int>(::rand() * ((double)(highest_key - txnSize) / (double)RAND_MAX));

			typename map_type::iterator i = map.find( entry_maker.make_key(j), *txn );
			if ( i == map.end() )
				misses++;
			else {
				for (int l=0; i != map.end() && l<txnSize; ++l, ++i) {
					hits++;
				}
			}
		}

		db.commit(txn);
	}

	virtual void print_totals() {
		std::cout << "find_trans_op: hits(est): " << hits << ", misses(est): " << misses << std::endl;
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int highest_key, txnSize;
	int hits, misses;
};


template <class database_type, class map_type, class entry_maker_t>
class update_trans_operation : public trans_operation
{
public:
	update_trans_operation(database_type &db, int highest_key, int txn_size, entry_maker_t &maker)
		: db(db)
		, entry_maker(maker), highest_key(highest_key)
		, txnSize(txn_size), hits(0), misses(0)
		{ }

	virtual void operator()()
	{
		timer t("update_trans_operation");

		map_type &map = *db.getMap();
		//stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());

		// find each of the rows now, and update them.
		Transaction *txn = db.beginTransaction(0);

		{//lock scope
			typedef sharable_lock<typename map_type::upgradable_mutex_type> lock_t;
			lock_t lock_holder(map.mutex());

			stldb::bounded_wait_policy< lock_t > wait_policy(lock_holder, boost::posix_time::seconds(10));

			for (int j=0; j<txnSize; j++) {
				int k = static_cast<int>(::rand() * (((double)highest_key) / (double)RAND_MAX));
				//typename map_type::key_type key( entry_maker.make_key(j) );

				typename map_type::iterator i = map.find( entry_maker.make_key(k), *txn );
				if (i == map.end()) {
					misses++;
					continue;
				}
				hits++;

				stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());
				typename map_type::mapped_type newValue = entry_maker.make_value(j);
				map.update(i, newValue, *txn, wait_policy);
			}
		}

		db.commit(txn);
	}

	virtual void print_totals() {
		std::cout << "update_trans_op: hits(est): " << hits << ", misses(est): " << misses << std::endl;
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int highest_key, txnSize;
	int hits, misses;
};

#endif

