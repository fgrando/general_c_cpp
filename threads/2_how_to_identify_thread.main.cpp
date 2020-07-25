// sources: http://www.bo-yang.net/2017/11/19/cpp-kill-detached-thread

#include <mutex>
#include <iostream>
#include <chrono>
#include <cstring>
#include <thread>
#include <pthread.h>

// compile with: g++ -Wall -std=c++11 <this-file>  -pthread -lpthread

std::mutex iomutex;
void f(int num)
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::lock_guard<std::mutex> lk(iomutex);
    std::cout << "Thread " << num << " pthread_t " << pthread_self() << std::endl;
}

int main()
{
    std::thread t1(f, 1), t2(f, 2);

    std::cout << "\nVerify that all ids are be equal (nice)\n";

    std::cout << "Thread 1 thread id " << t1.get_id() << std::endl;
    std::cout << "Thread 2 thread id " << t2.get_id() << std::endl;

    std::cout << "Thread 1 native handle " << t1.native_handle() << std::endl;
    std::cout << "Thread 2 native handle " << t2.native_handle() << std::endl;



    t1.join(); t2.join();

    std::cout << "\n\nNow look again after the join:\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::thread t3(f, 3), t4(f, 4);
    t3.join(); t4.join();

    std::cout << "Thread 3 thread id " << t3.get_id() << std::endl;
    std::cout << "Thread 4 thread id " << t4.get_id() << std::endl;

    std::cout << "Thread 3 native handle " << t3.native_handle() << std::endl;
    std::cout << "Thread 4 native handle " << t4.native_handle() << std::endl;


    std::cout << "how to call pthread_cancel if we dont have a valid id?\n")

    return 0;
}
