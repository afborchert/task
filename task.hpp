/* 
   Copyright (c) 2017, 2019, 2021 Andreas F. Borchert
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

/*
   this header-only package provides tasks with dependencies
   whose submission to a thread_pool is postponed until all
   dependencies are resolved
*/

#ifndef MT_TASK_HPP
#define MT_TASK_HPP 1

#if __cplusplus < 201402L
#error This file requires compiler and library support for the \
ISO C++ 2014 standard.
#else

#include <cassert>
#include <functional>
#include <initializer_list>
#include <list>
#include <memory>
#include <mutex>
#include <utility>
#include <thread_pool.hpp>

namespace mt {

/* task groups are used as a synchronization measure,
   i.e. the destruction of a task group is delayed until
   all its tasks are completed */
class task_group;

namespace impl {

/* the dependencies are organized in a directed,
   hopefully anti-cyclic graph with task_handle_rec object
   as vertices;
   the edges are represented by corresponding shared_ptr objects
   which are named task_handle */
class task_handle_rec;
using task_handle = std::shared_ptr<task_handle_rec>;

/* submission functions return a shared_ptr to
   a composite object task_rec that consists of
     - a shared_ptr to the corresponding vertice in the graph and
     - a shared_future object to access the result
*/
class basic_task_rec;
using basic_task = std::shared_ptr<basic_task_rec>;
template<typename T> class task_rec;
template<typename T> using task = std::shared_ptr<task_rec<T>>;

} // namespace impl

template<typename T> using task = impl::task<T>;

/* forward declaration of front-end functions
   such that we can befriend them */
template<typename F, typename... Parameters>
auto submit(thread_pool& tp,
      std::initializer_list<impl::basic_task> dependencies,
      F&& task_function, Parameters&&... parameters)
	 -> task<decltype(task_function(parameters...))>;
template<typename F, typename Iterator, typename... Parameters>
auto submit(thread_pool& tp,
      Iterator begin, Iterator end,
      F&& task_function, Parameters&&... parameters)
	 -> task<decltype(task_function(parameters...))>;

namespace impl {

/* task handles are used as vertices of the dependency graph;
   this class is mostly private and befriended with the
   submission functions
*/
class task_handle_rec: public std::enable_shared_from_this<task_handle_rec> {
   public:
      using State = enum {PREPARING, WAITING, SUBMITTED, FINISHED};
	 /*
	    PREPARING: still collecting dependencies & submit task
	    WAITING:   not all dependencies have been resolved yet
	    SUBMITTED: submitted to corresponding thread pool
	    FINISHED:  task is finished
	 */
      task_handle_rec() : state(PREPARING), dependencies_left(0) {
      }
      ~task_handle_rec() {
	 assert(state == FINISHED);
      }
   private:
      /* set function that submits this task to its thread pool;
         as we bury this operation into a function object, we
	 do not need a reference to the thread pool in task_handle_rec */
      void set_submit_task(std::function<void()> submit_task_func) {
	 std::lock_guard<std::mutex> lock(mutex);
	 assert(state == PREPARING && !submit_task && submit_task_func);
	 submit_task = submit_task_func;
      }
      /* add another dependency during the preparatory phase */
      bool add_dependency(task_handle dependency) {
	 std::lock_guard<std::mutex> lock(mutex);
	 assert(state == PREPARING);
	 if (dependency->add_dependent(shared_from_this())) {
	    ++dependencies_left;
	    return true;
	 } else {
	    return false;
	 }
      }
      /* end preparatory phase */
      void finish_preparation() {
	 std::lock_guard<std::mutex> lock(mutex);
	 assert(state == PREPARING);
	 if (dependencies_left == 0) {
	    enqueue();
	 } else {
	    state = WAITING;
	 }
      }
      /* enlist t as one of our dependents,
         i.e. when we finish we have to decrement the
	 number of dependencies of t;
	 we return false if we are already finished, otherwise true */
      bool add_dependent(task_handle t) {
	 std::lock_guard<std::mutex> lock(mutex);
	 if (state == FINISHED) {
	    return false;
	 } else {
	    dependents.push_back(t);
	    return true;
	 }
      }
      /* invoked by one of the tasks we depend on when it is finished */
      void remove_dependency() {
	 std::lock_guard<std::mutex> lock(mutex);
	 if (--dependencies_left == 0) {
	    if (state == PREPARING) return; // postponed
	    assert(state == WAITING);
	    enqueue();
	 }
      }
      /* submit our task in the corresponding thread pool by
	 invoking the stored function object submit_task;
	 this method is invoked when we have already mutual exclusion */
      void enqueue() {
	 submit_task();
	 /* be friendly to the std::shared_ptr-style of garbage collecting */
	 submit_task = nullptr;
	 state = SUBMITTED;
      }
      /* this method is invoked when the task is completed;
         we notify here all our dependents */
      void finish() {
	 std::lock_guard<std::mutex> lock(mutex);
	 assert(state == SUBMITTED);
	 for (auto dependent: dependents) {
	    dependent->remove_dependency();
	 }
	 /* be friendly to the std::shared_ptr-style
	    of garbage collecting */
	 dependents.clear();
	 /* we are done */
	 state = FINISHED;
      }

