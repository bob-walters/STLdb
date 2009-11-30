/*
 * times.h
 *
 *  Created on: Jun 1, 2009
 *      Author: rwalter3
 */
#ifndef TIMES_H_
#define TIMES_H_

#include <set>
#include <string>
#include <map>

#ifdef NO_STDINT_H
#include <stldb/detail/ints.h>
#else
#include <stdint.h>
#endif

#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/tss.hpp>

#include <stldb/stldb.hpp>


namespace stldb {

// fwd declaration
struct process_time_tracked;

/**
 * Holds data about the performance of some scope of code, based on times collected
 * by using the timer class.
 */
struct time_tracked {
	std::string name;
	uint64_t count;
	uint64_t total_elapsed;
	uint64_t max_elapsed;
	uint64_t min_elapsed;
	process_time_tracked *process_level;
	std::map<std::string, time_tracked> children;

	time_tracked() : name(), count(0), total_elapsed(0)
			, max_elapsed(0), min_elapsed((int64_t)-1)
			, process_level(NULL), children()
		{ }

	time_tracked(const char *s) : name(s), count(0), total_elapsed(0)
		, max_elapsed(0), min_elapsed((int64_t)-1)
		, process_level(NULL), children()
		{ }

	time_tracked(const std::string &s) : name(s), count(0), total_elapsed(0)
		, max_elapsed(0), min_elapsed((int64_t)-1)
		, process_level(NULL), children()
		{ }

	time_tracked(const time_tracked &rarg)
		: name(rarg.name), count(rarg.count), total_elapsed(rarg.total_elapsed)
		, max_elapsed(rarg.max_elapsed), min_elapsed(rarg.min_elapsed)
		, process_level(rarg.process_level), children(rarg.children)
		{ }

	// an an invocation of the specified duration
	void add_invocation(uint64_t duration) {
		count++;
		total_elapsed += duration;
		if (duration > max_elapsed)
			max_elapsed = duration;
		if ( duration < min_elapsed )
			min_elapsed = duration;
	}

	// Add a time_tracked object to another.  (Operator helps summarization)
	time_tracked& operator+=(const time_tracked &rarg) {
		count += rarg.count;
		total_elapsed += rarg.total_elapsed;
		if ( rarg.max_elapsed > max_elapsed)
			max_elapsed = rarg.max_elapsed;
		if ( rarg.min_elapsed < min_elapsed )
			min_elapsed = rarg.min_elapsed;
		return *this;
	}

	// Return the child of the this with the indicated name
	time_tracked* get_child(const char *name);

	// return the root time_tracked object for the calling thread
	static time_tracked* get_root();

	// kept to help with legacy design
	template <class ostream>
	static void print(ostream &s, bool reset=false);

	// The current time_tracked object to be updated by timers when their destructors are
	// called.  Moves up and down the tree as local timer objects are constructed.
	static BOOST_STLDB_DECL boost::thread_specific_ptr<time_tracked>  current_timer;

	// The root time_tracked object for the whole process (root of tree)
	static BOOST_STLDB_DECL boost::thread_specific_ptr<time_tracked>  thread_root;
};


struct process_time_tracked {
	std::string name;
	std::set<time_tracked*> thread_time_tracked;
	std::map<std::string, process_time_tracked> children;
	boost::mutex mutex;

	process_time_tracked() : name()
		, thread_time_tracked() , children(), mutex()
		{ }

	process_time_tracked(const std::string &s)
		: name(s)
		, thread_time_tracked(), children(), mutex()
		{ }

	process_time_tracked(const process_time_tracked &rarg)
		: name(rarg.name)
		, thread_time_tracked(rarg.thread_time_tracked)
		, children(rarg.children), mutex()
		{ }

	// Add a child process_time_tracked element based on the child time_tracked
	// element that some thread is creating.
	process_time_tracked* add_child(time_tracked *tt) {
		boost::lock_guard<boost::mutex> guard(this->mutex);
		std::map<std::string, process_time_tracked>::iterator i = children.find(tt->name);
		if (i == children.end()) {
			std::pair<std::map<std::string, process_time_tracked>::iterator, bool> j = children.insert(
					make_pair( tt->name, process_time_tracked(tt->name) ) );
			j.first->second.thread_time_tracked.insert(tt);
			i = j.first;
		}
		i->second.thread_time_tracked.insert(tt);
		return & i->second;
	}

	void add(time_tracked *tt) {
		boost::lock_guard<boost::mutex> guard(this->mutex);
		this->thread_time_tracked.insert(tt);
	}

	template <class ostream>
	void print(ostream &s, int level=0, bool reset=false) {
		boost::lock_guard<boost::mutex> guard(this->mutex);

		time_tracked sum(this->name);
		for ( std::set<time_tracked*>::iterator i=this->thread_time_tracked.begin();
			  i != this->thread_time_tracked.end(); i++ )
		{
			sum += **i;
		}
		if (sum.count == 0)
			return;
		for (int i=0; i<level; i++) {
			s << ". ";
		}
		s	<< name
			<< ",count(" << sum.count << ")"
			<< ",totaltime(" << sum.total_elapsed << ")"
			<< ",min(" << sum.min_elapsed << ")"
			<< ",avg(" << (sum.total_elapsed/sum.count) << ")"
			<< ",max(" << sum.max_elapsed << ")"
			<< std::endl;
		if (reset) {
			for ( std::set<time_tracked*>::iterator i=this->thread_time_tracked.begin();
				  i != this->thread_time_tracked.end(); i++ )
			{
				(*i)->count = (*i)->total_elapsed = (*i)->max_elapsed = 0;
				(*i)->min_elapsed = (int64_t)-1;
			}
		}
		for (std::map<std::string,process_time_tracked>::iterator i = children.begin();
			 i != children.end(); i++) {
			i->second.print(s, level+1, reset);
		}
	}

	template <class ostream>
	static void print(ostream &s, bool reset=false)
	{
		boost::lock_guard<boost::mutex> guard(process_root.mutex);
		for (std::map<std::string,process_time_tracked>::iterator i = process_root.children.begin();
			 i != process_root.children.end(); i++) {
			i->second.print(s, 0, reset);
		}
	}


	// The root time_tracked object for the whole process (root of tree)
	static BOOST_STLDB_DECL process_time_tracked process_root;
};

inline time_tracked* time_tracked::get_child(const char *name) {
	std::map<std::string, time_tracked>::iterator i = children.find(name);
	if (i != children.end() )
			return & i->second;
	std::pair<std::map<std::string, time_tracked>::iterator, bool> j = children.insert(
			std::make_pair( name, time_tracked(name) ) );
	j.first->second.process_level = this->process_level->add_child(& j.first->second);
	return & j.first->second;
}

inline time_tracked* time_tracked::get_root() {
	time_tracked* troot = thread_root.get();
	if (troot != NULL)
		return troot;
	troot = new time_tracked();
	process_time_tracked::process_root.add(troot);
	troot->process_level = &process_time_tracked::process_root;
	thread_root.reset(troot);
	return troot;
}

template <class ostream>
void time_tracked::print(ostream &s, bool reset) {
	process_time_tracked::print(s,reset);
}


} // namespace
#endif /* TIMES_H_ */
