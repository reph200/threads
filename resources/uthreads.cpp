#include<iostream>
#include<list>
#include <algorithm>
#include "uthreads.h"
#include "setjmp.h"
#include <sstream>
using namespace std;
//TODO:ERROR messages
//TODO:actually run and jump from and save threads
//handle when action called on main thread..
//maybe sleep should start next quantum?

/*
 * Interval-timer demo program.
 * Hebrew University OS course.
 * Author: OS, os@cs.huji.ac.il
 */

#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address (address_t addr)
{
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
  : "=g" (ret)
  : "0" (addr));
  return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

enum Thread_State
{
    Running, Ready, Blocked
};

struct Thread
{
    int id;
    Thread_State state;
    char *sp;
    int sleep;
    int quantum_counter;
    int flag_blocked;

};

// defining variables
list<Thread *> *thread_list;
list<Thread *> *ready_list;
Thread *running_thread;
Thread main_thread;
sigjmp_buf env[MAX_THREAD_NUM];
int total_quantums = 0;
int quantum;
int general_quantom_size;
int running_remaning_quantom;
struct itimerval timer;
list<int> available_ids;
int available_id = 0;

void fill_available_ids ()
{
  available_ids.clear ();
  for (int i = 1; i <= MAX_THREAD_NUM; i++)
  {
    available_ids.push_back (i);
  }
}

void wake_up_threads ()
{
  for (auto const &thread: *thread_list)
  {
    if (thread->sleep > 0)
    {
      thread->sleep -= 1;
    }
    if (thread->sleep == 0 && thread->flag_blocked == 0)
    {
      thread->state = Ready;
      ready_list->push_back (thread);
    }
  }
}

void setup_thread (int tid, char *stack, thread_entry_point entry_point)
{
  // initializes env[tid] to use the right stack, and to run from the function 'entry_point', when we'll use
  // siglongjmp to jump into the thread.
  address_t sp = (address_t) stack + STACK_SIZE - sizeof (address_t);
  address_t pc = (address_t) entry_point;
//  address_t sp = reinterpret_cast<address_t> (stack) + STACK_SIZE - sizeof
//      (address_t);
//  address_t pc = (address_t) entry_point;
  sigsetjmp (env[tid], 1);
  (env[tid]->__jmpbuf)[JB_SP] = translate_address (sp);
  (env[tid]->__jmpbuf)[JB_PC] = translate_address (pc);
  sigemptyset (&env[tid]->__saved_mask);
}

void jump_to_thread (int tid)
{
  timer.it_value.tv_sec = quantum / 1000000;
  timer.it_value.tv_usec = quantum - 1000000 * timer.it_value.tv_sec;
  siglongjmp (env[tid], 1);
}

/**
 * @brief Saves the current thread state, and jumps to the other thread.
 */
void context_switch ()
{
  // handle external defined
  int ret_val = sigsetjmp (env[running_thread->id], 1);
  if (ready_list->empty ())
  {
    running_thread = &main_thread;
  }
  else
  {
    running_thread = ready_list->front ();
    ready_list->pop_front ();
  }
  running_thread->state = Running;
  running_thread->quantum_counter += 1;

//  printf("context_switch: ret_val=%d\n", ret_val);
  bool did_just_save_bookmark = ret_val == 0;
//    bool did_jump_from_another_thread = ret_val != 0;
  if (did_just_save_bookmark)
  {
    jump_to_thread (running_thread->id);
  }
}

int get_available_id ()
{
  if (available_ids.empty ())
  {
    return -1;
  }
  available_id = available_ids.front ();
  available_ids.pop_front ();
  return available_id;
}

void timer_handler (int sig)
{
  //TODO:handle sig
  total_quantums += 1;
  wake_up_threads ();
  if (running_thread->id == 0)
  {
    return;
  }
  running_thread->state = Ready;
  ready_list->push_back (running_thread);
  context_switch ();
}

int setup_timer (void)
{
  struct sigaction sa = {0};
//  struct itimerval timer;

  // Install timer_handler as the signal handler for SIGVTALRM.
  sa.sa_handler = &timer_handler;
  if (sigaction (SIGVTALRM, &sa, NULL) < 0)
  {
    printf ("sigaction error.");
    return -1;
  }
  return 0;
}

