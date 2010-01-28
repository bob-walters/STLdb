
#ifndef STLDB_DATABASE_CONFIG_
#define STLDB_DATABASE_CONFIG_ 1

#include <string>
#include <list>
#include <vector>
#include <map>
#include <iostream>

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/map.hpp>

#ifndef BOOST_ARCHIVE_TEXT
#include <boost/archive/binary_iarchive.hpp>
typedef boost::archive::binary_iarchive boost_iarchive_t;
#else
#include <boost/archive/text_iarchive.hpp>
typedef boost::archive::text_iarchive boost_iarchive_t;
#endif

#include <stldb/cachetypes.h>

using boost::interprocess::map;
using boost::interprocess::allocator;

namespace stldb {


// Forward declarations...
template <class ManagedRegionType> class Database;
class TransactionalOperation;
class Transaction;
class checkpoint_ifstream;
class checkpoint_ofstream;

/**
 * Container_proxy_base is the interface between the Database
 * and the contaners within it.  It permits some generic functionality
 * which depends on the run-time polymorphism provided by this class.
 * Every container passes a proxy to the database at the time the
 * database constructor is called.  The database then uses the proxy
 * to create, load, recover, etc. the containers within it.
 */
template <class ManagedRegionType>
class container_proxy_base {
public:
	virtual ~container_proxy_base() { }

	// Called during database construction to open or create the container.
	// Also, the proxy can "remember" the database at this point if it may need to
	// issue callbacks against the database.  (Found this value while implementing
	// the recovery of swap operations involving multiple containers.)
	// TODO - provide open, open_or_create, and create versions to match Database constructors
	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) = 0;

	// Called during recovery processing to re-apply a transaction_operation which
	// MIGHT have not made it to the checkpoint file, but is recorded in the log.
	// The op code allows the container to determine the operation which needs to be
	// re-applied, and the stream contains the rest of the data from the serialized
	// TransactionOperation.
	virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) = 0;

	// Return the name of the container
	virtual std::string getName() { return container_name; }

	// Called against the container during commit, to prepare the container for the commit
	// of the transactional ops accumulated on the transaction.  Is expected to acquire an
	// exclusive lock on the container which will be held until completeCommit is called.
	virtual void initializeCommit(Transaction &trans) = 0;

	// Called when a commit operation against a container has been completed.  i.e. The
	// commit() method on all TransactionalOperations has been completed.
	virtual void completeCommit(Transaction &trans) = 0;

	// Called when a rollback operation is initiated against a container.  This is called
	// prior to calling rollback() on the individual transactional ops which make up the transaction
	// against the container.
	virtual void initializeRollback(Transaction &trans) = 0;

	// Called when a rollback() operation is completed.
	virtual void completeRollback(Transaction &trans) = 0;

	// Called when the container should write it's current changes to the
	// checkpoint file passed.  All rows which have been modified after last_checkpoint_lsn
	// should be written to the checkpoint_filename passed.
	// Upon completion, new free space shold be added to the checkpoints free space.
	virtual void save_checkpoint(Database<ManagedRegionType> &db,
			checkpoint_ofstream &checkpoint,
			transaction_id_t last_checkpoint_lsn) = 0;

	// Called when the container should load its contents from the input stream
	// EOF marks the end of the data stream.
	virtual void load_checkpoint(checkpoint_ifstream &checkpoint) = 0;

protected:
	// Every container in the database has a human-readable name
	// along with an integer id which is used in log files to
	// minimize metadata overhead.
	container_proxy_base(const char *name)
		: container_name(name)
		{ }

	const std::string container_name;
};

// An implementation of ContainerConfig which can construct any
// Container that meets the following contract:
// (a) has a 1 arg constructor that takes an allocator.
// (b) has a mutex() and a condition() member for locking/notification.
// (c) has a recoverOp(opcode, stream, lsn) method for re-applying logs.
template <class ManagedRegionType, class ContainerT>
class container_proxy : public container_proxy_base<ManagedRegionType> {
public:
	typedef ContainerT container_type;
	typedef container_proxy_base<ManagedRegionType>  base;
    typedef typename ContainerT::allocator_type  allocator_type;

	container_proxy(const char *name)
		: container_proxy_base<ManagedRegionType>(name)
		{ }

