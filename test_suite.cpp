/* 
   Copyright (c) 2019 Andreas F. Borchert
   All rights reserved.

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
   KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
   WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#include <functional>
#include <iostream>

#include <task.hpp>
#include <thread_pool.hpp>

bool t1() {
   mt::thread_pool tp(2);
   auto a = mt::submit(tp, {}, []() {
      return 7;
   });
   auto b = mt::submit(tp, {}, []() {
      return 22;
   });
   auto c = mt::submit(tp, {a, b}, [=]() {
      return a->get_value() + b->get_value();
   });
   auto d = mt::submit(tp, {}, []() {
      return 13;
   });
   auto e = mt::submit(tp, {c, d}, [=]() {
      return c->get_value() + d->get_value();
   });
   return e->get_value() == 42;
}

/* test that computes Fibonacci numbers recursively */
bool t2() {
   auto fibonacci = [](mt::thread_pool& tp, unsigned int n) {
      auto fib_impl = [](mt::thread_pool& tp, unsigned int n, auto& fib) {
	 if (n <= 1) {
	    return mt::submit(tp, {}, [n]() {
	       return n;
	    });
	 }
	 auto sum1 = fib(tp, n-1, fib);
	 auto sum2 = fib(tp, n-2, fib);
	 return mt::submit(tp, {sum1, sum2}, [=]() {
	    return sum1->get_value() + sum2->get_value();
	 });
      };
      return fib_impl(tp, n, fib_impl);
   };
   unsigned int results[] = {0, 1, 1, 2, 3, 5, 8};
   for (std::size_t tpool_size: {4, 2, 1}) {
      for (std::size_t n = 0; n < sizeof(results)/sizeof(results[0]); ++n) {
	 mt::thread_pool tpool(tpool_size);
	 auto res = mt::submit(tpool, {}, [&tpool, n, &fibonacci]() {
	    return fibonacci(tpool, n);
	 });
	 if (res->get_value() != results[n]) return false;
      }
   }
   return true;
}

struct statistics {
   unsigned int passed = 0;
   unsigned int failed = 0;
   unsigned int exceptions = 0;
};

template<typename F>
void t(const std::string& name, F&& f, statistics& stats) {
   std::cout << name << ": ";
   try {
      if (f()) {
	 ++stats.passed;
	 std::cout << "ok";
      } else {
	 ++stats.failed;
	 std::cout << "failed";
      }
   } catch (std::exception& e) {
      ++stats.exceptions; ++stats.failed;
      std::cout << "failed due to " << e.what();
   }
   std::cout << std::endl;
}

bool t3() {
   mt::thread_pool tp(2);
   int a_val, b_val, c_val, d_val, e_val;
   auto a = mt::submit(tp, {}, [&]() {
      a_val = 7;
   });
   auto b = mt::submit(tp, {}, [&]() {
      b_val = 22;
   });
   auto c = mt::submit(tp, {a, b}, [&]() {
      c_val = a_val + b_val;
   });
   auto d = mt::submit(tp, {}, [&]() {
      d_val = 13;
   });
   auto e = mt::submit(tp, {c, d}, [&]() {
      e_val = c_val + d_val;
   });
   e->join();
   return e_val == 42;
}

bool t4() {
   mt::thread_pool tp(2);
   int a_val, b_val, c_val, d_val, e_val;
   {
      mt::task_group tg(tp);
      auto a = tg.submit({}, [&]() {
	 a_val = 7;
      });
      auto b = tg.submit({}, [&]() {
	 b_val = 22;
      });
      auto c = tg.submit({a, b}, [&]() {
	 c_val = a_val + b_val;
      });
      auto d = tg.submit({}, [&]() {
	 d_val = 13;
      });
      auto e = tg.submit({c, d}, [&]() {
	 e_val = c_val + d_val;
      });
   }
   return e_val == 42;
}

bool t5() {
   mt::thread_pool tp(2);
   auto foo = [](mt::thread_pool& tp, int a, int b, auto& foo) {
      int len = b - a;
      if (len <= 2) {
	 return mt::submit(tp, {}, [=]() {
	    if (len == 1) {
	       return a;
	    } else if (len == 2) {
	       return a + a + 1;
	    } else {
	       return 0;
	    }
	 });
      } else {
	 int mid = a + len/2;
	 auto part1 = mt::submit(tp, {}, [=,&tp]() {
	    return foo(tp, a, mid, foo);
	 });
	 auto part2 = mt::submit(tp, {}, [=,&tp]() {
	    return foo(tp, mid, b, foo);
	 });
	 return mt::submit(tp, {part1, part2}, [=]() {
	    return part1->get_value() + part2->get_value();
	 });
      }
   };
   auto result = foo(tp, 0, 100, foo);
   return result->get_value() == 4950;
}

int main() {
   statistics stats;
   t(" t1", t1, stats);
   t(" t2", t2, stats);
   t(" t3", t3, stats);
   t(" t4", t4, stats);
   t(" t4", t5, stats);
   unsigned int tests = stats.passed + stats.failed;
   if (tests == stats.passed) {
      std::cout << "all tests passed" << std::endl;
   } else {
      std::cout << stats.passed << " tests passed, " <<
	 stats.failed << " tests failed (" <<
	 (double) stats.failed / tests * 100.0 << "%)" <<
	 std::endl;
   }
}
