# task
Tasks with dependencies for C++14

## Summary

This header-only C++14 package is an extension of the thread pool
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
function are edges, i.e. pointers to the internal vertices
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
      return fib_impl(n, fib_impl);
   };
   auto job = mt::submit(tp, {}, [&]() {
      return fibonacci(10);
   });
   int result = job->get_value();
```

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

You will need a compiler with support for C++14.
