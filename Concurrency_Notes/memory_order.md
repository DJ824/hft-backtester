# Memory model synchronization modes 

- atomic variables are primarily used to synchronize shared mem accesses between threads
- typically, one thread creates data, then stores to an atomic, other threads read from this atomic, and when the expected value is seen, the data the other thread was creating 
is going to be complete and visible in this thread
- the different memory model modes are used to indicate how strong this data sharing bond is beteween threads 
- each atomic class has a `load()` and `store()` operation which is utilized to perform assignments,  
```c++ 
atomic_var1.store(atomic_var2.load()); 
var1 = var2; 
```
- these operations also hae a second optional paramenter which is used to specifiy the memory model mode to use for synchronization 

### Sequentially Consistent 
- default mode used when none is specified, and it is the most restrictive 
- can be explicityl specified via `std::memory_order_seq_cst`, provides the same restrictions and limitation to moving loads around that sequentail programmers are inherently familiar with, except is applies across all threads 
```
 -Thread 1-       -Thread 2-
 y = 1            if (x.load() == 2)
 x.store (2);        assert (y == 1)
```
- although x and y are unrealated vars, the mem model specified that the assert cannot fail 
- the store to `y` happens-before the store to `x` in thread 1 
- if the load of `x` in thread 2 gets the results of the store that happened in thread 1, 
it must see all operations that happened before the store in thread 1, even unrealted ones 
- this means the optimizer is not free to reorder the 2 stores in thread 1, since thread 2 must see the store to `y` as well 
- this applies to loads as well 
```
 a = 0
             y = 0
             b = 1
 -Thread 1-              -Thread 2-
 x = a.load()            while (y.load() != b)
 y.store (b)                
 while (a.load() == x)   a.store(1)
 ```
- thread 1 loads the value of `a` into `x`, stores `b` into `y`, and waits in a loop while `a` remains equal to `x`
- thread 2 waits in a loop until `y` changes to `b`(1), once `y` changes, stores `1` into a 
- synchronization works as follows: 
1) thread 1 signals thread 2 by changing `y` to `1` 
2) thread 2 waits for this signal, then proceeds to change `a` to `1` 
3) thread 1 is waiting for a to change, which will happen when thread 2 completes its task
- in a single threaded context, the loop `while (a.load() == x` might be optimized to an infinite loop because `a` and `x` dont change within the loop
- in multithreaded context with atomics, this optimization cannot be applied, the compiler and cpu must ensure that 
1) load of a happens on every iteration of the loop 
2) the comparison with x is performed each time  
- this is very important because thread 2 might change the value of `a` at anytime, if the load and comparison didnt happne on each iteration
thread 1 might miss the change made by thread 2 and get stuck in an infinite loop

### Relaxed 
- the opposide approach is `std::memory_order_relaxed`, this allows for less synchronization by removing the happends before restrictions 
- we can perform various optimizations such as dead store removal and commoning 
```
-Thread 1-
y.store (20, memory_order_relaxed)
x.store (10, memory_order_relaxed)

-Thread 2-
if (x.load (memory_order_relaxed) == 10)
  {
    assert (y.load(memory_order_relaxed) == 20) /* assert A */
    y.store (10, memory_order_relaxed)
  }

-Thread 3-
if (y.load (memory_order_relaxed) == 10)
  assert (x.load(memory_order_relaxed) == 10) /* assert B */
```
- since threads dont need to bw synchronized across the system, either assert in this example can fail
- without any happens-before edges, no thread can count on a specific ordering from another thread this can lead to some execpected results is one isnt very careful
- the only ordering that is imposed is once a value from thread 1 is observed in thread 2, thread 2 cannot see an earlier value for that var from thread 1 
```
-Thread 1-
x.store (1, memory_order_relaxed)
x.store (2, memory_order_relaxed)

-Thread 2-
y = x.load (memory_order_relaxed)
z = x.load (memory_order_relaxed)
assert (y <= z)
```
- this assert cannot fail, once the store of 2 is seen by thread 2, it can no longer see the value 1 
- this prevents coalescing relaxed loads of one var across realxed loads of a different reference that might alias 
- relaxed stores from one thread are seen by relaxed loads in another thread within a raesonable amt of time 
- on non-cache-coherent architectures, relaxed operations need to flush the cache 
- relaxed mode is most commonly used when the programmer simply wants a var to be atomic in nature rather than using it to synchronize threads for other shared mem data 

### Acquire/Release 
- hybrid of first 2, similar to sequentailly consistent, except it only appliesa happens-before relationship to dependent vars, this allows for a relaxing of the synchronization requires between independent reads of independent writes 
```
-Thread 1-
 y.store (20, memory_order_release);

 -Thread 2-
 x.store (10, memory_order_release);

 -Thread 3-
 assert (y.load (memory_order_acquire) == 20 && x.load (memory_order_acquire) == 0)

 -Thread 4-
 assert (y.load (memory_order_acquire) == 0 && x.load (memory_order_acquire) == 10)
```
- both these asserts can pass since there is no ordering imposed between the stores in thread 1 and thread 2 
- if we used seq_consistent, one of the stores must happen-before the other, values are synchronized between threads, and if one assert passes, the other assert must therefore fail 
- the loads in threads 3 and 4 can see different combinations of the stores from threads 1 and 2 
- thread 3 might see y's new val (20), but not x's new val(still 0)
- thread 4 might see x's new val (10), but not y's new val (still 0)

### Consume 
`std::memory_order_consume` is a refinement in the release/acquire mem model that relaxes the requirements slightly by removing the happens before ordering on non-dependent shared vars as well 
```
// n and m = 0; 
 -Thread 1-
 n = 1
 m = 1
 p.store (&n, memory_order_release)

 -Thread 2-
 t = p.load (memory_order_acquire);
 assert( *t == 1 && m == 1 );

 -Thread 3-
 t = p.load (memory_order_consume);
 assert( *t == 1 && m == 1 );
```
- thread 1 sets n to 1, m to 1, then stores the address of n in p with release semantics 
- thread 2 loads p with aquire semantics, then asserts *t is 1 and m is 1 
- thread 3 loads p with consume semantics, then asserts *t is 1 and m is 1 
- memory_order_consume creates a dependency ordered before relationship, ensuring visibility only for operations that are data dependent on the loaded value 
- the assert in thread 2 will always pass because of the acquire load synchronizing with the release store 
- this creates a happens before realtionship btwen all operations in thread 1 and subsequent operations in thread 2 
- thread 2 is guaranteed to see both n = 1 and m = 1 
- in thread 3, *t == 1 is guaranteed to be true because it depends directly on the loaded ptr val 
- however, m = 1 is not data dependent on the loaded ptr, thread 3 may see the old value of m(0)), evenn though it sees the new val of  

