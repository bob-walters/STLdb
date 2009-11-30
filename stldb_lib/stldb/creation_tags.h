/*
 * creation_tags.h
 *
 *  Created on: Jan 16, 2009
 *      Author: bobw
 */

#ifndef CREATION_TAGS_H_
#define CREATION_TAGS_H_

namespace stldb {

//!Tag to indicate that the Database, when opened, must be
//!created, or recovered from disk.  No existing managed
//!shared memory segument is to be reused.
struct create_or_recover_t {};

//!Tag to indicate that the Database, when opened, can be
//!opened, created, or recovered, as needed.  With this tag,
//!the constructor attempts to reuse an existing region, if
//!one exists.  If a region exists but is not reusable, it
//!is recovered from disk.
struct open_create_or_recover_t {};

//!Tag to indicate that the Database, when opened, can be
//!opened or created, but that recovery is not allowed.
struct open_or_create_t {};

//!Tag to indicate that the Database, when opened, should
//!already exist.  It can be recovered is needed, but should
//!not need to be created.
struct open_or_recover_t {};

//!Tag to indicate that the Database, when opened, must already
//!exist, and that it must not require recovery of loading.
struct open_only_t {};

//!Value to indicate that the resource must
//!be only created
static const create_or_recover_t      create_or_recover    = create_or_recover_t();

//!Value to indicate that the resource must
//!be only opened
static const open_only_t              open_only      = open_only_t();

//!Value to indicate that the resource must
//!be only opened for reading
static const open_create_or_recover_t open_create_or_recover = open_create_or_recover_t();

//!Value to indicate that the resource must
//!be created. If already created, it must be opened.
static const open_or_create_t         open_or_create = open_or_create_t();

//!Value to indicate that the resource must
//!be only opened for reading
static const open_or_recover_t        open_or_recover = open_or_recover_t();


}  //namespace stldb

#endif /* CREATION_TAGS_H_ */
