#include <iostream>

class X {
public:
	virtual ~X() {
		std::cout << "~X() called" << std::endl;
	}
};

class Y : public X {
public:
	void operator delete(void *obj) {
		std::cout << "Y::delete called" << std::endl;
	}
};

class Z : public X {
public:
	void operator delete(void *obj) {
		std::cout << "Y::delete called" << std::endl;
	}
};

int test_util4() {
	X *x = new X();
	X *y = new Y();
	X *z = new Z();

	delete x;
	delete y;
	delete z;

	return 0;
};