// origianl functions
int uthread_init (int quantum_usecs)
{
  fill_available_ids ();
  setup_timer ();
  if (quantum_usecs <= 0)
  {
    return -1;
  }
  quantum = quantum_usecs;
  int quantum_secs = quantum_usecs / 1000000;
  timer.it_value.tv_sec = quantum_secs;
  timer.it_value.tv_usec = quantum_usecs - 1000000 * quantum_secs;
  timer.it_interval.tv_sec = quantum_secs;
  timer.it_interval.tv_usec = quantum_usecs - 1000000 * quantum_secs;
  main_thread = {0, Running};
  running_thread = &main_thread;

  //TODO: Start a virtual timer. It counts down whenever this process is
  // executing.
  if (setitimer (ITIMER_VIRTUAL, &timer, NULL))
  {
    printf ("setitimer error.");
  }

  thread_list = new list<Thread *> ();
  ready_list = new list<Thread *> ();
  return 0;
}

int uthread_spawn (thread_entry_point entry_point)
{
  int id = get_available_id ();
  if (id == -1 || entry_point == NULL)
  {
    return -1;
  }
  char *stack = new char[STACK_SIZE];
  setup_thread (id, stack, entry_point);
  Thread *new_thread = new Thread{id, Ready, stack};
  thread_list->push_back (new_thread); // about the *...
  ready_list->push_back (new_thread); // same
  if (running_thread->id == 0)
  {
    main_thread.state=Ready;
    context_switch ();
  }
  return id;
}

int uthread_terminate (int tid)
{
  auto target_thread = std::find_if (thread_list->begin (), thread_list->end (),
                                     [&] (const Thread *t)
                                     { return t->id == tid; });
  // if thread with this id does not exist in thread list
  if (target_thread == thread_list->end ())
  {
    return -1;
  }
    //if thread is the main-thread
  else if (tid == 0)
  {
    for (const auto thread: *thread_list) // thread_list)?
    {
      ready_list->remove (thread);
      thread_list->remove (thread);
      delete[] thread->sp;
      delete thread;
    }
    delete ready_list;
    delete thread_list;
    fill_available_ids ();
    exit (0);
  }

    //not main thread
  else
  {
    Thread *found_thread = *target_thread;
    int new_available_id = (found_thread)->id;
    ready_list->remove (found_thread);
    thread_list->remove (found_thread);
    // is a pointer
    delete[] found_thread->sp;
    delete found_thread;
    auto it = std::lower_bound (available_ids.begin (), available_ids.end (),
                                new_available_id);
    available_ids.insert (it, new_available_id);
    return 0;
  }
}

int uthread_block (int tid)
{
  if (tid == 0)
  {
    return -1;
  }
  if (tid == running_thread->id) // blocking itself
  {
    running_thread->state = Blocked;
    running_thread->flag_blocked = 1;
    context_switch ();
    running_thread->quantum_counter += 1;
    return 0;
  }
  auto target_thread = std::find_if (thread_list->begin (), thread_list->end (),
                                     [&] (const Thread *t)
                                     { return t->id == tid; });

  if (target_thread != thread_list->end ())
  {
    Thread *found_thread = *target_thread;
    if (found_thread->state == Blocked)
    {
      return 0;
    }
    found_thread->state = Blocked;
    found_thread->flag_blocked = 1;
    ready_list->remove (found_thread);
  }
  else
  {
    return -1;
  }
  return 0;
}

int uthread_resume (int tid)
{
  auto target_thread = std::find_if (thread_list->begin (), thread_list->end
                                         (),
                                     [&] (const Thread *t)
                                     { return t->id == tid; });

  if (target_thread != thread_list->end ())
  {
    Thread *found_thread = *target_thread;
    if (found_thread->state != Blocked)
    {
      return 0;
    }
    found_thread->flag_blocked = 0;
    if (found_thread->sleep == 0)
    {
      found_thread->state = Ready;
      ready_list->push_back (found_thread);
    }
  }
  else
  {
    return -1;
  }
  return 0;
}

int uthread_sleep (int num_quantums)
{
  if (num_quantums <= 0)
  {
    return -1;
  }
  running_thread->state = Blocked;
  running_thread->sleep = num_quantums + 1;
  context_switch ();
  running_thread->quantum_counter += 1;
  return 0;
}

int uthread_get_tid ()
{
  return running_thread->id;
}

int uthread_get_total_quantums ()
{
  return total_quantums;
}

int uthread_get_quantums (int tid)
{
  auto target_thread = std::find_if (thread_list->begin (), thread_list->end
                                         (),
                                     [&] (const Thread *t)
                                     { return t->id == tid; });

  if (target_thread != thread_list->end ())
  {
    return (*target_thread)->quantum_counter;
  }
  else
  {
    return -1;
  }
}

