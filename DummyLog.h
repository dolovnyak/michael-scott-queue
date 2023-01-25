#pragma once

#include <iostream>

static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
//static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

template<class T>
void debug(T t) {
    pthread_mutex_lock(&g_cout_mutex);
    std::cout << t << std::endl;
    pthread_mutex_unlock(&g_cout_mutex);
}

template<class T, class... Args>
void debug(T t, Args... args) {
    pthread_mutex_lock(&g_cout_mutex);
    std::cout << t;
    debug(args...);
    pthread_mutex_unlock(&g_cout_mutex);
}
