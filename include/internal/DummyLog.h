#pragma once

#include <iostream>

#ifdef __APPLE__
static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
#elif __linux__
static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#endif

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


#ifndef MS_DEBUG
#define LOG_DEBUG(...)          debug(__VA_ARGS__);
#else
#define LOG_DEBUG(...)
#endif