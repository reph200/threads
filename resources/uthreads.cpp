#include<iostream>
#include<vector>
#include <list>
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
address_t translate_address(address_t addr) {
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

enum Thread_State {
    Running, Ready, Blocked
};

struct Thread {
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
Thread *to_be_deleted;
Thread main_thread;
sigjmp_buf env[MAX_THREAD_NUM];
int total_quantums;
int quantum;
struct itimerval timer;
list<int> available_ids;
int available_id = 0;
sigset_t set;

void fill_available_ids() {
    available_ids.clear();
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        available_ids.push_back(i);
    }
}

//TO DO: deal with the condition of adding to the ready list
void wake_up_threads() {
    for (auto const &thread: *thread_list) {
        if (thread->sleep > 0) {
            thread->sleep -= 1;
            if (thread->sleep == 0 && thread->flag_blocked == 0) {
                thread->state = Ready;
                ready_list->push_back(thread);
            }
        }
    }
}

void setup_thread(int tid, char *stack, thread_entry_point entry_point) {
    // initializes env[tid] to use the right stack, and to run from the function 'entry_point', when we'll use
    // siglongjmp to jump into the thread.
    address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
    address_t pc = (address_t) entry_point;
//  address_t sp = reinterpret_cast<address_t> (stack) + STACK_SIZE - sizeof
//      (address_t);
//  address_t pc = (address_t) entry_point;
    sigsetjmp (env[tid], 1);
    (env[tid]->__jmpbuf)[JB_SP] = translate_address(sp);
    (env[tid]->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env[tid]->__saved_mask);
}

void jump_to_thread(int tid) {
    timer.it_value.tv_usec = quantum;
    siglongjmp(env[tid], 1);
}

/**
 * @brief Saves the current thread state, and jumps to the other thread.
 */
void context_switch() {
    sigprocmask(SIG_BLOCK, &set, NULL);
    // handle external defined
    wake_up_threads(); // moved from timer_handler
    total_quantums += 1;
    int ret_val = sigsetjmp (env[running_thread->id], 1);
    bool did_just_save_bookmark = ret_val == 0;
    if (did_just_save_bookmark) {
        if (running_thread->state != Blocked && to_be_deleted==NULL) {
            running_thread->state = Ready;
            ready_list->push_back(running_thread);
        }
        if (!ready_list->empty())
        {
            running_thread = ready_list->front();
            ready_list->erase(ready_list->begin());
        }
        running_thread->state = Running;
        running_thread->quantum_counter += 1;

        jump_to_thread(running_thread->id);
    }
    if (to_be_deleted != NULL && to_be_deleted->id != 0) { // terminated itself

        delete[] to_be_deleted->sp;
        delete[]to_be_deleted;
        to_be_deleted = NULL;
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

int get_available_id() {
    if (available_ids.empty()) {
        return -1;
    }
    available_id = available_ids.front();
    available_ids.pop_front();
    return available_id;
}

void timer_handler(int sig) {
    //TODO:handle sig
    context_switch();
}

int setup_timer(void) {
    struct sigaction sa = {0};
//  struct itimerval timer;

    // Install timer_handler as the signal handler for SIGVTALRM.
    sa.sa_handler = &timer_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        std::cerr << "system error: sigaction error\n";
        return -1;
    }
    return 0;
}

// origianl functions
int uthread_init(int quantum_usecs) {
    sigemptyset(&set);
    sigaddset(&set,SIGVTALRM);
    total_quantums = 1;
    thread_list = new list<Thread *>();
    ready_list = new list<Thread *>();
    fill_available_ids();
    setup_timer();
    if (quantum_usecs <= 0) {
        std::cerr << "thread library error: negative number of quantums is not allowed\n";
        return -1;
    }
    quantum = quantum_usecs;
    timer.it_value.tv_sec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_value.tv_usec = quantum_usecs;
    timer.it_interval.tv_usec = quantum_usecs;
    main_thread = {0, Running,};
    main_thread.quantum_counter = 1;
    thread_list->push_back(&main_thread);
    running_thread = &main_thread;
    to_be_deleted = nullptr;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        std::cerr << "system error: setitimer error\n";
    }
    return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    int id = get_available_id();
    if (id == -1){
        std::cerr << "thread library error: exceeded maximum number of threads\n";
        return -1;
    }
    if (entry_point == NULL) {
        std::cerr << "thread library error: entry point cannot be NULL\n";
        return -1;
    }
    char *stack = new char[STACK_SIZE];
    setup_thread(id, stack, entry_point);
    Thread *new_thread = new Thread{id, Ready, stack};
    new_thread->quantum_counter = 0;
    thread_list->push_back(new_thread);
    ready_list->push_back(new_thread);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return id;
}

int uthread_terminate(int tid) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    auto target_thread = std::find_if(thread_list->begin(), thread_list->end(),
                                      [&](const Thread *t) { return t->id == tid; });
    // if thread with this id does not exist in thread list
    if (target_thread == thread_list->end()) {
        std::cerr << "thread library error: thread not found\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
        //if thread is the main-thread
    else if (tid == 0) {
        auto it = thread_list->begin();
        Thread *deleted_thread = *it;
        while (it != thread_list->end()) // thread_list)?
        {
            if ((*it)->id != 0) {
                deleted_thread = *it;
                ready_list->remove(deleted_thread);
//                std::remove(ready_list->begin(), ready_list->end(),deleted_thread);
                it = thread_list->erase(it);
                delete[] deleted_thread->sp;
                delete deleted_thread;
            } else { it++; }
        }
//        thread_list->clear();
        ready_list->remove(&main_thread);
        thread_list->remove(&main_thread);

        delete ready_list;
        delete thread_list;
        fill_available_ids();
        exit(0);
    }

        //not main thread
    else {
        int current_id = running_thread->id;
        Thread *found_thread = *target_thread;
        int new_available_id = (found_thread)->id;
        ready_list->remove(found_thread);
        thread_list->remove(found_thread);
        //delete[] found_thread->sp;
        //delete found_thread;
        auto it = std::lower_bound(available_ids.begin(), available_ids.end(),
                                   new_available_id);
        available_ids.insert(it, new_available_id);
        to_be_deleted = found_thread;
        found_thread = NULL;
        if (current_id == tid) { //itself
            context_switch();
        }
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return 0;
    }
}

int uthread_block(int tid) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    if (tid == 0) {
        std::cerr << "thread library error: cannot block main thread\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    if (tid == running_thread->id) // blocking itself
    {
        running_thread->state = Blocked;
        running_thread->flag_blocked = 1;
        context_switch();
//        running_thread->quantum_counter += 1;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return 0;
    }
    auto target_thread = std::find_if(thread_list->begin(), thread_list->end(),
                                      [&](const Thread *t) { return t->id == tid; });

    if (target_thread != thread_list->end()) {
        Thread *found_thread = *target_thread;
        if (found_thread->state == Blocked && found_thread->flag_blocked ==1)
        {
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            return 0;
        }
        found_thread->state = Blocked;
        found_thread->flag_blocked = 1;
        ready_list->remove(found_thread);
    } else {
        std::cerr << "thread library error: thread not found\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}

int uthread_resume(int tid) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    auto target_thread = std::find_if(thread_list->begin(), thread_list->end
                                              (),
                                      [&](const Thread *t) { return t->id == tid; });

    if (target_thread != thread_list->end()) {
        Thread *found_thread = *target_thread;
        if (found_thread->state != Blocked) {
            sigprocmask(SIG_UNBLOCK, &set, NULL);
            return 0;
        }
        found_thread->flag_blocked = 0;
        if (found_thread->sleep == 0) {
            found_thread->state = Ready;
            ready_list->push_back(found_thread);
        }
    } else {
        std::cerr << "thread library error: thread not found\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}

int uthread_sleep(int num_quantums) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    if (num_quantums <= 0) {
        std::cerr << "thread library error: negative number of quantums is not allowed\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    running_thread->state = Blocked;
    running_thread->sleep = num_quantums;
    context_switch();
//    running_thread->quantum_counter += 1;
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}

int uthread_get_tid() {
    return running_thread->id;
}

int uthread_get_total_quantums() {
    return total_quantums;
}

int uthread_get_quantums(int tid) {
    sigprocmask(SIG_BLOCK, &set, NULL);
    auto target_thread = std::find_if(thread_list->begin(), thread_list->end
                                              (),
                                      [&](const Thread *t) { return t->id == tid; });

    if (target_thread != thread_list->end()) {
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return (*target_thread)->quantum_counter;
    } else {
        std::cerr << "thread library error: thread not found\n";
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
}

