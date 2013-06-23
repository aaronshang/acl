#include "acl_stdafx.hpp"
#include "acl_cpp/stdlib/log.hpp"
#include "acl_cpp/mime/rfc2047.hpp"
#include "acl_cpp/memcache/mem_cache.hpp"
#include "acl_cpp/stdlib/util.hpp"
#include "acl_cpp/stream/socket_stream.hpp"

#define	SPECIAL_CHAR(x)	((x) == ' ' || (x) == '\t' || (x) == '\r' || (x) == '\n')

namespace acl
{

	mem_cache::mem_cache(const char* key_pre /* = NULL */,
		const char* addr /* = "127.0.0.1:11211" */, bool retry /* = true */,
		int conn_timeout /* = 180 */, int rw_timeout /* = 300 */,
		bool encode_key /* = true */)
	: m_coder(false, false)
	, m_conn_timeout(conn_timeout)
	, m_rw_timeout(rw_timeout)
	, m_encode_key(encode_key)	
	, m_opened(false)
	, m_retry(retry)
	, m_conn(NULL)
	{
		if (key_pre && *key_pre)
		{
			bool beCoding = false;

			m_key_pre = NEW acl::string(strlen(key_pre));
			while (*key_pre)
			{
				if (SPECIAL_CHAR(*key_pre) || !ACL_ISPRINT(*key_pre))
				{
					m_coder.encode_update(key_pre, 1, m_key_pre);
					beCoding = true;
				}
				else if (beCoding)
				{
					m_coder.encode_finish(m_key_pre);
					m_coder.reset();
					beCoding = false;
					*m_key_pre << (char) *key_pre;
				}
				else
					*m_key_pre << (char) *key_pre;
				key_pre++;
			}
			if (beCoding)
				m_coder.encode_finish(m_key_pre);
		}
		else
			m_key_pre = NULL;

		acl_assert(addr && *addr);
		m_addr = acl_mystrdup(addr);
		char* ptr = strchr(m_addr, ':');
		if (ptr == NULL)
			logger_fatal("addr(%s) invalid", addr);
		*ptr++ = 0;
		if (*ptr == 0)
			logger_fatal("addr(%s) invalid", addr);
		m_ip = m_addr;
		m_port = atoi(ptr);
		if (m_port <= 0)
			logger_fatal("addr(%s) invalid", addr);
	}

	mem_cache::~mem_cache()
	{
		close();
		if (m_key_pre)
			delete m_key_pre;
		acl_myfree(m_addr);
	}

	void mem_cache::close()
	{
		if (m_opened == false)
			return;

		if (m_conn)
		{
			delete m_conn;
			m_conn = NULL;
		}
		m_opened = false;
	}

	bool mem_cache::open()
	{
		if (m_opened)
			return (true);

		m_conn = NEW socket_stream();
		char  addr[64];

		snprintf(addr, sizeof(addr), "%s:%d", m_ip, m_port);
		if (m_conn->open(addr, m_conn_timeout, m_rw_timeout) == false)
		{
			logger_error("connect %s error(%s)",
				addr, acl::last_serror());
			delete m_conn;
			m_conn = NULL;
			m_opened = false;
			m_ebuf.format("connect server(%s) error(%s)",
				addr, acl_last_serror());
			return (false);
		}
		m_opened = true;
		return (true);
	}

	bool mem_cache::set(const acl::string& key, const void* dat, size_t dlen,
		time_t timeout, unsigned short flags)
	{
		bool has_tried = false;
		struct iovec v[4];
		m_line.format("set %s %u %d %d\r\n", key.c_str(),
			flags, (int) timeout, (int) dlen);
AGAIN:
		if (open() == false)
			return (false);

		v[0].iov_base = (void*) m_line.c_str();
		v[0].iov_len = m_line.length();
		v[1].iov_base = (void*) dat;
		v[1].iov_len = dlen;
		v[2].iov_base = (void*) "\r\n";
		v[2].iov_len = 2;

		if (m_conn->writev(v, 3) < 0)
		{
			close();
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("write set(%s) error", key.c_str());
			return (false);
		}

		if (m_conn->gets(m_line) == false)
		{
			close();
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("reply for set(%s) error", key.c_str());
			return (false);
		}

		if (m_line != "STORED")
		{
			close();
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("reply(%s) for set(%s) error",
				m_line.c_str(), key.c_str());
			return (false);
		}
		return (true);
	}

	bool mem_cache::set(const char* key, size_t klen, const void* dat,
		size_t dlen, time_t timeout /* = 0 */, unsigned short flags /* = 0 */)
	{
		const acl::string& keybuf = get_key(key, klen);
		return (set(keybuf, dat, dlen, timeout, flags));
	}

	bool mem_cache::set(const char* key, const void* dat, size_t dlen,
		time_t timeout /* = 0 */, unsigned short flags /* = 0 */)
	{
		return (set(key, strlen(key), dat, dlen, timeout, flags));
	}

	bool mem_cache::set(const char* key, size_t klen, time_t timeout /* = 0 */)
	{
		const acl::string& keybuf = get_key(key, klen);
		acl::string buf;
		unsigned short flags;

		if (get(keybuf, buf, &flags) == false)
			return (false);
		return (set(keybuf, buf.c_str(), buf.length(), timeout, flags));
	}

	bool mem_cache::set(const char* key, time_t timeout /* = 0 */)
	{
		return (set(key, strlen(key), timeout));
	}

