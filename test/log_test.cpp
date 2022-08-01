#include "log.h"
#include <unistd.h>
#include <iostream>
using namespace std;

void* producer(void* arg){
    for(int i = 0; i < 100; i++){
        LOG_DEBUG("[%d pid:%lld msg:log test message!]", i, pthread_self());
        usleep(500);
    }
    return NULL;
}


int main(){
    Log::init(".", "log_test", 0, 10000);
    //cout << Log::test(10, 20) << endl;

    pthread_t pt[10];
    for(int i = 0; i < 50; i++){
        pthread_create(pt+i, NULL, producer, NULL);
    }
    for(int i = 0; i < 50; i++){
        pthread_join(pt[i], NULL);
    }
    return 0;
}