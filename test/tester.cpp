/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2025-2026, Michael VanLoon
 * All rights reserved.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>

#include "mlvlib/helper.hpp"
#include "mlvlib/lock_traits.hpp"
#include "mlvlib/spinlock.hpp"
#include "mlvlib/null_lock.hpp"
#include "mlvlib/lifetimeobserver.hpp"

using namespace std::literals;
using namespace mlvlib;

TEST(SpinLockTest, BasicLocking)
{
    int counter = 0;

    SpinLock lock;

    ++counter;
    lock.lock();
    // critical section
    EXPECT_EQ(++counter, 2);
    lock.unlock();

    // try_lock should succeed when not locked
    EXPECT_EQ(++counter, 3);
    lock.try_lock();
    // critical section
    EXPECT_EQ(++counter, 4);
    lock.unlock();

    // lock again
    EXPECT_EQ(++counter, 5);
    lock.lock();
    // critical section
    EXPECT_EQ(++counter, 6);        
    lock.unlock();

    EXPECT_EQ(++counter, 7);
}


//
// NOTE: This test may occasionally fail due to timing issues. These are
// clearly intentional race conditions with highly probabilistic outcomes.
//
TEST(SpinLockTest, Backoff)
{
    uint32_t tested_value = 0x55555555;

    SpinLock lock;

    lock.lock();

    // Start a thread that will attempt to acquire the lock
    std::thread t([&lock, &tested_value]() {
        lock.lock();
        // critical section
        EXPECT_EQ(tested_value, 0xAAAAAAAAu);
        tested_value ^= 0xFFFFFFFFu;
        EXPECT_EQ(tested_value, 0x55555555u);
        lock.unlock();
    });

    EXPECT_EQ(tested_value, 0x55555555u);
    tested_value ^= 0xFFFFFFFFu;
    EXPECT_EQ(tested_value, 0xAAAAAAAAu);

    // Sleep for a short duration to ensure the other thread attempts to lock
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(tested_value, 0xAAAAAAAAu);

    // Unlock the main thread's lock
    lock.unlock();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    lock.lock();
    EXPECT_EQ(tested_value, 0x55555555u);
    tested_value ^= 0xFFFFFFFFu;
    EXPECT_EQ(tested_value, 0xAAAAAAAAu);
    lock.unlock();

    // Wait for the other thread to finish
    t.join();
    EXPECT_EQ(tested_value, 0xAAAAAAAAu);
}


TEST(SpinLockTest, TryLockContention)
{
    int tested_value = 0;

    SpinLock lock;

    lock.lock();

    // Start a thread that will attempt to acquire the lock using try_lock
    std::thread t([&lock, &tested_value]() {
        while (!lock.try_lock())
        {
            // failed to acquire lock, yield and try again
            std::this_thread::yield();
        }
        // critical section
        tested_value = 42;
        lock.unlock();
    });

    // Sleep for a short duration to ensure the other thread attempts to lock
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(tested_value, 0);

    // Unlock the main thread's lock
    lock.unlock();

    // Wait for the other thread to finish
    t.join();

    EXPECT_EQ(tested_value, 42);
}


TEST(SpinLockTest, LockContention)
{
    int tested_value = 0;

    SpinLock lock;

    lock.lock();

    // Start a thread that will attempt to acquire the lock
    std::thread t([&lock, &tested_value]() {
        lock.lock();
        // critical section
        tested_value = 99;
        lock.unlock();
    });

    // Sleep for a short duration to ensure the other thread attempts to lock
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(tested_value, 0);

    // Unlock the main thread's lock
    lock.unlock();

    // Wait for the other thread to finish
    t.join();

    EXPECT_EQ(tested_value, 99);
}


