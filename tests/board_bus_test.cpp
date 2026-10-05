#include "board_bus.h"
#include <thread>
#include <cassert>
int main(){int count=0;auto f=[&]{for(int i=0;i<100000;++i){board_bus::Guard a;board_bus::Guard b;++count;}};std::thread a(f),b(f);a.join();b.join();assert(count==200000);}