   public:
      /* befriending our front-end interfaces */
      template<typename F, typename... Parameters>
      friend auto ::mt::submit(thread_pool& tp,
	    std::initializer_list<::mt::impl::basic_task> dependencies,
	    F&& task_function, Parameters&&... parameters)
	       -> task<decltype(task_function(parameters...))>;
      template<typename F, typename Iterator, typename... Parameters>
      friend auto ::mt::submit(thread_pool& tp,
	    Iterator begin, Iterator end,
	    F&& task_function, Parameters&&... parameters)
	       -> task<decltype(task_function(parameters...))>;
      friend class ::mt::task_group;

   private:
      std::mutex mutex;
      State state;
      std::function<void()> submit_task;
      std::size_t dependencies_left;
      std::list<task_handle> dependents;
};

/* we need this base class to offer the get_handle() method on a
   non-templated class */
class basic_task_rec {
   public:
      basic_task_rec(task_handle handle) : handle(handle) {
      }
      task_handle get_handle() {
	 return handle;
      }
   protected:
      task_handle handle;
};

/* tasks consist of a task handle (for the interdependency graph)
   and a future object that delivers the return value of
   the corresponding task */
template<typename T>
class task_rec: public basic_task_rec {
   public:
      task_rec(task_handle handle, std::shared_future<T> result) :
	    basic_task_rec(handle), result(result) {
	 assert(result.valid());
      }
      void join() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 result.wait();
      }
      const T& get() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 return result.get();
      }
      const T& get_value() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 return result.get();
      }
   private:
      mutable std::mutex mutex;
      std::shared_future<T> result;
};
/* special case where we eliminate one level of indirection */
template<typename T>
class task_rec<task<T>>: public basic_task_rec {
   public:
      task_rec(task_handle handle, std::shared_future<task<T>> result) :
	    basic_task_rec(handle), result(result) {
	 assert(result.valid());
      }
      void join() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 auto nested_result = result.get();
	 nested_result->join();
      }
      const task<T>& get() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 return result.get();
      }
      const T& get_value() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 auto nested_result = result.get();
	 return nested_result->get_value();
      }
   private:
      mutable std::mutex mutex;
      std::shared_future<task<T>> result;
};
/* special case of task_rec for void where
   get() must not return void& */
template<>
class task_rec<void>: public basic_task_rec {
   public:
      task_rec(task_handle handle, std::shared_future<void> result) :
	    basic_task_rec(handle), result(result) {
	 assert(result.valid());
      }
      void join() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 result.wait();
      }
      void get() const {
	 join();
      }
   private:
      mutable std::mutex mutex;
      std::shared_future<void> result;
};
template<>
class task_rec<task<void>>: public basic_task_rec {
   public:
      task_rec(task_handle handle, std::shared_future<task<void>> result) :
	    basic_task_rec(handle), result(result) {
	 assert(result.valid());
      }
      void join() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 auto nested_result = result.get();
	 nested_result->join();
      }
      const task<void>& get() const {
	 std::lock_guard<std::mutex> lock(mutex);
	 return result.get();
      }
   private:
      mutable std::mutex mutex;
      std::shared_future<task<void>> result;
};

} // namespace impl