TEST(SpinLockTest, MultipleThreads)
{
    constexpr int num_threads = 10;
    constexpr int increments_per_thread = 1000;

    int counter = 0;
    SpinLock lock;

    auto worker = [&counter, &lock]() {
        for (int i = 0; i < increments_per_thread; ++i)
        {
            lock.lock();
            ++counter;
            lock.unlock();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(num_threads));
    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(counter, num_threads * increments_per_thread);
}


//
// Use RAII locking with std::scoped_lock and std::unique_lock
//
TEST(SpinLockTest, stdLock)
{
    int counter = 0;
    SpinLock lock;

    {
        std::scoped_lock<SpinLock> slock(lock);
        ++counter;
        EXPECT_EQ(counter, 1);
    } // slock goes out of scope and unlocks

    {
        std::scoped_lock<SpinLock> slock(lock);
        ++counter;
        EXPECT_EQ(counter, 2);
    } // slock goes out of scope and unlocks

    EXPECT_EQ(counter, 2);

    // with thread, starting unlocked
    {
        counter = 0;
        std::thread t([&counter, &lock]() {
            std::scoped_lock<SpinLock> slock(lock);
            ++counter;
            EXPECT_EQ(counter, 1);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        {
            std::scoped_lock<SpinLock> slock(lock);
            EXPECT_EQ(counter, 1);
            ++counter;
            EXPECT_EQ(counter, 2);
        } // slock goes out of scope and unlocks

        t.join();
        EXPECT_EQ(counter, 2);
    }

    // with thread, starting locked
    {
        counter = 0;
        std::unique_lock<SpinLock> ulock(lock); // main thread locks

        std::thread t([&counter, &lock]() {
            std::unique_lock<SpinLock> ulock(lock);
            ++counter;
            EXPECT_EQ(counter, 1);
        });

        EXPECT_EQ(counter, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        EXPECT_EQ(counter, 0);

        ulock.unlock(); // main thread unlocks
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        {
            std::unique_lock<SpinLock> ulock(lock);
            EXPECT_EQ(counter, 1);
            ++counter;
            EXPECT_EQ(counter, 2);
        } // ulock goes out of scope and unlocks

        t.join();
        EXPECT_EQ(counter, 2);
    }
}


#ifndef NDEBUG
// Owner tracking is debug-only; release builds omit these asserts.
TEST(SpinLockTest, UnlockWithoutHoldAborts)
{
    SpinLock lock;
    EXPECT_DEATH(lock.unlock(), "SpinLock");
}


TEST(SpinLockTest, UnlockByNonOwnerAborts)
{
    EXPECT_DEATH(
        {
            SpinLock lock;
            lock.lock();
            std::thread thief([&lock]() { lock.unlock(); });
            thief.join();
        },
        "SpinLock");
}


TEST(SpinLockTest, DebugIntrospection)
{
    SpinLock lock;
    EXPECT_FALSE(lock.debug_is_locked());
    EXPECT_FALSE(lock.debug_is_owned_by_current_thread());
    EXPECT_EQ(lock_traits<SpinLock>::debug_is_locked(lock), std::optional<bool>(false));
    EXPECT_EQ(lock_traits<SpinLock>::debug_is_owned_by_current_thread(lock),
              std::optional<bool>(false));

    lock.lock();
    EXPECT_TRUE(lock.debug_is_locked());
    EXPECT_TRUE(lock.debug_is_owned_by_current_thread());
    EXPECT_EQ(lock_traits<SpinLock>::debug_is_locked(lock), std::optional<bool>(true));
    EXPECT_EQ(lock_traits<SpinLock>::debug_is_owned_by_current_thread(lock),
              std::optional<bool>(true));
    assert_lock_held(lock);

    // Concurrent read from a non-owner thread must observe locked=true, owned=false,
    // and remain race-free under ThreadSanitizer.
    std::atomic<bool> observer_saw_locked{false};
    std::atomic<bool> observer_saw_owned{true};
    std::thread observer([&]() {
        observer_saw_locked.store(lock.debug_is_locked(), std::memory_order_relaxed);
        observer_saw_owned.store(lock.debug_is_owned_by_current_thread(),
                                 std::memory_order_relaxed);
    });
    observer.join();
    EXPECT_TRUE(observer_saw_locked.load());
    EXPECT_FALSE(observer_saw_owned.load());

    lock.unlock();
    EXPECT_FALSE(lock.debug_is_locked());
    EXPECT_FALSE(lock.debug_is_owned_by_current_thread());
}


TEST(SpinLockTest, AssertLockHeldAbortsWhenUnlocked)
{
    SpinLock lock;
    EXPECT_DEATH(assert_lock_held(lock), "lock must be held");
}
#endif // NDEBUG


// ---------------------------------------------------------------------------
// NullLock & lock_traits tests
// ---------------------------------------------------------------------------

namespace
{
struct CustomBasicLock
{
    void lock() {}
    void unlock() {}
};

struct NonLock
{
    void lock() {}
};
} // namespace

TEST(LockTraitsTest, ConceptsAndProperties)
{
    static_assert(BasicLockable<SpinLock>);
    static_assert(Lockable<SpinLock>);
    static_assert(BasicLockable<NullLock>);
    static_assert(Lockable<NullLock>);
    static_assert(BasicLockable<std::mutex>);
    static_assert(Lockable<std::mutex>);
    static_assert(BasicLockable<CustomBasicLock>);
    static_assert(!Lockable<CustomBasicLock>);
    static_assert(!BasicLockable<NonLock>);

    // SpinLock traits
    static_assert(std::is_same_v<lock_traits<SpinLock>::lock_type, SpinLock>);
    static_assert(std::is_same_v<lock_traits<SpinLock>::guard_type, std::scoped_lock<SpinLock>>);
    static_assert(lock_traits<SpinLock>::is_thread_safe);
    static_assert(!lock_traits<SpinLock>::is_noop);
    static_assert(lock_traits<SpinLock>::supports_try_lock);
    static_assert(lock_traits<SpinLock>::name == "spinlock");

    // std::mutex traits
    static_assert(std::is_same_v<lock_traits<std::mutex>::lock_type, std::mutex>);
    static_assert(
        std::is_same_v<lock_traits<std::mutex>::guard_type, std::scoped_lock<std::mutex>>);
    static_assert(lock_traits<std::mutex>::is_thread_safe);
    static_assert(!lock_traits<std::mutex>::is_noop);
    static_assert(lock_traits<std::mutex>::supports_try_lock);
    static_assert(!lock_traits<std::mutex>::has_debug_introspection);
    static_assert(lock_traits<std::mutex>::name == "std::mutex");

    // NullLock traits
    static_assert(std::is_same_v<lock_traits<NullLock>::lock_type, NullLock>);
    static_assert(std::is_same_v<lock_traits<NullLock>::guard_type, std::scoped_lock<NullLock>>);
    static_assert(!lock_traits<NullLock>::is_thread_safe);
    static_assert(lock_traits<NullLock>::is_noop);
    static_assert(lock_traits<NullLock>::supports_try_lock);
    static_assert(lock_traits<NullLock>::name == "null");

    // Third-party BasicLockable primary template fallback
    static_assert(lock_traits<CustomBasicLock>::is_thread_safe);
    static_assert(!lock_traits<CustomBasicLock>::is_noop);
    static_assert(!lock_traits<CustomBasicLock>::supports_try_lock);
    static_assert(!lock_traits<CustomBasicLock>::has_debug_introspection);
    static_assert(lock_traits<CustomBasicLock>::name == "custom");

#ifndef NDEBUG
    static_assert(DebugIntrospectableLock<SpinLock>);
    static_assert(DebugIntrospectableLock<NullLock>);
    static_assert(lock_traits<SpinLock>::has_debug_introspection);
    static_assert(lock_traits<NullLock>::has_debug_introspection);
#else
    static_assert(!DebugIntrospectableLock<SpinLock>);
    static_assert(!DebugIntrospectableLock<NullLock>);
    static_assert(!lock_traits<SpinLock>::has_debug_introspection);
    static_assert(!lock_traits<NullLock>::has_debug_introspection);
#endif

    std::mutex mtx;
    EXPECT_EQ(lock_traits<std::mutex>::debug_is_locked(mtx), std::nullopt);
    EXPECT_EQ(lock_traits<std::mutex>::debug_is_owned_by_current_thread(mtx), std::nullopt);
    // assert_lock_held is a no-op for std::mutex even when unlocked
    assert_lock_held(mtx);
    {
        lock_traits<std::mutex>::guard_type guard(mtx);
        assert_lock_held(mtx);
    }

    CustomBasicLock custom;
    EXPECT_EQ(lock_traits<CustomBasicLock>::debug_is_locked(custom), std::nullopt);
    EXPECT_EQ(lock_traits<CustomBasicLock>::debug_is_owned_by_current_thread(custom),
              std::nullopt);
    assert_lock_held(custom);
}


TEST(NullLockTest, BasicLockingAndRAII)
{
    NullLock lock;
    int counter = 0;

    lock.lock();
    ++counter;
    assert_lock_held(lock);
    lock.unlock();
    EXPECT_EQ(counter, 1);

    EXPECT_TRUE(lock.try_lock());
    ++counter;
    assert_lock_held(lock);
    lock.unlock();
    EXPECT_EQ(counter, 2);

    {
        std::scoped_lock<NullLock> guard(lock);
        ++counter;
        assert_lock_held(lock);
    }
    EXPECT_EQ(counter, 3);

    {
        std::unique_lock<NullLock> ulock(lock);
        EXPECT_TRUE(ulock.owns_lock());
        ++counter;
        ulock.unlock();
        EXPECT_FALSE(ulock.owns_lock());
    }
    EXPECT_EQ(counter, 4);
}


TEST(NullLockTest, EmptyObjectOptimizationInRelease)
{
    struct HolderWithLock
    {
        std::size_t value{0};
        MLVLIB_NO_UNIQUE_ADDRESS NullLock lock{};
    };

#ifdef NDEBUG
    static_assert(std::is_empty_v<NullLock>,
                  "NullLock must be an empty class in NDEBUG builds");
    EXPECT_EQ(sizeof(HolderWithLock), sizeof(std::size_t));
#else
    EXPECT_GE(sizeof(HolderWithLock), sizeof(std::size_t));
#endif
}


#ifndef NDEBUG
TEST(NullLockTest, DebugIntrospectionAndReentrancy)
{
    NullLock lock;
    EXPECT_FALSE(lock.debug_is_locked());
    EXPECT_FALSE(lock.debug_is_owned_by_current_thread());
    EXPECT_EQ(lock_traits<NullLock>::debug_is_locked(lock), std::optional<bool>(false));
    EXPECT_EQ(lock_traits<NullLock>::debug_is_owned_by_current_thread(lock),
              std::optional<bool>(false));

    lock.lock();
    EXPECT_TRUE(lock.debug_is_locked());
    EXPECT_TRUE(lock.debug_is_owned_by_current_thread());
    EXPECT_EQ(lock_traits<NullLock>::debug_is_locked(lock), std::optional<bool>(true));
    EXPECT_EQ(lock_traits<NullLock>::debug_is_owned_by_current_thread(lock),
              std::optional<bool>(true));

    // Re-entrant try_lock fails in debug builds
    EXPECT_FALSE(lock.try_lock());
    EXPECT_TRUE(lock.debug_is_owned_by_current_thread());

    lock.unlock();
    EXPECT_FALSE(lock.debug_is_locked());
    EXPECT_FALSE(lock.debug_is_owned_by_current_thread());
}


TEST(NullLockTest, DoubleLockAborts)
{
    NullLock lock;
    lock.lock();
    EXPECT_DEATH(lock.lock(), "NullLock");
    lock.unlock();
}


TEST(NullLockTest, UnlockWithoutHoldAborts)
{
    NullLock lock;
    EXPECT_DEATH(lock.unlock(), "NullLock");
}


TEST(NullLockTest, UnlockByNonOwnerAborts)
{
    EXPECT_DEATH(
        {
            NullLock lock;
            lock.lock();
            std::thread thief([&lock]() { lock.unlock(); });
            thief.join();
        },
        "NullLock");
}


TEST(NullLockTest, AssertLockHeldAbortsWhenUnlocked)
{
    NullLock lock;
    EXPECT_DEATH(assert_lock_held(lock), "lock must be held");
}
#endif // NDEBUG


TEST(SpinLockTest, ManyThreads)
{
    std::size_t const num_cores = std::thread::hardware_concurrency();
    println("Detected {} hardware threads", num_cores);
    ASSERT_GT(num_cores, 0u);
    const std::size_t num_threads = num_cores * 8 / 10;

    constexpr std::size_t increments_per_thread = 10000;

    uint32_t counter = 0;
    SpinLock lock;

    // lock initially to ensure all threads contend
    std::unique_lock<SpinLock> ulock(lock); // lock for setup

    auto worker = [&counter, &lock]() {
        for (size_t i = 0; i < increments_per_thread; ++i)
        {
            std::scoped_lock<SpinLock> slock(lock);
            ++counter;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i)
    {
        threads.emplace_back(worker);
    }

    EXPECT_EQ(counter, 0u);
    ulock.unlock(); // unlock to allow threads to proceed

    for (auto& t : threads)
    {
        t.join();
    }

    EXPECT_EQ(counter, num_threads * increments_per_thread);
}


TEST(LifetimeObserverTest, SimpleObjectOwnership)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    {
        TestObject obj;
        EXPECT_EQ(obj.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj.getCount(TestObject::e_refType::observer), 0);
        {
            LifetimeObserver observer = obj.getObserver(); // weak reference
            EXPECT_TRUE(observer.isAlive());
            EXPECT_EQ(obj.getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(obj.getCount(TestObject::e_refType::observer), 1);
            EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);
        }
        EXPECT_TRUE(obj.isAlive());
        EXPECT_EQ(obj.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj.getCount(TestObject::e_refType::observer), 0);
    }

    {
        TestObject* obj = new TestObject();
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 0);

        LifetimeObserver observer = obj->getObserver(); // weak reference
        EXPECT_TRUE(obj->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_TRUE(observer);

        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);

        delete obj;
        EXPECT_FALSE(observer.isAlive());
        EXPECT_FALSE(observer);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);
    }
}

/*
TEST(LifetimeObserverTest, MoveSemantics)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    {
        TestObject obj1;
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::observer), 0);

        LifetimeObserver obj2 = std::move(obj1);
        EXPECT_FALSE(obj1.isAlive());
        EXPECT_TRUE(obj2.isAlive());

        EXPECT_EQ(obj2.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj2.getCount(TestObject::e_refType::observer), 0);
    }
}
*/

TEST(LifetimeObserverTest, CopySemantics)
{
    struct TestObject: public LifetimeObserver
    {
        TestObject() = default;
        TestObject(int value) : i(value) {}
        ~TestObject() = default;

        int i = 0;
    };

    {
        TestObject* obj1 = new TestObject{1};
        EXPECT_EQ(obj1->i, 1);
        EXPECT_TRUE(obj1->isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 0);

        TestObject* obj2 = new TestObject{2};
        EXPECT_EQ(obj2->i, 2);
        EXPECT_TRUE(obj2->isAlive());
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::observer), 0);

        LifetimeObserver observer(obj1->getObserver());
        EXPECT_TRUE(obj1->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::observer), 0);

        {
            LifetimeObserver observer2(obj2->getObserver());
            EXPECT_TRUE(obj2->isAlive());
            EXPECT_TRUE(observer2.isAlive());
            EXPECT_TRUE(observer.isAlive());

            EXPECT_EQ(obj2->getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(obj2->getCount(TestObject::e_refType::observer), 1);
            EXPECT_EQ(observer2.getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(observer2.getCount(TestObject::e_refType::observer), 1);
            EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
            EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);
        }
        // observer2 goes out of scope

        EXPECT_TRUE(obj1->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::observer), 0);

        // obj2 becomes a fully unique, owned copy of obj1. Unique meaning
        // they do not share ownership of the same underlying object; they
        // uniquely own separate objects.
        *obj2 = *obj1; // copy assignment
        EXPECT_EQ(obj1->i, 1);
        EXPECT_EQ(obj2->i, 1);
        EXPECT_TRUE(obj1->isAlive());
        EXPECT_TRUE(obj2->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj2->getCount(TestObject::e_refType::observer), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);

        TestObject* obj3 = new TestObject{3};
        EXPECT_EQ(obj3->i, 3);
        EXPECT_TRUE(obj3->isAlive());
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::observer), 0);

        *obj3 = *obj1; // copy assignment
        EXPECT_EQ(obj1->i, 1);
        EXPECT_EQ(obj3->i, 1);
        EXPECT_TRUE(obj1->isAlive());
        EXPECT_TRUE(obj3->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::observer), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);

        delete obj2;
        EXPECT_TRUE(obj1->isAlive());
        EXPECT_TRUE(observer.isAlive());
        EXPECT_TRUE(obj3->isAlive());
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1->getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::observer), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);

        delete obj1;
        EXPECT_FALSE(observer.isAlive());
        EXPECT_TRUE(obj3->isAlive());
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj3->getCount(TestObject::e_refType::observer), 0);

        delete obj3;
        EXPECT_FALSE(observer.isAlive());
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 0);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);
    }

    {
        TestObject obj1;
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::observer), 0);

        LifetimeObserver observer(obj1.getObserver()); // weak reference
        EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 1);

        LifetimeObserver observer2(obj1); // weak reference
        EXPECT_EQ(observer2.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer2.getCount(TestObject::e_refType::observer), 2);

        LifetimeObserver observer3 = obj1.getObserver(); // weak reference
        EXPECT_EQ(observer3.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(observer3.getCount(TestObject::e_refType::observer), 3);

        //LifetimeObserver observer4 = obj1; // weak reference
        //EXPECT_EQ(observer4.getCount(TestObject::e_refType::owner), 1);
        //EXPECT_EQ(observer4.getCount(TestObject::e_refType::observer), 4);
    }
}

