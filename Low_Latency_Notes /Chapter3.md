## Low Latency in C++ 

### Understanding cache and memory access 
- it is common for data structures and algos that are sub-optimal on paper to outperform ones that are faster on paper, reason for this is higher cache and mem access costs for the optimal solution outweighting the time saved becasue of the reduced number of instructions the processor needs to execute 
- memory hierachy works such that cpu first checks register->l0 cache->l1 cache->l2 cache->memory->disk, higher is faster but smaller storage, and vice versa 
- we have to think carefully regarding cache and mem access patterns for the local algo, as well as globally, if we have a function with quick execution time but pollutes the cache, degrades overall performance as other componens will incur cache miss penalites

### Understanding how c++ features work under the hood 
- a lot of the higher level abstractions available in c++ improve ease of development, but may incur higher latency costs 
- for example, dynamic polymorphism, dynamic mem allocation, and exception handling are best avoided or used in a specific manner as they have a larger overhead 
- many traditional programming practices suggest the developer break everyuthing into numerous small functions, use OOP principles, smart pointers, etc, which are sensible for most applications but need be used carefuly in low latency contexts to avoid the overhead 


### Avoiding pitfals and leverage c++ features to minimize application latency 
1) Choosing Storage 
- local variable screated within a function are stored on the stack by defualt and the stack memory is also used to store function return values, assuming no large objects are created, the same rang eof stak storage space is reused a lot, resulting in great cache performance due to locality of reference 
- register variables are closest to cpu and fastest form of storage, extremely limited, compiler will try to use them for local variables that are used the most 
- static/global varaibles are inefficient from the prespective of cache performance becasue that memory cannot be reused for oher variables
- *volatile* keyword tells compiler to disable optimizations taht rely on the assumption that the variable value does not change without the compilers knowledg, only be used carefully in multithreaded use cases since it prevents optimizations such as storing vars in registers snad force flushing them to main memory from cache everytime the val changes 
- one example of c++ optimization technique taht leverages storage choice optimization is small string optimization (SSO), this attempts to use local storage for short strings if they are smller than a certain size, vs the slow dynamic mem allocation normally used 

2) Choosing data type
- c++ integer operations are super0fast as long as the size of the lragest register is larger than the integer size, integers smaller or larger than register size are sometimes slower, because the 
processor must use multiple registers for a single variable and apply carry-over logic for large integers 
- signed and unsigned integers are equally fast, but sometimes unsigned is a tiny bit faster as we dont have to check the sign bit 

3) Using casting and conversion operations
- converting btwn signed and unsigned integers is free, converting from smaller size to larger one takes a single clock cycle, larger to smaller is free 
- conversion btwn floats, doubles and long doubles is free, signed/unsigned to float/double takes a few cycles 
- floats to ints is expensive, 50-100 cycles, it conversions are on critical path, use special methods to avoid these expensive conversions such as inline assembly, or avoid converting altogether 
- convterting ptrs from one type to antoehr type is completely free, safe is another question 
- type casting a ptr to an object to a ptr of a diff object violates the rule that 2 ptrs of diff types cannot pt to same mem location
```c++
int main() {
    double x = 100; 
    const auto orig_x = x; 
    auto x_as_ui = (uint64_t *) (&x); 
    *x_as_ui |= 0x80000000000000; 
    printf("orig_x, x, &x, x_as_ui") // 100.00, -100.00, 0x7fff1e6b00d0, 0x7fff1e6b00d0 
}
```
- this shows type casting a ptr to be a diff object 
- casting is usually free except `dynamic_cast`, as this checks whether the conversion is valid using time-time type information, which is slow and can throw exceptions 

4) Optimizing numerical operations 
- double precision calculations take about the sme time as single-precision operations, adds are fast, mults little slower, div is slowest 
- int mult = 5 cycles, float mult = 8 cycles, int add = single cycle, float add = 2-5 cycles, float div & int div = 20-80 cycles 
- compilres will try to rewrite and reduce expressions to prefer faaster operations such as rewriting divs to be mults by reciprocal, mult & div by powers of 2 are faster because compiler rewrites them to be bit shift operations 
- mixing int and floats in expressions should be avoided because they force type conversions 