	bool mem_cache::get(const acl::string& key, acl::string& buf, unsigned short* flags)
	{
		bool has_tried = false;
		buf.clear();

		m_line.format("get %s\r\n", key.c_str());

	AGAIN:
		if (open() == false)
			return (false);
		if (m_conn->write(m_line) < 0)
		{
			close();
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("write get(%s) error", key.c_str());
			return (false);
		}

		// 读取服务器响应行
		if (m_conn->gets(m_line) == false)
		{
			close();
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("reply for get(%s) error", key.c_str());
			return (false);
		}
		else if (m_line == "END")
		{
			m_ebuf.format("not found");
			return (false);
		}
		else if (error_happen(m_line.c_str()))
		{
			close();
			return (false);
		}

		// VALUE {key} {flags} {bytes}\r\n
		ACL_ARGV* tokens = acl_argv_split(m_line.c_str(), " \t");
		if (tokens->argc < 4 || strcasecmp(tokens->argv[0], "VALUE") != 0)
		{
			close();
			m_ebuf.format("server error for get(%s)", key.c_str());
			acl_argv_free(tokens);
			return (false);
		}
		if (flags)
			*flags = (unsigned short) atoi(tokens->argv[2]);

		int len = atoi(tokens->argv[3]);
		if (len < 0)
		{
			close();
			m_ebuf.format("value's len < 0");
			acl_argv_free(tokens);
			return (false);
		}
		else if (len == 0)
		{
			acl_argv_free(tokens);
			return (true);
		}
		acl_argv_free(tokens);

		// 得需要保证足够的空间能容纳读取的数据，该种方式
		// 可能会造成数据量非常大时的缓冲区溢出！

		char  tmp[4096];
		int   n;
		while (true)
		{
			n = sizeof(tmp);
			if (n > len)
				n = len;
			if ((n = m_conn->read(tmp, n, false)) < 0)
			{
				close();
				m_ebuf.format("read data for get cmd error");
				return (false);
			}
			buf.append(tmp, n);
			len -= n;
			if (len <= 0)
				break;
		}

		// 读取数据尾部的 "\r\n"
		if (m_conn->gets(m_line) == false)
		{
			close();
			m_ebuf.format("read data's delimiter error");
			return (false);
		}

		// 读取 "END\r\n"
		if (m_conn->gets(m_line) == false || m_line != "END")
		{
			close();
			m_ebuf.format("END flag not found");
			return (false);
		}
		return (true);
	}

	bool mem_cache::get(const char* key, size_t klen, acl::string& buf,
		unsigned short* flags /* = NULL */)
	{
		const acl::string& keybuf = get_key(key, klen);
		return (get(keybuf, buf, flags));
	}

	bool mem_cache::get(const char* key, acl::string& buf,
		unsigned short* flags /* = NULL */)
	{
		return (get(key, strlen(key), buf, flags));
	}

	bool mem_cache::del(const char* key, size_t klen)
	{
		bool has_tried = false;
		const acl::string& keybuf = get_key(key, klen);

	AGAIN:
		if (open() == false)
			return (false);

		m_line.format("delete %s\r\n", keybuf.c_str());
		if (m_conn->write(m_line) < 0)
		{
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("write (%s) error", m_line.c_str());
			return (false);
		}
		// DELETED|NOT_FOUND\r\n
		if (m_conn->gets(m_line) == false)
		{
			if (m_retry && !has_tried)
			{
				has_tried = true;
				goto AGAIN;
			}
			m_ebuf.format("reply for(%s) error", m_line.c_str());
			return (false);
		}
		if (m_line != "DELETED" && m_line != "NOT_FOUND")
		{
			m_ebuf.format("reply for (%s) error", m_line.c_str());
			return (false);
		}
		return (true);
	}

	bool mem_cache::del(const char* key)
	{
		return (del(key, strlen(key)));
	}

	const char* mem_cache::last_serror() const
	{
		static const char* dummy = "ok";

		if (m_ebuf.empty())
			return (dummy);
		return (m_ebuf.c_str());
	}

	int mem_cache::last_error() const
	{
		return (m_enum);
	}

	const acl::string& mem_cache::get_key(const char* key, size_t klen)
	{
		m_kbuf.clear();
		if (m_key_pre)
			m_kbuf.format("%s:", m_key_pre->c_str());

		m_coder.reset();

		if (m_encode_key)
		{
			m_coder.encode_update(key, klen, &m_kbuf);
			m_coder.encode_finish(&m_kbuf);
			return (m_kbuf);
		}

		bool beCoding = false;

		while (klen > 0)
		{
			if (SPECIAL_CHAR(*key) || !ACL_ISPRINT(*key))
			{
				m_coder.encode_update(key, 1, &m_kbuf);
				beCoding = true;
			}
			else if (beCoding)
			{
				m_coder.encode_finish(&m_kbuf);
				m_coder.reset();
				beCoding = false;
				m_kbuf << (char) *key;
			}
			else
				m_kbuf << (char) *key;
			key++;
			klen--;
		}

		if (beCoding)
			m_coder.encode_finish(&m_kbuf);

		return (m_kbuf);
	}

	bool mem_cache::error_happen(const char* line)
	{
		if (strcasecmp(line, "ERROR") == 0)
			return (true);
		if (strncasecmp(line, "CLIENT_ERROR", sizeof("CLIENT_ERROR") - 1) == 0)
		{
			m_ebuf.format("%s", line);
			const char* ptr = line + sizeof("CLIENT_ERROR") - 1;
			if (*ptr == ' ' || *ptr == '\t')
				ptr++;
			m_enum = atoi(ptr);
			return (true);
		}
		if (strncasecmp(line, "SERVER_ERROR", sizeof("SERVER_ERROR") - 1) == 0)
		{
			m_ebuf.format("%s", line);
			const char* ptr = line + sizeof("SERVER_ERROR") - 1;
			if (*ptr == ' ' || *ptr == '\t')
				ptr++;
			m_enum = atoi(ptr);
			return (true);
		}
		return (false);
	}

	void mem_cache::property_list()
	{
	}

} // namespace acl