/*
TEST(LifetimeObserverTest, SelfAssignment)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    {
        TestObject obj1;
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj1.getCount(TestObject::e_refType::observer), 0);

        obj1 = obj1; // self-assignment
        EXPECT_TRUE(obj1.isAlive());
    }
}
*/

// Single-threaded / same-strand callback: destroy and use never overlap, so
// isAlive()-then-use needs no mutex (e.g. Asio on one event thread).
TEST(LifetimeObserverTest, TestSimpleCallback)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    struct TestObjHolder
    {
        TestObjHolder(TestObject* p): obj(p), obj_lifetime(p->getObserver()) { }
        TestObjHolder() = delete;
        ~TestObjHolder() = default;

        bool testAlive(bool assumeAlive)
        {
            bool objIsAlive = false;

            // Weak observer: check liveness before using the raw pointer.
            // Safe here because this test is single-threaded (sequencing).
            if (obj_lifetime.isAlive())
            {
                objIsAlive = obj->isAlive();
            }

            EXPECT_EQ(objIsAlive, assumeAlive);
            return objIsAlive;
        }

    private:
        TestObject* obj;
        LifetimeObserver obj_lifetime;
    };

    {
        TestObject* obj = new TestObject();
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 0);

        TestObjHolder holder(obj);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 1);

        EXPECT_TRUE(holder.testAlive(true));

        {
            LifetimeObserver observer = obj->getObserver(); // weak reference
            EXPECT_EQ(observer.getCount(TestObject::e_refType::owner), 1);
            EXPECT_EQ(observer.getCount(TestObject::e_refType::observer), 2);
            EXPECT_TRUE(observer.isAlive());
            EXPECT_TRUE(holder.testAlive(true));
        }
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 1);

        EXPECT_TRUE(holder.testAlive(true));

        delete obj;

        // Emulate a callback holding a weak reference after delete.
        EXPECT_FALSE(holder.testAlive(false));
    }
}


