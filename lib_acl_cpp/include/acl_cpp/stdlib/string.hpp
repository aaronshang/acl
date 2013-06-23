#pragma once
#include "acl_cpp/acl_cpp_define.hpp"
#include <vector>
#include <list>
#include <utility>

struct ACL_VSTRING;

namespace acl {

class ACL_CPP_API string
{
public:
	string(size_t n = 64, bool bin = false);
	string(const string& s);
	string(const char* s);
	string(const void* s, size_t n);
	~string(void);

	void set_bin(bool bin);
	bool get_bin() const;
	char operator[](size_t);
	char operator[](int);
	string& operator=(const char*);
	string& operator=(const string&);
	string& operator=(const string*);
#ifdef WIN32
	string& operator=(__int64);
	string& operator=(unsigned __int64);
#else
	string& operator=(long long int);
	string& operator=(unsigned long long int);
#endif
	string& operator=(char);
	string& operator=(unsigned char);
	string& operator=(long);
	string& operator=(unsigned long);
	string& operator=(int);
	string& operator=(unsigned int);
	string& operator=(short);
	string& operator=(unsigned short);
	string& operator+=(const char*);
	string& operator+=(const string&);
	string& operator+=(const string*);
#ifdef WIN32
	string& operator+=(__int64);
	string& operator+=(unsigned __int64);
#else
	string& operator+=(long long int);
	string& operator+=(unsigned long long int);
#endif
	string& operator+=(long);
	string& operator+=(unsigned long);
	string& operator+=(int);
	string& operator+=(unsigned int);
	string& operator+=(short);
	string& operator+=(unsigned short);
	string& operator+=(char);
	string& operator+=(unsigned char);
	string& operator<<(const string&);
	string& operator<<(const string*);
	string& operator<<(const char*);
#ifdef WIN32
	string& operator<<(__int64);
	string& operator<<(unsigned __int64);
#else
	string& operator<<(long long int);
	string& operator<<(unsigned long long int);
#endif
	string& operator<<(long);
	string& operator<<(unsigned long);
	string& operator<<(int);
	string& operator<<(unsigned int);
	string& operator<<(short);
	string& operator<<(unsigned short);
	string& operator<<(char);
	string& operator<<(unsigned char);
	string& operator>>(string*);
#ifdef WIN32
	string& operator>>(__int64&);
	string& operator>>(unsigned __int64&);
#else
	string& operator>>(long long int&);
	string& operator>>(unsigned long long int&);
#endif
	string& operator>>(int&);
	string& operator>>(unsigned int&);
	string& operator>>(short&);
	string& operator>>(unsigned short&);
	string& operator>>(char&);
	string& operator>>(unsigned char&);
	bool operator==(const string&) const;
	bool operator==(const string*) const;
	bool operator==(const char*) const;
	bool operator!=(const string&) const;
	bool operator!=(const string*) const;
	bool operator!=(const char*) const;
	bool operator<(const string&) const;
	bool operator>(const string&) const;
	operator const char*() const;
	operator const void*() const;

	string& push_back(char ch);
	int compare(const string&) const;
	int compare(const string*) const;
	int compare(const char*, bool case_sensitive=true) const;
	int compare(const void* ptr, size_t len) const;
	int ncompare(const char*, size_t len, bool case_sensitive=true) const;
	int rncompare(const char*, size_t len, bool case_sensitive=true) const;

	int find(char) const;
	const char* find(const char* needle, bool case_sensitive=true) const;
	const char* rfind(const char* needle, bool case_sensitive=true) const;
	const string left(size_t npos);
	const string right(size_t npos);

	string& scan_buf(void* buf, size_t size);
	char* buf_end(void);
	void* buf() const;
	char* c_str() const;
	size_t length() const;
	size_t size() const;
	size_t capacity() const;
	bool empty() const;
	ACL_VSTRING* vstring(void) const;
	string& set_offset(size_t n);
	string& space(size_t n);

	const std::list<string>& split(const char*);
	const std::vector<string>& split2(const char*);
	const std::pair<string, string>& split_nameval(void);

	string& copy(const char* ptr);
	string& copy(const void* ptr, size_t len);
	string& memmove(const char* src);
	string& memmove(const char* src, size_t len);
	string& append(const string& s);
	string& append(const string* s);
	string& append(const char* ptr);
	string& append(const void* ptr, size_t len);
	string& prepend(const char* s);
	string& prepend(const void* ptr, size_t len);
	string& insert(size_t start, const void* ptr, size_t len);
	string& format(const char* fmt, ...);
	string& vformat(const char* fmt, va_list ap);
	string& format_append(const char* fmt, ...);
	string& vformat_append(const char* fmt, va_list ap);
	string& replace(char from, char to);
	string& truncate(size_t n);
	string& strip(const char* needle, bool each = false);
	string& clear();

	string& lower(void);
	string& upper(void);
	string& base64_encode(void);
	string& base64_encode(const void* ptr, size_t len);
	string& base64_decode(void);
	string& base64_decode(const char* s);
	string& base64_decode(const void* ptr, size_t len);
	string& url_encode(const char* s);
	string& url_decode(const char* s);
	string& hex_encode(const void* s, size_t len);
	string& hex_decode(const char* s, size_t len);

	static const string& parse_int(int);
	static const string& parse_int(unsigned int);
#ifdef WIN32
	static const string& parse_int64(__int64);
	static const string& parse_int64(unsigned __int64);
#else
	static const string& parse_int64(long long int);
	static const string& parse_int64(unsigned long long int);
#endif

private:
	bool m_bin;
	void init(size_t len);
	ACL_VSTRING* m_pVbf;
	const char* m_ptr;
	std::list<string>* m_psList;
	std::vector<string>* m_psList2;
	std::pair<string, string>* m_psPair;
};

} // namespce acl