	virtual ~container_proxy() { }

	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
		allocator_type alloc( db.getRegion().get_segment_manager() );
		_container = db.getRegion().template find_or_construct<container_type>
			(base::container_name.c_str())(alloc);
		return _container;
	}

	virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) {
		_container->recoverOp(opcode, stream, lsn);
	}
	virtual void initializeCommit(Transaction &trans)
	{
		_container->mutex().lock();
	}
	virtual void completeCommit(Transaction &trans)
	{
		_container->condition().notify_all(); // wake up anyone waiting on a row lock
		_container->mutex().unlock();
	}
	virtual void initializeRollback(Transaction &trans)
	{
		_container->mutex().lock();
	}
	virtual void completeRollback(Transaction &trans)
	{
		_container->condition().notify_all(); // wake up anyone waiting on a row lock
		_container->mutex().unlock();
	}
	virtual void save_checkpoint(Database<ManagedRegionType> &db,
			checkpoint_ofstream &checkpoint,
			transaction_id_t last_checkpoint_lsn)
	{
		_container->checkpoint(checkpoint,last_checkpoint_lsn);
	}
	virtual void load_checkpoint(checkpoint_ifstream &checkpoint)
	{
		_container->load_checkpoint(checkpoint);
	}

private:
	container_type *_container;
};


//!Specialization of container_proxy for map, to deal with
//!map's 2 arg constructor.
template <class ManagedRegionType, class K, class V, class Pred, class Alloc>
class container_proxy<ManagedRegionType, boost::interprocess::map<K, V, Pred, Alloc> >
	: public container_proxy_base<ManagedRegionType>
{
public:
	typedef boost::interprocess::map<K,V,Pred,Alloc> container_type;
	typedef container_proxy_base<ManagedRegionType>  base;
	typedef typename container_type::allocator_type allocator_type;

	container_proxy(const char *name)
		: container_proxy_base<ManagedRegionType>(name)
		{ }

	virtual ~container_proxy() { }

	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
		allocator_type alloc( db.getRegion().get_segment_manager() );
		_container = db.getRegion().template find_or_construct<container_type>
			(base::container_name.c_str())(Pred(), alloc);
		return _container;
	}

	virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) {
		//_container->recoverOp(opcode, stream, lsn);
	}
	virtual void initializeCommit(Transaction &trans)
	{
		//_container->mutex().lock();
	}
	virtual void completeCommit(Transaction &trans)
	{
		//_container->set_max_committed(new_max_log_seq);
		//_container->condition().notify_all(); // wake up anyone waiting on a row lock
		//_container->mutex().unlock();
	}
	virtual void initializeRollback(Transaction &trans)
	{
		//_container->mutex().lock();
	}
	virtual void completeRollback(Transaction &trans)
	{
		//_container->condition().notify_all(); // wake up anyone waiting on a row lock
		//_container->mutex().unlock();
	}
	virtual void save_checkpoint(Database<ManagedRegionType> &db,
			checkpoint_ofstream &checkpoint,
			transaction_id_t last_checkpoint_lsn)
	{
		// _container->checkpoint(checkpoint,last_checkpoint_lsn);
	}
	virtual void load_checkpoint(checkpoint_ifstream &checkpoint)
	{
		// _container->load_checkpoint(checkpoint);
	}

private:
	container_type *_container;
};


//!Specialization of container_proxy for map, to deal with
//!map's 2 arg constructor.
template <class ManagedRegionType, class K, class V, class Pred, class Alloc>
class container_proxy<ManagedRegionType, std::map<K, V, Pred, Alloc> >
	: public container_proxy_base<ManagedRegionType>
{
public:
	typedef std::map<K,V,Pred,Alloc> container_type;
	typedef container_proxy_base<ManagedRegionType>  base;
	typedef typename container_type::allocator_type allocator_type;

	container_proxy(const char *name)
		: container_proxy_base<ManagedRegionType>(name)
		{ }

	virtual ~container_proxy() { }

	virtual void* find_or_construct_container(Database<ManagedRegionType> &db) {
		allocator_type alloc( db.getRegion().get_segment_manager() );
		_container = db.getRegion().template find_or_construct<container_type>
			(base::container_name.c_str())(Pred(), alloc);
		return _container;
	}

	virtual void recoverOp(int opcode, boost_iarchive_t &stream, transaction_id_t lsn) {
		//_container->recoverOp(opcode, stream, lsn);
	}
	virtual void initializeCommit(Transaction &trans)
	{
		//_container->mutex().lock();
	}
	virtual void completeCommit(Transaction &trans)
	{
		//_container->set_max_committed(new_max_log_seq);
		//_container->condition().notify_all(); // wake up anyone waiting on a row lock
		//_container->mutex().unlock();
	}
	virtual void initializeRollback(Transaction &trans)
	{
		//_container->mutex().lock();
	}
	virtual void completeRollback(Transaction &trans)
	{
		//_container->condition().notify_all(); // wake up anyone waiting on a row lock
		//_container->mutex().unlock();
	}
	virtual void save_checkpoint(Database<ManagedRegionType> &db,
			checkpoint_ofstream &checkpoint,
			transaction_id_t last_checkpoint_lsn)
	{
		// _container->checkpoint(checkpoint,last_checkpoint_lsn);
	}
	virtual void load_checkpoint(checkpoint_ifstream &checkpoint)
	{
		// _container->load_checkpoint(checkpoint);
	}

private:
	container_type *_container;
};


} // namespace

#endif
