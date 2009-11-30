/*
 * map_ops.h
 *
 *  Created on: Jun 15, 2009
 *      Author: bobw
 */

#ifndef MAP_OPS_H_
#define MAP_OPS_H_


// Concept of entry_maker
/*
struct entry_maker_t {
	  key_type make_key(int keynum);
	  value_type make_value(int keynum);
};
*/


template <class database_type, class map_type, class entry_maker_t>
class insert_trans_operation : public trans_operation
{
public:
	insert_trans_operation(database_type &db, int inserts_per_txn, entry_maker_t &entry_maker,
			boost::interprocess::interprocess_upgradable_mutex *map_mutex)
		: db(db), entry_maker(entry_maker), txnSize(inserts_per_txn), last_key(0)
		, inserts(0), dups(0), mutex(map_mutex)
		{ }

	virtual void operator()() {
		timer t("insert_trans_operation");

		map_type &map = *db.getMap();

		stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());

		{ // lock scope
			scoped_lock<boost::interprocess::interprocess_upgradable_mutex> lock_holder(*mutex);

			for (int j=0; j<txnSize; j++) {
				// insert the pair
				std::pair<typename map_type::iterator, bool> result
					= map.insert( std::make_pair<
							typename map_type::key_type,
							typename map_type::mapped_type> (
									entry_maker.make_key(last_key),
									entry_maker.make_value(last_key) ) );
				last_key++;

				if (result.second)
					inserts++;
				else
					dups++;
			}
		}
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int txnSize;
	int last_key;
	int inserts, dups;
	boost::interprocess::interprocess_upgradable_mutex *mutex;
};


template <class database_type, class map_type, class entry_maker_t>
class find_trans_operation : public trans_operation
{
public:
	find_trans_operation(database_type &db, int highest_key, int finds_per_txn, entry_maker_t &maker,
			boost::interprocess::interprocess_upgradable_mutex *map_mutex)
		: db(db), entry_maker(maker), highest_key(highest_key), txnSize(finds_per_txn)
		, hits(0), misses(0), mutex(map_mutex)
		{ }

	virtual void operator()() {
		timer t("find_trans_operation");

		stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());
		map_type &map = *db.getMap();

		{ // lock scope
			sharable_lock<boost::interprocess::interprocess_upgradable_mutex> lock_holder(*mutex);

			int j = static_cast<int>(::rand() * ((double)(highest_key - txnSize) / (double)RAND_MAX));

			typename map_type::iterator i = map.find( entry_maker.make_key(j) );
			if ( i == map.end() )
				misses++;
			else {
				for (int l=0; i != map.end() && l<txnSize; l++, i++) {
					hits++;
				}
			}
		}
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int highest_key, txnSize;
	int hits, misses;
	boost::interprocess::interprocess_upgradable_mutex *mutex;
};


template <class database_type, class map_type, class entry_maker_t>
class update_trans_operation : public trans_operation
{
public:
	update_trans_operation(database_type &db, int highest_key, int txn_size, entry_maker_t &maker,
			boost::interprocess::interprocess_upgradable_mutex *map_mutex)
		: db(db), entry_maker(maker), highest_key(highest_key), txnSize(txn_size)
		, hits(0), misses(0), mutex(map_mutex)
		{ }

	virtual void operator()()
	{
		timer t("update_trans_operation");

		stldb::scoped_allocation<typename database_type::db_type::RegionType::segment_manager> a(db.getRegion().get_segment_manager());
		map_type &map = *db.getMap();

		{//lock scope
			scoped_lock<boost::interprocess::interprocess_upgradable_mutex> lock_holder(*mutex);

			for (int j=0; j<txnSize; j++) {
				int k = static_cast<int>(::rand() * (((double)highest_key) / (double)RAND_MAX));
				//typename map_type::key_type key( entry_maker.make_key(j) );

				typename map_type::iterator i = map.find( entry_maker.make_key(k) );
				if (i == map.end()) {
					misses++;
					continue;
				}

				typename map_type::mapped_type newValue = entry_maker.make_value(j);
				i->second = newValue;
			}
		}
	}

private:
	database_type &db;
	entry_maker_t entry_maker;
	int highest_key, txnSize;
	int hits, misses;
	boost::interprocess::interprocess_upgradable_mutex *mutex;
};


#endif /* MAP_OPS_H_ */
