# michael-scott-queue

This is Michael Scott's queue implementation using my implementation of danger pointers.

Tested with a pthread sanitizer on a test where n producers are written to the queue and m consumers are read from it, and the threads are turned off after some iterations and new ones are create. fully implemented on compare and set (CAS) operations and optimized with a more flexible memory model.

TODO: memory model optimization is not fully completed, this does not affect the correctness of the queue, but it affects its performance.

Build and run example
-------
```
cmake -S . -B build
make -C build

./michael-scott-queue-example
```

In the example/main.cpp, threads are deleted and new ones are added, but the thread structures aren't cleaned, so the example may crash due to an error that the maximum number of threads has been exceeded, in this case g_consumer_iterations_before_die should be increased.

Also, this example runs this queue along with the boost queue, so you can compare their performance.

If you compile this library with the MSQ_DEBUG flag, various events will be logged to the console under a common mutex, which will greatly slow down the queue
