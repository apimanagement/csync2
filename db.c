/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005, 2006  Clifford Wolf <clifford@clifford.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csync2.h"
#include <sqlite.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define DEADLOCK_MESSAGE \
	"Database backend is exceedingly busy => Terminating (requesting retry).\n"

int db_blocking_mode = 1;
int db_sync_mode = 1;

static sqlite *db = 0;
static int dblock_timeout = 100;

static int tqueries_counter = -50;
static time_t transaction_begin = 0;
static time_t last_wait_cycle = 0;
static int begin_commit_recursion = 0;
static int in_sql_query = 0;
static int pause_requested = 0;

void gen_dblock_timeout()
{
	dblock_timeout=((getpid() % 7 + 12) * 10);
}

void csync_db_alarmhandler(int signum)
{
	if ( in_sql_query || begin_commit_recursion )
		alarm(2);

	if (tqueries_counter <= 0)
		return;

	begin_commit_recursion++;

	csync_debug(2, "Database idle in transaction. Forcing COMMIT.\n");
	SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
	tqueries_counter = -10;

	begin_commit_recursion--;
}

void csync_db_maybegin()
{
	if ( !db_blocking_mode || begin_commit_recursion ) return;
	begin_commit_recursion++;

	signal(SIGALRM, SIG_IGN);
	alarm(0);

	tqueries_counter++;
	if (tqueries_counter <= 0) {
		begin_commit_recursion--;
		return;
	}

	if (tqueries_counter == 1) {
		transaction_begin = time(0);
		if (!last_wait_cycle)
			last_wait_cycle = transaction_begin;
		SQL("BEGIN TRANSACTION", "BEGIN TRANSACTION");
	}

	begin_commit_recursion--;
}

void csync_db_continue() {
	pause_requested=0;
}

void csync_db_pause() {
	pause_requested=1;
}

void csync_db_maycommit()
{
	time_t now;
	int remaining;
	
	//csync_debug(0, "pause_requested=%d tqueries_counter=%d begin_commit_recursion=%d time(0)-last_wait_cycle=%d\n",pause_requested,tqueries_counter,begin_commit_recursion,time(0)-last_wait_cycle);
	
	if ( !db_blocking_mode || begin_commit_recursion ) return;
	begin_commit_recursion++;

	if (tqueries_counter <= 0) {
		begin_commit_recursion--;
		return;
	}

	now = time(0);

	if (pause_requested/* || (now - last_wait_cycle) > 10*/) {
		pause_requested=0;
		SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
		csync_debug(2, "Waiting 5 secs so others can lock the database (%d - %d)...\n", (int)now, (int)last_wait_cycle);
		remaining = sleep(5);
		csync_debug(2, "Woke up. Remaning time: %d\n",remaining);
		last_wait_cycle = 0;
		tqueries_counter = -10;
		begin_commit_recursion--;
		return;
	}

	if ((tqueries_counter > 1000) || ((now - transaction_begin) > 3)) {
		SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
		tqueries_counter = 0;
		begin_commit_recursion--;
		return;
	}

	signal(SIGALRM, csync_db_alarmhandler);
	alarm(10);

	begin_commit_recursion--;
	return;
}

void csync_db_open(const char *file)
{
	db = sqlite_open(file, 0, 0);
	if ( db == 0 ) {
		csync_debug(0,"Can't open database: %s. Deleting and trying again.\n", file);
		int unlink_rc = unlink(file);
		if (!unlink_rc) {
			csync_fatal("ERROR: %i. Can't delete corrupt database: %s\n", unlink_rc, file);
		}
		db = sqlite_open(file, 0, 0);			
		csync_fatal("Can't open database: %s\n", file);
	}

	/* ignore errors on table creation */
	in_sql_query++;
	sqlite_exec(db,
		"CREATE TABLE file ("
		"	filename, checktxt,"
		"	UNIQUE ( filename ) ON CONFLICT REPLACE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE dirty ("
		"	filename, force, myname, peername,"
		"	UNIQUE ( filename, peername ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE hint ("
		"	filename, recursive,"
		"	UNIQUE ( filename, recursive ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE action ("
		"	filename, command, logfile,"
		"	UNIQUE ( filename, command ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	sqlite_exec(db,
		"CREATE TABLE x509_cert ("
		"	peername, certdata,"
		"	UNIQUE ( peername ) ON CONFLICT IGNORE"
		")",
		0, 0, 0);
	if (!db_sync_mode)
		sqlite_exec(db, "PRAGMA synchronous = OFF", 0, 0, 0);
	in_sql_query--;
}

