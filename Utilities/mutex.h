#pragma once

#include <mutex>
#include "types.h"
#include "Atomic.h"

// Shared mutex.
class shared_mutex final
{
	enum : s64
	{
		c_one = 1ull << 31, // Fixed-point 1.0 value (one writer)
		c_min = 0x00000001, // Fixed-point 1.0/max_readers value
		c_sig = 1ull << 62,
		c_max = c_one
	};

	atomic_t<s64> m_value{c_one}; // Semaphore-alike counter

	void imp_lock_shared(s64 _old);
	void imp_unlock_shared(s64 _old);
	void imp_wait(s64 _old);
	void imp_lock(s64 _old);
	void imp_unlock(s64 _old);

	void imp_lock_upgrade();
	void imp_lock_degrade();

public:
	constexpr shared_mutex() = default;

	bool try_lock_shared();

	void lock_shared()
	{
		const s64 value = m_value.load();

		// Fast path: decrement if positive
		if (UNLIKELY(value < c_min || value > c_one || !m_value.compare_and_swap_test(value, value - c_min)))
		{
			imp_lock_shared(value);
		}
	}

	void unlock_shared()
	{
		// Unconditional increment
		const s64 value = m_value.fetch_add(c_min);

		if (value < 0 || value > c_one - c_min)
		{
			imp_unlock_shared(value);
		}
	}

	bool try_lock();

	void lock()
	{
		// Try to lock
		const s64 value = m_value.compare_and_swap(c_one, 0);

		if (value != c_one)
		{
			imp_lock(value);
		}
	}

	void unlock()
	{
		// Unconditional increment
		const s64 value = m_value.fetch_add(c_one);

		if (value != 0)
		{
			imp_unlock(value);
		}
	}

	bool try_lock_upgrade();

	void lock_upgrade()
	{
		if (!m_value.compare_and_swap_test(c_one - c_min, 0))
		{
			imp_lock_upgrade();
		}
	}

	bool try_lock_degrade();

	void lock_degrade()
	{
		if (!m_value.compare_and_swap_test(0, c_one - c_min))
		{
			imp_lock_degrade();
		}
	}

	bool is_reading() const
	{
		return (m_value.load() % c_one) != 0;
	}

	bool is_lockable() const
	{
		return m_value.load() >= c_min;
	}
};

// Simplified shared (reader) lock implementation.
class reader_lock final
{
	shared_mutex& m_mutex;
	bool m_upgraded = false;

public:
	reader_lock(const reader_lock&) = delete;

	explicit reader_lock(shared_mutex& mutex)
		: m_mutex(mutex)
	{
		m_mutex.lock_shared();
	}

	// One-way lock upgrade
	void upgrade()
	{
		if (!m_upgraded)
		{
			m_mutex.lock_upgrade();
			m_upgraded = true;
		}
	}

	~reader_lock()
	{
		m_upgraded ? m_mutex.unlock() : m_mutex.unlock_shared();
	}
};