/* task groups are used for synchronization
   as their destructor waits until all tasks
   of this task group are finished */
class task_group {
   public:
      task_group(thread_pool& tp) : tp(tp), active(0) {
      }
      ~task_group() {
	 join();
      }
      /* wait until all tasks of this task group are finished */
      void join() {
	 std::unique_lock<std::mutex> lock(mutex);
	 while (active > 0) {
	    cv.wait(lock);
	 }
      }
      template<typename F, typename... Parameters>
      auto submit(std::initializer_list<impl::basic_task> dependencies,
	    F&& task_function, Parameters&&... parameters)
	       -> impl::task<decltype(task_function(parameters...))> {
	 return submit(dependencies.begin(), dependencies.end(),
	    std::forward<F>(task_function),
	    std::forward<Parameters>(parameters)...);
      }
      template<typename Iterator, typename F, typename... Parameters>
      auto submit(Iterator begin, Iterator end,
	    F&& task_function, Parameters&&... parameters)
	       -> impl::task<decltype(task_function(parameters...))> {
	 using T = decltype(task_function(parameters...));
	 auto f = std::make_shared<std::packaged_task<T()>>(
	    std::bind(std::forward<F>(task_function),
	       std::forward<Parameters>(parameters)...)
	 );
	 auto th = std::make_shared<impl::task_handle_rec>();
	 auto t = std::make_shared<impl::task_rec<T>>(th, f->get_future());
	 for (auto it = begin; it != end; ++it) {
	    th->add_dependency((*it)->get_handle());
	 }
	 {
	    std::lock_guard<std::mutex> lock(mutex);
	    ++active;
	 }
	 th->set_submit_task([f,th,this]() {
	    tp.submit([f,th,this]() {
	       (*f)(); 
	       th->finish();
	       std::lock_guard<std::mutex> lock(mutex);
	       if (--active == 0) {
		  cv.notify_all();
	       }
	    });
	 });
	 th->finish_preparation();
	 return t;
      }
   private:
      std::mutex mutex;
      std::condition_variable cv;
      thread_pool& tp;
      std::size_t active; /* number of still running tasks */
};

/* submission front-end where the dependencies are
   specified through an initializer_list */
template<typename F, typename... Parameters>
auto submit(thread_pool& tp,
      std::initializer_list<impl::basic_task> dependencies,
      F&& task_function, Parameters&&... parameters)
	 -> impl::task<decltype(task_function(parameters...))> {
   using T = decltype(task_function(parameters...));
   auto f = std::make_shared<std::packaged_task<T()>>(
      std::bind(std::forward<F>(task_function),
	 std::forward<Parameters>(parameters)...)
   );
   auto th = std::make_shared<impl::task_handle_rec>();
   auto t = std::make_shared<impl::task_rec<T>>(th, f->get_future());
   for (auto dependency: dependencies) {
      th->add_dependency(dependency->get_handle());
   }
   th->set_submit_task([f,th,&tp]() {
      tp.submit([f,th]() {
	 (*f)(); 
	 th->finish();
      });
   });
   th->finish_preparation();
   return t;
}

/* submission front-end where the dependencies are
   specified by a pair of iterators */
template<typename F, typename Iterator, typename... Parameters>
auto submit(thread_pool& tp,
      Iterator begin, Iterator end,
      F&& task_function, Parameters&&... parameters)
	 -> impl::task<decltype(task_function(parameters...))> {
   using T = decltype(task_function(parameters...));
   auto f = std::make_shared<std::packaged_task<T()>>(
      std::bind(std::forward<F>(task_function),
	 std::forward<Parameters>(parameters)...)
   );
   auto th = std::make_shared<impl::task_handle_rec>();
   auto t = std::make_shared<impl::task_rec<T>>(th, f->get_future());
   for (auto it = begin; it != end; ++it) {
      th->add_dependency((*it)->get_handle());
   }
   th->set_submit_task([f,th,&tp]() {
      tp.submit([f,th]() {
	 (*f)(); 
	 th->finish();
      });
   });
   th->finish_preparation();
   return t;
}

} // namespace mt

#endif // of #if __cplusplus < 201402L #else ...
#endif // of #ifndef MT_TASK_HPP
