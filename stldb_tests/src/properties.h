/*
 * properties.h
 *
 *  Created on: Jun 15, 2009
 *      Author: rwalter3
 */

#ifndef PROPERTIES_H_
#define PROPERTIES_H_

#include <iostream>
using std::cout;
using std::endl;

class properties_t {
public:
	properties_t() : _props() { }

	void parse_args(int argc, const char *argv[])
	{
		for (int i=1; i<argc; i++) {
			const char* idx=strchr(argv[i], '=');
			if (idx) {
				std::string key(argv[i], (idx-argv[i]));
				std::string val(idx+1);
				cout << "recognized property: " << key << "=" << val << endl;
				_props.insert(std::make_pair(key,val));
			}
			else {
				// some sort of indicator flag
				cout << "recognized flag: " << argv[i] << endl;
				_props.insert(std::make_pair(argv[i], ""));
			}
		}
	}

	template <typename T>
	T getProperty(const char *name, const T& default_val ) {
		std::map<std::string, std::string>::iterator i = _props.find(name);
		if (i != _props.end() ) {
			T retval( i->second );
			return retval;
		}
		else
			return default_val;
	}

	int getProperty(const char *name, int default_val) {
		std::map<std::string, std::string>::iterator i = _props.find(name);
		if (i != _props.end() ) {
			return atoi( i->second.c_str() );
		}
		else
			return default_val;

	}

	long getProperty(const char *name, long default_val) {
		std::map<std::string, std::string>::iterator i = _props.find(name);
		if (i != _props.end() ) {
			return atol( i->second.c_str() );
		}
		else
			return default_val;

	}

	double getProperty(const char *name, double default_val) {
		std::map<std::string, std::string>::iterator i = _props.find(name);
		if (i != _props.end() ) {
			double val;
			std::istringstream str(i->second);
			str >> val;
			return val;
		}
		else
			return default_val;

	}

	bool getProperty(const char *name, bool default_val) {
		std::map<std::string, std::string>::iterator i = _props.find(name);
		if (i != _props.end() ) {
			return ( i->second == "true" || i->second == "True" || i->second == "TRUE" );
		}
		else
			return default_val;
	}
private:
	// run-time configuration in the form of name/value pairs.
	std::map<std::string, std::string> _props;
};

// a global will be declared in main
extern properties_t properties;



#endif /* PROPERTIES_H_ */