// Cross-thread variant of TestSimpleCallback: external mutex covers check/use
// and delete (isAlive() alone would be TOCTOU).
TEST(LifetimeObserverTest, TestSimpleCallbackThreaded)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    struct TestObjHolder
    {
        TestObjHolder(TestObject*& p, std::mutex& m)
            : obj(p), mu(m), obj_lifetime(p->getObserver()) { }
        TestObjHolder() = delete;
        ~TestObjHolder() = default;

        bool testAlive(bool assumeAlive)
        {
            std::lock_guard lock(mu);
            bool objIsAlive = false;
            if (obj_lifetime.isAlive())
            {
                objIsAlive = obj->isAlive();
            }
            EXPECT_EQ(objIsAlive, assumeAlive);
            return objIsAlive;
        }

        void poke()
        {
            std::lock_guard lock(mu);
            if (obj_lifetime.isAlive())
            {
                (void)obj->isAlive();
            }
        }

    private:
        TestObject*& obj;
        std::mutex& mu;
        LifetimeObserver obj_lifetime;
    };

    std::mutex access_mu;
    TestObject* obj = new TestObject();
    TestObjHolder holder(obj, access_mu);

    std::atomic<bool> start{false};
    constexpr int kReaders = 4;
    std::vector<std::thread> readers;
    readers.reserve(static_cast<std::size_t>(kReaders));
    for (int i = 0; i < kReaders; ++i)
    {
        readers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            for (int n = 0; n < 10000; ++n)
            {
                holder.poke();
            }
        });
    }

    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
        std::lock_guard lock(access_mu);
        delete obj;
        obj = nullptr;
    }

    for (auto& t : readers)
    {
        t.join();
    }

    EXPECT_FALSE(holder.testAlive(false));
}


