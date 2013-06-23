#include "acl_stdafx.hpp"
#include "acl_cpp/stdlib/locker.hpp"
#include "acl_cpp/stdlib/log.hpp"
#include "acl_cpp/db/db_handle.hpp"
#include "acl_cpp/db/db_sqlite.hpp"
#include "acl_cpp/db/db_pool.hpp"

namespace acl
{

db_pool::db_pool(int dblimit /* = 64 */)
{
	if (dblimit > 0)
		dblimit_ = dblimit;
	else
		dblimit_ = 64;

	dbcount_ = 0;
	locker_ = NEW locker(true);
}

db_pool::~db_pool()
{
	std::list<db_handle*>::iterator it = pool_.begin();
	for (; it != pool_.end(); ++it)
		delete (*it);
	pool_.clear();
	delete locker_;
}

void db_pool::set_id()
{
	struct timeval tv;

	(void) gettimeofday(&tv, NULL);
	snprintf(id_, sizeof(id_), "%d.%d.%d",
		(int) tv.tv_sec, (int) tv.tv_usec, rand());
}

db_handle* db_pool::peek()
{
	db_handle* conn;

	locker_->lock();

	std::list<db_handle*>::iterator it = pool_.begin();
	if (it != pool_.end())
	{
		conn = *it;
		pool_.erase(it);
		locker_->unlock();
		return conn;
	}
	else if (dbcount_ >= dblimit_)
	{
		logger_error("dbcount(%d) > dblimit(%d)",
			dbcount_, dblimit_);
		locker_->unlock();
		return NULL;
	}

	// 记数器加 1
	dbcount_++;
	locker_->unlock(); // 尽量减少锁的锁定时间

	// 创建 mysql 或 sqlite 连接句柄
	conn = create();

	conn->set_id(id_);
	return conn;
}

void db_pool::put(db_handle* conn, bool keep /* = true */)
{
	acl_assert(conn);

	const char* id = conn->get_id();
	bool eq;
	if (id && strcmp(id, id_) == 0)
		eq = true;
	else
		eq = false;

	locker_->lock();

	if (keep)
	{
		pool_.push_back(conn);
		// 如果该连接句柄不是由本连接池产生的，则需要
		// 重新设置连接句柄的 ID 标识
		if (!eq)
			conn->set_id(id_);
	}
	else
	{
		// 只有当该连接句柄由本连接池产生时，才需要
		// 减少记数器
		if (eq)
			dbcount_--;
		delete conn;
	}
	locker_->unlock();
}

} // namespace acl
