# michael-scott-queue

This is michael scott's queue using my implementation of hazard pointers.

Tested with a pthread sanitizer on a test where n producers are written to the queue and m consumers are read from it, and the threads are turned off after some iterations and new ones are create. fully implemented on compare and set (CAS) operations and optimized with a more flexible memory model.