// Single-threaded deferred callbacks (same-strand event engine).
TEST(LifetimeObserverTest, TestDeferredCallback)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;

        int incValue() { return ++i; }

    private:
        int i = 0;
    };

    // a simple simulated event engine: registers a callback, and that
    // callback is invoked when an event happens
    class EventEngine
    {
    public:
        EventEngine() = default;
        ~EventEngine() = default;

        int event()
        {
            if (callback)
                return callback();
            return -2;
        }

        void registerEventCallback(std::function<int()> cb)
        {
            callback = cb;
        }

        // loop until an end condition is met, triggering a callback every 10ms
        void startEventLoop(int endWhen)
        {
            int count = 0;
            while (count >= 0 && count < endWhen)
            {
                std::this_thread::sleep_for(10ms);

                if (callback)
                    count = callback();
                else
                    return;
            }
        }

    private:
        std::function<int()> callback;
    };

    {
        TestObject* obj = new TestObject();
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 0);

        EventEngine engine;

        // Weak observer; safe without a mutex on a single event strand.
        engine.registerEventCallback([&obj, alive = obj->getObserver()]() {
            if (alive)
                return obj->incValue();
            return -1;
        });

        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 1);

        int result = engine.event();
        EXPECT_EQ(result, 1);
        result = engine.event();
        EXPECT_EQ(result, 2);

        delete obj;

        // After delete on the same strand, the aliveness check skips the object.
        result = engine.event();
        EXPECT_EQ(result, -1);
    }

    {
        TestObject* obj = new TestObject();
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 0);

        EventEngine engine;

        engine.registerEventCallback([&obj, alive = obj->getObserver()]() {
            if (alive)
                return obj->incValue();
            return -1;
        });

        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 1);

        engine.startEventLoop(5);
        EXPECT_EQ(obj->incValue(), 6);

        delete obj;

        // No live object; loop should return with no further work.
        engine.startEventLoop(10);

        int result = engine.event();
        EXPECT_EQ(result, -1);
    }
}