5) Optmizing boolean and bitwise operations 
- booll operarations such as logcail AND (&&) and OR(||) are exalauted such that for AND, if first operand is false, does not check second, and for OR, if first operand is true, does not check second 
- we can order the operands for AND by probability, lower to higher, and opposite for OR - this is called short circuiting 
- bitwise operations can also help speed up other cases of boolean expressions by treating each bit of an integer as a single boolean variable, adn then rewriting expressions involving compariosns of multiple booleans with bit-masking ops 
```c++ 
// market_state = uint64_t, PreOpen, Opening, Trading enum vals 0x100, 0x010, 0x001
if (market_state == PreOpen || market_state == Opening || market_state == Trading) {
    do_something(); 
    }
   
if (market_state & (PreOpen | Opening | Trading) { 
    do_something(); 
    } 
```

6) Initialzing, destroying, copying, and moving objects 
- constructors and destructors for developer defined classes should be light as possible, since they can be called without expectation, also allows compiler to inline these methods to improve performance 
- same with copy & move construcotrs, in cases where high levels of optimizations are req, we can delete the default and copy constructors to avoid unecessary copies being made 

7) Using references and ptrs
- accessing objects thru refs and ptrs is as efficient as direct access, only disadvantage is that these take up an extra register for the ptr, and the cost of extra deference instructions 
- ptr arithmetic is as fast as integer arithmetic except when computeing the difference btwn ptrs requires a division by the size of object 
- smart ptrs such as `unique_ptr`, `shared_ptr`, and `weak_ptr`, use the resource aquisition is initialization paradigm (RAII), extra cost with `shared_ptr` due to reference counting but generally add little overhead unless there are a lot 
- ptrs can prevent compiler optimizations due to pointer aliasing, while it may be obvious to the user, as compile time, the compiler cannot guarantee that 2 ptr vars in the code will nver pt to same mem address

8) Optimizing jumping and branching 
- instructions adnd ata are detched and decoded in stagtes, when there is a branch instruction, processor tries to predict which branch will be taken and fetches and decodes instructions from that branch 
- in the case of misprediction, takes 10+ cyclcers to detect, and then uses more cycles to fetch the correct branch instructions 
- `if-else` branching most common, avoid nesting these statements, try to structure such taht they are more predictable 
- avoid nesting loops 
- `switch` statements are branches with multiple jump targest, if label values are spread out, compiler treats switch statements as long sequence of if-else branching trees, assign case lebl values that increment by 1 and ascending order so they are implemented as jump tables, which are significantly faster 
- replacing branching with table lookups containing different output values in source code is a good optimization technique 
- *loop unrolling* duplicates the body of the loop multiple times in order to avoid the checks and branching that determines if a loop should continue, compiler will attempt to unroll loops if possible 
```c++ 
int a[5]; a[0] = 0; 
for (int i = 1; i < 5; i++)
    a[i] = a[i - 1] + 1; 
// unroll loop 
int a[5]; 
a[0] = 0; 
a[1] = a[0] + 1; a[2] = a[1] + 1; 
a[3] = a[2] + 1; a[4] = a[3] + 1; 
```
- compile time branching using an `if constexpr (condition-expression) {}` format can help by moving the overhead of brnaching to compile time only if the `condition-expression` can be something taht is evaluated at compile time (compile time polymorphism/template metaprogramming)
- it is also possible to provide the compiler with brach prediction ints in the source code since the developer has a better idea, but these do not usually have any difference when using modern processors 

