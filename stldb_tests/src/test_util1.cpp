/*
 * test_util1.cpp
 *
 *  Created on: May 24, 2009
 *      Author: bobw
 */

#include <iostream>
#include <boost/bind.hpp>
#include <boost/function.hpp>

class FooBar {
public:
	int foo() { return 0; }
	int foo(int x) { return x; }

	int bar(int x) { return x; }
	void whatifvoid(int x) { std::cout << "whatifvoid() called" << std::endl; }
};

// invokes the method passed as bound func, passing i.
template<class bind_t>
typename bind_t::result_type
blocking_invoke(const bind_t &boundfunc, int i)
{
	return boundfunc(i);
};

int test_util1()
{
	int i=4;
	//boost::function<int (FooBar*, int)> func( &FooBar::bar );
	FooBar fb;

	//int result = func(&fb, 5);
	int result = blocking_invoke( boost::bind(&FooBar::bar, &fb, _1), i );
	assert(result==4);

	// How do I ever ID one among a set of overloaded methods with the same name?
	//boost::bind bound_foo(((&FooBar::foo), _1) );
	//std::cout << bound_foo(i) << endl;;

	blocking_invoke( boost::bind(&FooBar::whatifvoid, &fb, _1), i );

	return 0;
}