// Cross-thread deferred callbacks: mutex covers check/use and delete.
TEST(LifetimeObserverTest, TestDeferredCallbackThreaded)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;

        int incValue() { return ++i; }

    private:
        int i = 0;
    };

    std::mutex access_mu;
    TestObject* obj = new TestObject();

    // Each worker gets its own std::function copy; the external mutex serializes
    // isAlive()/use against delete (avoid TOCTOU).
    auto make_callback = [&]() {
        return std::function<int()>([&access_mu, &obj, alive = obj->getObserver()]() {
            std::lock_guard lock(access_mu);
            if (alive)
                return obj->incValue();
            return -1;
        });
    };

    auto probe = make_callback();

    // Handshake instead of a fixed sleep: wait for live hits, destroy, wait for
    // dead hits, then stop. A 1ms sleep raced under release/-O3 on fast CI hosts.
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> live_hits{0};
    std::atomic<int> dead_hits{0};

    auto wait_nonzero = [](std::atomic<int>& counter) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (counter.load(std::memory_order_acquire) == 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    };

    constexpr int kWorkers = 4;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(kWorkers));
    for (int i = 0; i < kWorkers; ++i)
    {
        auto cb = make_callback();
        workers.emplace_back(
            [cb = std::move(cb), &start, &stop, &live_hits, &dead_hits]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_acquire))
            {
                int result = cb();
                if (result >= 0)
                    live_hits.fetch_add(1, std::memory_order_relaxed);
                else
                    dead_hits.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    ASSERT_TRUE(wait_nonzero(live_hits)) << "timed out waiting for live hits";
    {
        std::lock_guard lock(access_mu);
        delete obj;
        obj = nullptr;
    }
    ASSERT_TRUE(wait_nonzero(dead_hits)) << "timed out waiting for dead hits";
    stop.store(true, std::memory_order_release);

    for (auto& t : workers)
    {
        t.join();
    }

    EXPECT_GT(live_hits.load(), 0);
    EXPECT_GT(dead_hits.load(), 0);
    EXPECT_EQ(probe(), -1);
}