9) Calling functions efficiently 
- think before creaitng an excessive number of functions, should only be created if there is enough re-usability to justify them
- the criteria for creating functions should be logical program flow and re-usability and not the length of the code, as calling functions is not free 
- class member and non-class member functions get assigned memory addresses in the order in which they are created, so it is generally a good idea to group together performance critical functions that clal ech other fequently or operate on the same datasetd 
- when writing performance-critical functions, it is important to place them in the same modyle where they are used if possible, doing so unlocks a lot of compiler optimizations, the most important is the ability to inline the function call
- using the *static* keyword to decalre a functin does the equivalent of putting it in ana nonymous namespace, which makes it lcoal to the translation unit it is used in
- specifiying WPO and LTO parameters for the compiler instructs it to threat the entire code bcae as a single module and enable compiler optimizations across modules, without these options optimizations ccur across functions in the same module, but not different modules 
- *macro expressions* are a preprocessor directive and are expanded even before compilation begins, elimintaes the overhead associated with calling and returning from functions at runime, however they have several disadvantages such as namespace collision, cryptic compilation errors, etc 
- *inline functions* are expanded at their usage during compilation and link times and eliminate the overhead associated with function calls 
- using template metaprogramming it is possible to move a lot of the computation load from runtime to compile time, however it is clumsy and annoying to code (lol) 
- calling a function through a function ptr has a larger overhead than directly calling the funciton, for ex) if the ptr changes, then the compiler cnanot predict whihc function will be called and cannot prefetch the instructions and data 
- try to avoid `std::function, std::bind` as these constructs can perform virtual function calls and invoke dynamic mem allocations under the hood 
- for primitive types, passing parameters by value is super-efficient, for composite types that are function parameters, the preferred way of passing them would be *const reference*, the constsness means that hte object cannot be modified and allows the compiler to apply optimizations based on that and the reference allows the compiler to possibly inline the object itself 
- functions taht return primitive types are very efficient, returning composite types is much more inefficient and can lead to a cpuple of copies being created, which is sub-optimal especially if these are large and/or have slow copy constructors and assignment operators 
- when the compiler can apply *Return Value Optimization (RVO)* it can elimate the temporary copy created and write the result to the callers object directly
- the optimal way to return a composite type is to have the caller create an object of that type ans pass it to the function using a reference of a ptr for the function to modify
```c++
struct LargeClass {
    int i; 
    char c; 
    double d;
};

auto rvoExample(int i, char c, double d) {
    return LargeClass{i, c, d}; 
}

int main() {
    LargeClassExample lc_obj = rvoExample(10, 'c', 3.14); 
}
```
- with RVO, instead of creting a temporary LargeClass object inside rvoExample() and then copying it into the LargeClass lc_obj in main, the rvoExample() func taht directly update lc_obj and avoid the temporary object and copy
- avoid recursive functions, instead opt for looping, as they take up a lot of stack space and can even cause a stack overflow 
- also causes a lot of cache misses due to the new temporary mem areas and makes predicting the return address difficult and inefficient 
- *bitfields* are structs where the dev controls the number of bits assigned to each member, this makes the data as compact as possible and improves cache performance for many objects 

