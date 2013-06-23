#pragma once
#include "acl_cpp/acl_cpp_define.hpp"
#include <list>

namespace acl {

class db_handle;
class locker;

class db_pool
{
public:
	/**
	 * 数据库构造函数
	 * @param dblimit {int} 数据库连接池的最大连接数限制
	 */
	db_pool(int dblimit = 64);
	virtual ~db_pool();

	/**
	 * 从数据库中连接池获得一个数据库连接，该函数返回的数据库
	 * 连接对象用完后必须调用 db_pool->put(db_handle*) 将连接
	 * 归还至数据库连接池，由该函数获得的连接句柄不能 delete，
	 * 否则会造成连接池的内部计数器出错
	 * @return {db_handle*} 返回空，则表示出错
	 */
	db_handle* peek();

	/**
	 * 将数据库连接放回至连接池中，当从数据库连接池中获得连接
	 * 句柄用完后应该通过该函数放回，不能直接 delete，因为那样
	 * 会导致连接池的内部记数发生错误
	 * @param conn {db_handle*} 数据库连接句柄，该连接句柄可以
	 *  是由 peek 创建的，也可以单独动态创建的
	 * @param keep {bool} 归还给连接池的数据库连接句柄是否继续
	 *  保持连接，如果否，则内部会自动删除该连接句柄
	 */
	void put(db_handle* conn, bool keep = true);
protected:
	/**
	 * 纯虚函数：创建 DB 的方法
	 * @return {db_handle*}
	 */
	virtual db_handle* create() = 0;
private:
	std::list<db_handle*> pool_;
	int   dblimit_;  // 连接池的最大连接数限制
	int   dbcount_;  // 当前已经打开的连接数
	locker* locker_;
	char  id_[128];  // 本类实例的唯一 ID 标识

	// 设置本实例的唯一 ID 标识
	void set_id();
};

} // namespace acl