TEST(LifetimeObserverTest, TestCallbackWithMultipleObservers)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };
    {
        TestObject* obj = new TestObject();
        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 0);

        LifetimeObserver observer1 = obj->getObserver(); // weak reference
        LifetimeObserver observer2 = obj->getObserver(); // weak reference

        EXPECT_EQ(obj->getCount(TestObject::e_refType::owner), 1);
        EXPECT_EQ(obj->getCount(TestObject::e_refType::observer), 2);

        EXPECT_TRUE(observer1.isAlive());
        EXPECT_TRUE(observer2.isAlive());

        delete obj;

        EXPECT_FALSE(observer1.isAlive());
        EXPECT_FALSE(observer2.isAlive());
    }
}


// Concurrent isAlive() while the owner is destroyed must not data-race on counts.
TEST(LifetimeObserverTest, ConcurrentIsAliveDuringDestroy)
{
    class TestObject: public LifetimeObserver
    {
    public:
        TestObject() = default;
        ~TestObject() = default;
    };

    auto* obj = new TestObject();
    LifetimeObserver observer = obj->getObserver();
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> true_hits{0};
    std::atomic<int> false_hits{0};

    auto wait_nonzero = [](std::atomic<int>& counter) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (counter.load(std::memory_order_acquire) == 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    };

    constexpr int kReaders = 4;
    std::vector<std::thread> readers;
    readers.reserve(kReaders);
    for (int i = 0; i < kReaders; ++i)
    {
        readers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            while (!stop.load(std::memory_order_acquire))
            {
                if (observer.isAlive())
                {
                    true_hits.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    false_hits.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    ASSERT_TRUE(wait_nonzero(true_hits)) << "timed out waiting for true hits";
    delete obj;
    ASSERT_TRUE(wait_nonzero(false_hits)) << "timed out waiting for false hits";
    stop.store(true, std::memory_order_release);

    for (auto& t : readers)
    {
        t.join();
    }

    EXPECT_GT(true_hits.load(), 0);
    EXPECT_GT(false_hits.load(), 0);
    EXPECT_FALSE(observer.isAlive());
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
