// sources http://www.bo-yang.net/2017/11/19/cpp-kill-detached-thread

#include <thread>
#include <string>
#include <iostream>
#include <unordered_map>
#include <unistd.h>
#include <cstdlib>
#include <pthread.h>
#include <sys/prctl.h>


using namespace std;


class Foo {
public:
    void sleep_for(const std::string &tname, int num)
    {
        prctl(PR_SET_NAME,tname.c_str(),0,0,0);
        sleep(num);
    }

    void start_thread(const std::string &tname)
    {
        std::thread thrd = std::thread(&Foo::sleep_for, this, tname, 3600);
        thrd.detach();
        tm_[tname] = std::move(thrd);
        std::cout << "Thread " << tname << " created:" << std::endl;
    }

    void stop_thread(const std::string &tname)
    {
        ThreadMap::const_iterator it = tm_.find(tname);
        if (it != tm_.end()) {
            it->second.std::thread::~thread(); // thread not killed
            tm_.erase(tname);
            std::cout << "Thread " << tname << " killed (should not be visible in ps command):" << std::endl;
        }
    }

private:
    typedef std::unordered_map<std::string, std::thread> ThreadMap;
    ThreadMap tm_;
};



void show_thread(const std::string &keyword)
{
    std::string cmd("ps -T | grep ");
    cmd += keyword;
    std::cout << std::endl << cmd << std::endl;
    system(cmd.c_str());
}

int main()
{
    Foo foo;
    std::string keyword("test_thread");
    std::string tname1 = keyword + "1";
    std::string tname2 = keyword + "2";

    // create and kill thread 1
    foo.start_thread(tname1);
    show_thread(keyword);
    foo.stop_thread(tname1);
    show_thread(keyword);

    // create and kill thread 2
    foo.start_thread(tname2);
    show_thread(keyword);
    foo.stop_thread(tname2);
    show_thread(keyword);

    return 0;
}