10) Using runtime polymorphism 
- elegant solutio wehnt eh member function that needs to be called will be determined at runtime instead of compile time, virtual functions are the key to implementing, but have additional overhead 
- usually the compiler cannot predict which version of a virtual function will be called, which causes branch mispredictions, it is possible for the compiler to determine the virtual function implementation at compile time using *devirtualization*
- be careful when using inheritance, as the child classes inherit every member of the base class, which can lead to large classes and poor cache performance, instead we can use the *Composition* paradigm, where the child class has members of different parent class types instead of inherting from them 
```c++
struct Order {int id; double price;}; 

class InheritanceOrderBook : public std::vector<Order> {} 

class CompositionOrderBook {
std::vector<Order> orders_; 
public: 
    auto size() const noexcept {
        return orders_.size(); 
    }
};

int main() {
    InheritanceOrderBook i_book; 
    CompositionOrderBook c_book; 
    printf ("InheritanceOrderBook::size() : %lu CompositionOrderbook: %lu\n", i_book.size(), c_book.size()); 
}
```
- we implement a `size()` method in `CompositionOrderBook` which calls the `size()` method on the `std::vector` object, while `InheritanceOrderBook` inherits it directly from `std::vector`
```c++
class RuntimeExample {
public:
  virtual void placeOrder() {
    printf("RuntimeExample::placeOrder()\n");
  }
};

class SpecificRuntimeExample : public RuntimeExample {
public:
  void placeOrder() override {
    printf("SpecificRuntimeExample::placeOrder()\n");
  }
};

template<typename actual_type>
class CRTPExample {
public:
  void placeOrder() {
    static_cast<actual_type *>(this)->actualPlaceOrder();
  }

  void actualPlaceOrder() {
    printf("CRTPExample::actualPlaceOrder()\n");
  }
};

class SpecificCRTPExample : public CRTPExample<SpecificCRTPExample> {
public:
  void actualPlaceOrder() {
    printf("SpecificCRTPExample::actualPlaceOrder()\n");
  }
};

int main(int, char **) {
  RuntimeExample *runtime_example = new SpecificRuntimeExample();
  runtime_example->placeOrder();

  CRTPExample <SpecificCRTPExample> crtp_example;
  crtp_example.placeOrder();

  return 0;
}
```
- the output of this is as follows: 
`SpecificRuntimeExample :: placeOrder() 
SpcificCRTPExample :: actualPlaceOrder(); `

#### Handling Exceptions  
- when it comes to low-lateny applications, it is imporantt to evaluate the use of exception handling, since there can be overheads even it the exception is not raised 
- with nested functions, exceptions need to be propagates all the way up to the top-most caller function, and each stack frame needs to be cleaned up, this is known as stack unwinding, and requires the exception handler to track all the information it needs to walk backward during an exception 

#### Accessing cache and memory 
- variales that are aligned, int that they ar eplaced at mem locations that are multiples of the size of the variabel, are accessed most efficiently, the term *word size* described the number of bits read by and processed by processors, for most is either 32 or 64 bits
- this means the processor can read a variable up to the word size in a single operation, if the variable is aligned in mem, makes it easlier to get into the required register 
- compiler will take care of automatically aligning variables, this includes adding padding in bteween member variables in a class or struct to keep those variables aligned 
- when adding mem variables to structs where we have a lot of objects, it is imporatnt to order the members so there is minmal padding 
```c++
#include <cstdio>
#include <cstdint>
#include <cstddef>

struct PoorlyAlignedData {
  char c;
  uint16_t u;
  double d;
  int16_t i;
};

struct WellAlignedData {
  double d;
  uint16_t u;
  int16_t i;
  char c;
};

#pragma pack(push, 1) // eliminates padding 
struct PackedData {
  double d;
  uint16_t u;
  int16_t i;
  char c;
};
#pragma pack(pop)

int main() {
  printf("PoorlyAlignedData c:%lu u:%lu d:%lu i:%lu size:%lu\n",
         offsetof(struct PoorlyAlignedData,c), offsetof(struct PoorlyAlignedData,u), offsetof(struct PoorlyAlignedData,d), offsetof(struct PoorlyAlignedData,i), sizeof(PoorlyAlignedData));
  printf("WellAlignedData d:%lu u:%lu i:%lu c:%lu size:%lu\n",
         offsetof(struct WellAlignedData,d), offsetof(struct WellAlignedData,u), offsetof(struct WellAlignedData,i), offsetof(struct WellAlignedData,c), sizeof(WellAlignedData));
  printf("PackedData d:%lu u:%lu i:%lu c:%lu size:%lu\n",
         offsetof(struct PackedData,d), offsetof(struct PackedData,u), offsetof(struct PackedData,i), offsetof(struct PackedData,c), sizeof(PackedData));
}
```
- output is as follows: 
`PoorlyAlignedData c:0 u:2 i:16 size:24
WellAlignedData d:0 u:8 i:10 c:12 size:16
PackedData d:0 u:8 c:12 size:13`

