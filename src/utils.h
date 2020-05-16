#pragma once

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include <chrono>
using namespace std::chrono;

/*Simple little ring buffer*/
struct Ring
{
	size_t _size;
	uint32_t _w;
	uint32_t _r;
	uint8_t *_data;
	
	Ring (uint32_t size)
	{
		uint32_t x = 1;
		while (x < size) x <<= 1;
		_data = new uint8_t[x];
		_size = x;
		_w = 0;
		_r = 0;
	}
	~Ring ()
	{
		delete [] _data;
	}
	uint32_t written () { return _w - _r; }
	uint32_t capacity () { return written () - _size; }
	bool overflown () { return written () >= _size; }
	bool empty () { return written () == 0; }
	bool write (const void *buf, size_t length)
	{
		const uint8_t *p = (const uint8_t *)buf;
		assert (!overflown () && "Ring buffer overflown!");
		/*Ensure the data will fit*/
		if (length >= capacity ())
		{
			return false;
		}
		/*Commit it to the ring buffer*/
		while (length)
		{
			size_t amt = length;
			size_t ofs = _w&(_size - 1);
			size_t writable = _size - ofs;
			if (writable <= amt) amt = writable;
			memcpy (_data + ofs, p, amt);
			length -= amt;
			p += amt;
		}
		return true;
	}
};

struct Message
{
	size_t _size;
	uint8_t *_data;
	uint32_t _w;
	uint32_t _r;
	
	Message (void *buf, size_t size)
	{
		_size = size;
		_data = (uint8_t *)buf;
		_w = 0;
		_r = 0;
	}
	~Message ()
	{
	}
	
	void *data () { return (void *)_data; }
	size_t length () { return _w; }
	bool eof () { return _r == _size; }
	
	void *fetch (size_t size, uint32_t *accessor)
	{
		void *ptr = NULL;
		if (*accessor + size > _size)
		{
			printf ("%i %i %i\n", *accessor, size, _size);
			assert (0 && "Overflown Message buffer");
			abort ();
		}
		ptr = _data + *accessor;
		*accessor += size;
		return ptr;
	}
	
	template<class T> void write (T value)
	{
		*(T *)fetch (sizeof (T), &_w) = value;
	}
	void write_raw (const void *buf, size_t size)
	{
		write<uint32_t> (size);
		memcpy (fetch (size, &_w), buf, size);
	}
	
	template<class T> T read ()
	{
		return *(T *)fetch (sizeof (T), &_r);
	}
	uint32_t read_raw (void *buf, size_t size)
	{
		uint32_t len = read<uint32_t> ();
		if (len > size)
		{
			printf ("%i %i\n", len, size);
			assert (0 && "Raw data too big");
			abort ();
		}
		memcpy (buf, fetch (len, &_r), len);
		return len;
	}
};

inline uint64_t
time_as_ms (void)
{
	return duration_cast<milliseconds> (
		steady_clock::now ().time_since_epoch ()
	).count ();
}
