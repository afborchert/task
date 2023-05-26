# task
Tasks with dependencies for C++17

## Summary

This header-only C++17 package is an extension of the thread pool
package that allows to create tasks with dependencies. If the number of
dependencies is non-empty, the actual submission to the thread pool
will be postponed until all dependencies are resolved.

```C++
   mt::thread_pool tp(2);
   auto a = mt::submit(tp, {}, []() {
      return 20;
   });
   auto b = mt::submit(tp, {}, []() {
      return 22;
   });
   auto c = mt::submit(tp, {a, b}, [=]() {
      return a->get_value() + b->get_value();
   });
   int result = c->get_value();
```

In this example, _a_ and _b_ are tasks with no dependencies
but task _c_ depends on the completion of _a_ and _b_.

Note that implicitly a directed anti-cyclic graph is
created where the objects returned by this `mt::submit`
function are pointers to the internal vertices
of the graph. These pointers are based on `std::shared_ptr`
which are automatically free'd.

A recursive divide-and-conquer-pattern can be implemented as follows:

```C++
   mt::thread_pool tp(2);
   auto fibonacci = [&tp](unsigned int n) {
      auto fib_impl = [&tp](unsigned int n, auto& fib) {
	 if (n <= 1) {
	    return mt::submit(tp, {}, [n]() {
	       return n;
	    });
	 }
	 auto sum1 = fib(n-1, fib);
	 auto sum2 = fib(n-2, fib);
	 return mt::submit(tp, {sum1, sum2}, [=]() {
	    return sum1->get_value() + sum2->get_value();
	 });
      };
      return fib_impl(n, fib_impl)->get_value();
   };
   int result = fibonacci(10);
```

Task groups allow to synchronize with the completion of an arbitrary
number of individual tasks. This is particularly convenient for tasks
that do not return a value. Just create a task group object of type
`mt::task_group` which needs a reference to a thread pool and submit
tasks to the task group. The destructor of the task group will wait
until all tasks are finished which were submitted to the task group.
Following example demonstrates this for a parallelized quicksort
implementation:

```C++
#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>
#include <task.hpp>
#include <thread_pool.hpp>

namespace pqsort_impl {
   template<typename RandomIt, typename Compare>
   auto partition(RandomIt begin, RandomIt end, Compare cmp) {
      /* using Hoare partitioning */
      auto len = std::distance(begin, end);
      auto pivot = *(std::next(begin, len/2));
      auto it1 = begin;
      auto it2 = std::next(begin, len-1);
      for(;;) {
	 while (cmp(*it1, pivot)) {
	    ++it1;
	 }
	 while (cmp(pivot, *it2)) {
	    --it2;
	 }
	 if (it1 >= it2) {
	    return it1;
	 }
	 std::iter_swap(it1, it2);
      }
   }

   template<typename RandomIt, typename Compare>
   void sort(mt::task_group& tg, RandomIt begin, RandomIt end, Compare cmp) {
      if (std::distance(begin, end) > 1) {
	 auto p = tg.submit({}, [=]() {
	    /* avoid argument-dependent lookup of partition,
	       otherwise we might conflict with std::partition */
	    return ::pqsort_impl::partition(begin, end, cmp);
	 });
	 tg.submit({p}, [=,&tg]() {
	    sort(tg, begin, p->get_value(), cmp);
	 });
	 tg.submit({p}, [=,&tg]() {
	    sort(tg, p->get_value(), end, cmp);
	 });
      }
   }
} // namespace pqsort_impl

template<typename RandomIt, typename Compare = std::less<>>
void pqsort(mt::thread_pool& tp,
      RandomIt begin, RandomIt end, Compare cmp = Compare{}) {
   mt::task_group tg(tp);
   pqsort_impl::sort(tg, begin, end, cmp);
}
```

In this example, _pqsort_ will not return until all tasks
submitted to _tg_ are completed.

## License

This package is available under the terms of
the [MIT License](https://opensource.org/licenses/MIT).

## Files

To use this package, you will need to drop the header files
[thread_pool.hpp](https://github.com/afborchert/tpool/blob/master/thread_pool.hpp)
and
[task.hpp](https://github.com/afborchert/task/blob/master/task.hpp)

within your project and `#include` it.

The source file `test_suite.cpp` is an associated
test suite and the Makefile helps to compile it.

## Downloading

If you want to clone this project, you should do this recursively:

```
git clone --recursive https://github.com/afborchert/task.git
```

You will need a compiler with support for C++17.