void csync_db_close()
{
	if (!db || begin_commit_recursion) return;

	begin_commit_recursion++;
	if (tqueries_counter > 0) {
		SQL("COMMIT TRANSACTION", "COMMIT TRANSACTION");
		tqueries_counter = -10;
	}
	sqlite_close(db);
	begin_commit_recursion--;
	db = 0;
}

void csync_db_sql(const char *err, const char *fmt, ...)
{
	char *sql;
	va_list ap;
	int rc, busyc = 0;
	struct timespec requested_time, remaining_time;
	requested_time.tv_sec = 0;
	requested_time.tv_nsec = 100000000;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	in_sql_query++;
	csync_db_maybegin();

	csync_debug(3, "SQL: %s: %s\n", err, sql);

	while (1) {
		rc = sqlite_exec(db, sql, 0, 0, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > dblock_timeout) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a tenth of a sec.\n");
		nanosleep(&requested_time,&remaining_time);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d]: %s\n", err, rc, sql);
	free(sql);

	csync_db_maycommit();
	in_sql_query--;
}

void* csync_db_begin(const char *err, const char *fmt, ...)
{
	sqlite_vm *vm;
	char *sql;
	va_list ap;
	int rc, busyc = 0;
	struct timespec requested_time, remaining_time;
	requested_time.tv_sec = 0;
	requested_time.tv_nsec = 100000000;

	va_start(ap, fmt);
	vasprintf(&sql, fmt, ap);
	va_end(ap);

	in_sql_query++;
	csync_db_maybegin();

	csync_debug(3, "SQL: %s\n", sql);

	while (1) {
		rc = sqlite_compile(db, sql, 0, &vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > dblock_timeout) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a tenth of a sec.\n");
		nanosleep(&requested_time,&remaining_time);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d]: %s\n", err, rc, sql);
	free(sql);

	return vm;
}

int csync_db_next(void *vmx, const char *err,
		int *pN, const char ***pazValue, const char ***pazColName)
{
	sqlite_vm *vm = vmx;
	int rc, busyc = 0;
	struct timespec requested_time, remaining_time;
	requested_time.tv_sec = 0;
	requested_time.tv_nsec = 100000000;

	csync_debug(4, "Trying to fetch a row from the database.\n");

	while (1) {
		rc = sqlite_step(vm, pN, pazValue, pazColName);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > dblock_timeout) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a tenth of a sec.\n");
		nanosleep(&requested_time,&remaining_time);
	}

	if ( rc != SQLITE_OK && rc != SQLITE_ROW &&
	     rc != SQLITE_DONE && err )
		csync_fatal("Database Error: %s [%d].\n", err, rc);

	return rc == SQLITE_ROW;
}

void csync_db_fin(void *vmx, const char *err)
{
	sqlite_vm *vm = vmx;
	int rc, busyc = 0;
	struct timespec requested_time, remaining_time;
	requested_time.tv_sec = 0;
	requested_time.tv_nsec = 100000000;

	csync_debug(3, "SQL Query finished.\n");

	while (1) {
		rc = sqlite_finalize(vm, 0);
		if ( rc != SQLITE_BUSY ) break;
		if (busyc++ > dblock_timeout) { db = 0; csync_fatal(DEADLOCK_MESSAGE); }
		csync_debug(2, "Database is busy, sleeping a tenth of a sec.\n");
		nanosleep(&requested_time,&remaining_time);
	}

	if ( rc != SQLITE_OK && err )
		csync_fatal("Database Error: %s [%d].\n", err, rc);

	csync_db_maycommit();
	in_sql_query--;
}

