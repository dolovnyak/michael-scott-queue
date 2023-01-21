#pragma once

#include <iostream>

static int GenerateId() {
    static pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&id_mutex);
    static int id = 0;
    ++id;
    pthread_mutex_unlock(&id_mutex);
    return id;
}

static pthread_mutex_t g_cout_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

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