#### Minimzing Branching 
- here is an example of how to convert a code block that uses branching to transform it to avoid branching
```c++
#include <cstdio>
#include <cstdint>
#include <cstdlib>

enum class Side : int16_t { BUY = 1, SELL = -1 };

int main() {
  const auto fill_side = (rand() % 2 ? Side::BUY : Side::SELL);
  const int fill_qty = 10;
  printf("fill_side:%s fill_qty:%d.\n", (fill_side == Side::BUY ? "BUY" : (fill_side == Side::SELL ? "SELL" : "INVALID")), fill_qty);

  { // with branching
    int last_buy_qty = 0, last_sell_qty = 0, position = 0;

    if (fill_side == Side::BUY) { position += fill_qty; last_buy_qty = fill_qty;
    } else if (fill_side == Side::SELL) { position -= fill_qty; last_sell_qty = fill_qty; }

    printf("With branching - position:%d last-buy:%d last-sell:%d.\n", position, last_buy_qty, last_sell_qty);
  }

  { // without branching
    int last_qty[3] = {0, 0, 0}, position = 0;

    auto sideToInt = [](Side side) noexcept { return static_cast<int16_t>(side); };

    const auto int_fill_side = sideToInt(fill_side);
    position += int_fill_side * fill_qty;
    last_qty[int_fill_side + 1] = fill_qty;

    printf("Without branching - position:%d last-buy:%d last-sell:%d.\n", position, last_qty[sideToInt(Side::BUY) + 1], last_qty[sideToInt(Side::SELL) + 1]);
  }
}
```
- over here to avoid branching we use a lambda function to calculate the int value of the enum, and an array to hold qty for buy and sell, index into array using the calculated value

#### Reordering and scheduling instructions
- the cmpiler can take advantage of advanced processors by re-ordering instructions in such a way that parallel processing can happen at the instruction, memory, and thread levels 
```c++ 
x = a + b + c + d + e + f; 
// this expression has a data dependenct and would be executed sequentially as follows, costs 5 clock cycles 
x = a + b; 
x = x + c; 
x = x + d; 
x = x + e; 
x = x + f; 

// we can reorder as follows: 
x = a + b; p = c + d; 
q = e + f; x = x + p; 
x = x + q;
// reduced to 3 cycles, x = a + b and p = c + d are performed concurrently as the data is independent of each other 
```

#### Vectorization 
- modern processors can use vector registers to perform multiple calculations on multiple pieces of data in parallel
- for ex) the AVX2 instruction set has 256 bit vector registers and can support a higher degree of vectorized operations 
```c++
#include <cstddef>

int main() {
  const size_t size = 1024;
  [[maybe_unused]] float x[size], a[size], b[size];

  // no vectorization
  for (size_t i = 0; i < size; ++i) {
    x[i] = a[i] + b[i];
  }

  // vectorization
  for (size_t i = 0; i < size; i += 4) {
    x[i] = a[i] + b[i];
    x[i + 1] = a[i + 1] + b[i + 1];
    x[i + 2] = a[i + 2] + b[i + 2];
    x[i + 3] = a[i + 3] + b[i + 3];
  }
}
```
- we can hold 4 byte float values simultaneously and perform 4 additions at a time 

#### Strength Reduction 
- compiler optimization where complex operations that are quite expensive are replaced by instructions that are simpler and chaeper to improve performance 
- we can replace division by some value with multiplication by the reciprocal of that value, and multiplcation by a loop index with an addition operation 
```c++
#include <cstdint>

int main() {
  const auto price = 10.125; // prices are like: 10.125, 10.130, 10.135...
  constexpr auto min_price_increment = 0.005;
  [[maybe_unused]] int64_t int_price = 0;

  // no strength reduction
  int_price = price / min_price_increment;

  // strength reduction
  constexpr auto inv_min_price_increment = 1 / min_price_increment;
  int_price = price * inv_min_price_increment;
}
```





