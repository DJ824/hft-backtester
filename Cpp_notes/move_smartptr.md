## Introduction to smart ptrs and move semantics

- consider a function in which we dynamically allocate a val 
```c++
void someFunction()
{
    Resource* ptr = new Resource(); // Resource is a struct or class

    // do stuff with ptr here

    delete ptr;
}
```

- it is fairly easy to forget to deallocate ptr, even if you do remember to delete ptr, the function can return early wihtout the ptr being deleted 

```c++
#include <iostream>

void someFunction()
{
    Resource* ptr = new Resource();

    int x;
    std::cout << "Enter an integer: ";
    std::cin >> x;

    if (x == 0)
        return; // the function returns early, and ptr won’t be deleted!
        
    // do stuff with ptr here

    delete ptr;
}
```

- one of the best things about classes is that they contain destructors that automatically get executed when a object of the class goes out of scope 
- if you allocate mem in the construcotr, you can deallocate in the destrucotr, and be guaranteed that the mem will be deallocated when teh class object is destroyed 
- this is the heart of RAII programming 
- we can create a class that automatically takes care of the ptr deallocating for us 

```c++
#include <iostream>

template <typename T>
class Auto_ptr1
{
	T* m_ptr {};
public:
	// Pass in a pointer to "own" via the constructor
	Auto_ptr1(T* ptr=nullptr)
		:m_ptr(ptr)
	{
	}

	// The destructor will make sure it gets deallocated
	~Auto_ptr1()
	{
		delete m_ptr;
	}

	// Overload dereference and operator-> so we can use Auto_ptr1 like m_ptr.
	T& operator*() const { return *m_ptr; }
	T* operator->() const { return m_ptr; }
};

// A sample class to prove the above works
class Resource
{
public:
    Resource() { std::cout << "Resource acquired\n"; }
    ~Resource() { std::cout << "Resource destroyed\n"; }
};

int main()
{
	Auto_ptr1<Resource> res(new Resource()); // Note the allocation of memory here

        // ... but no explicit delete needed

	// Also note that we use <Resource>, not <Resource*>
        // This is because we've defined m_ptr to have type T* (not T)

	return 0;
} // res goes out of scope here, and destroys the allocated Resource for us
```
- the above prints 
`Resource acquired` 
`Resource destroyed` 
- first we dynaically create a resource and pass it as a paramter to our templated auto_ptr1 class 
- from that pt forward, the auto_ptr1 variable res owns taht resouce object (auto_ptr1 has a composition relationship with m_ptr) 
- res is declared as local val, has block scope and will be destroyed when, the autoptr destructor will be called, and delete the m_ptr 
- as long as auto_ptr1 is defined as a local var, the resource will be guaranteed to by destroyed at the end of the block it is decalred in, regardless of how the function terminates 
- such a class is called a smart ptr, a smart ptr is a composition class that is designed to manage dynamically allocated memory and ensure that memory gets deleted when the smart ptr object goes out of scope 
- however, we also needa  move and copy constructor, otherwise if we do something like this: 
```c++ 
int main()
{
	Auto_ptr1<Resource> res1(new Resource());
	Auto_ptr1<Resource> res2(res1); // Alternatively, don't initialize res2 and then assign res2 = res1;

	return 0;
}
```
- this will print
  Resource acquired
  Resource destroyed
  Resource destroyed
- the c++ provided copy constructor does shallow copies, when we initialzie res2 with res1, both auto_ptr1 vars are pointing to the same resource, 
- when res2 goes out of scope, it deletes the resource, leaving res1 with a danlging ptr, and when res1 does to delete its resource (which has already been deleted by res2) it will result in a crash 
- thus we need to make our own copy/assignment operations, however, we cant return by reference, as the local auto_ptr1 will be destroyed at the end of the function, and the caller will be left with a dangling reference 
- we could return pointer r as a resource*, but this defeats the purpose of having the ptr being automatically deleted
- one solution is to instead of having our copy construcotr and assignment operator copy the ptr, we instead transfer/move owenershi of the ptr from the source to the destination object 
```c++
#include <iostream>

template <typename T>
class Auto_ptr2
{
	T* m_ptr {};
public:
	Auto_ptr2(T* ptr=nullptr)
		:m_ptr(ptr)
	{
	}

	~Auto_ptr2()
	{
		delete m_ptr;
	}

	// A copy constructor that implements move semantics
	Auto_ptr2(Auto_ptr2& a) // note: not const
	{
		// We don't need to delete m_ptr here.  This constructor is only called when we're creating a new object, and m_ptr can't be set prior to this.
		m_ptr = a.m_ptr; // transfer our dumb pointer from the source to our local object
		a.m_ptr = nullptr; // make sure the source no longer owns the pointer
	}

	// An assignment operator that implements move semantics
	Auto_ptr2& operator=(Auto_ptr2& a) // note: not const
	{
		if (&a == this)
			return *this;

		delete m_ptr; // make sure we deallocate any pointer the destination is already holding first
		m_ptr = a.m_ptr; // then transfer our dumb pointer from the source to the local object
		a.m_ptr = nullptr; // make sure the source no longer owns the pointer
		return *this;
	}

	T& operator*() const { return *m_ptr; }
	T* operator->() const { return m_ptr; }
	bool isNull() const { return m_ptr == nullptr; }
};

class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

int main()
{
	Auto_ptr2<Resource> res1(new Resource());
	Auto_ptr2<Resource> res2; // Start as nullptr

	std::cout << "res1 is " << (res1.isNull() ? "null\n" : "not null\n");
	std::cout << "res2 is " << (res2.isNull() ? "null\n" : "not null\n");

	res2 = res1; // res2 assumes ownership, res1 is set to null

	std::cout << "Ownership transferred\n";

	std::cout << "res1 is " << (res1.isNull() ? "null\n" : "not null\n");
	std::cout << "res2 is " << (res2.isNull() ? "null\n" : "not null\n");

	return 0;
}
```
Resource acquired
res1 is not null
res2 is null
Ownership transferred
res1 is null
res2 is not null
Resource destroyed

- as we see with the output, after setting res2 = res1, res2 assumes ownershpi, and res1 is set to null 

## R-value references 
l-value: 
- represents an object that has an identifable locaiton in memory (address) 
- cna appear on the left side of an assignment operator 
- ex) variables, array elements, dereferened ptrs 

r-value: 
- represents a temporary value or an object thats about to be destroyed 
- an only appear on right side of an assignment operator 
- ex) literals, temp objects, result of expressions 
```c++
int x = 5;  // x is an l-value, 5 is an r-value
int y = x;  // x is an l-value (used as an r-value here), y is an l-value
x + y;      // This entire expression is an r-value
```

l-value refrence: 
- can only be initialized with modifiable l-values 
- l-val refrences to const objects can be initiazlied with modifiable and non-modifiable l-val/r-vals alike, but those vals cant be modified 
- declared using a single ampersand 
- bind to l-vals (named objects with a persistent addres) 
- cannot bind to r-vals without const 

r-value reference: 
- declared using double ampersand 
- bind to r-val (temporaries or objects about to be destroyed) 
- enable move semantics and perfect forwarding
- extend the lifespan of the object they are initialized with to the lifespan of the r-val refrence 
- non const r-val references allow you to modify the r-val 

```c++
#include <iostream>
#include <string>

void process(std::string& lref) {
    std::cout << "L-value reference: " << lref << std::endl;
}

void process(std::string&& rref) {
    std::cout << "R-value reference: " << rref << std::endl;
}

int main() {
    std::string str = "Hello";
    
    process(str);  // Calls process(std::string&)
    process(std::string("World"));  // Calls process(std::string&&)
    process(std::move(str));  // Calls process(std::string&&)

    return 0;
}
```
- l-val referneces are used to aliasing existing objects 
- r-val references are used to implement move semantics and perfect forwarding 
- std::move is used to convert an l-val to an r-val, enabling move operations 
```c++
#include <iostream>

class Fraction
{
private:
	int m_numerator { 0 };
	int m_denominator { 1 };

public:
	Fraction(int numerator = 0, int denominator = 1) :
		m_numerator{ numerator }, m_denominator{ denominator }
	{
	}

	friend std::ostream& operator<<(std::ostream& out, const Fraction& f1)
	{
		out << f1.m_numerator << '/' << f1.m_denominator;
		return out;
	}
};

int main()
{
	auto&& rref{ Fraction{ 3, 5 } }; // r-value reference to temporary Fraction

	// f1 of operator<< binds to the temporary, no copies are created.
	std::cout << rref << '\n';

	return 0;
} // rref (and the temporary Fraction) goes out of scope here
```
- as an anonymous object, Fraction(3,5) would normally go out of scope at the end of the expression in which it is defined
- however, since we're initializing an r-val refernce with it its duration is extended until the end of the block, we cna then use that r-val reference to print 
```c++
#include <iostream>

int main()
{
    int&& rref{ 5 }; // because we're initializing an r-value reference with a literal, a temporary with value 5 is created here
    rref = 10;
    std::cout << rref << '\n';

    return 0;
}
```
prints 10 
- when initialzing an r-val reference with a literla, a temproy object is constructed from the literal so that the refernce is referencing a temp object, not a literal value 

R value references are more often used as function parameters, this is most useful for funcition overloads when you want to have different behaviour for l-val and r-val arguements 
```c++
#include <iostream>

void fun(const int& lref) // l-value arguments will select this function
{
	std::cout << "l-value reference to const: " << lref << '\n';
}

void fun(int&& rref) // r-value arguments will select this function
{
	std::cout << "r-value reference: " << rref << '\n';
}

int main()
{
	int x{ 5 };
	fun(x); // l-value argument calls l-value version of function
	fun(5); // r-value argument calls r-value version of function

	return 0;
}
```
prints: 
l-value reference to const: 5
r-value reference: 5

- as we see here, when passed an l-alue, the overloaded function resolved to the version with the l-val reference, when passed an r-val, the overloaded function resolved to the r-val reference

R value refernece variables are l-values 
```c++
int&& ref{ 5 };
fun(ref);
```
- the above will call `fun(const int&)` alrhough variable ref has type int&&, when used in an expression it is an l-alue, the type of an object and its value category are independent 
- you already know that literal 5 is an r-val of type int and int x is an lval of type int, similarly, int&&ref is an lval of type int&& 
- you should almost never return an r-val refrence, for the same reasion you should almost never return an l-val reference, in most cases, youll end up returing a hanging reference when the referenced object goes out of scope at the end of the function 

```c++
int main()
{
	int x{};

	// l-value references
	int& ref1{ x }; // A
	int& ref2{ 5 }; // B

	const int& ref3{ x }; // C
	const int& ref4{ 5 }; // D

	// r-value references
	int&& ref5{ x }; // E
	int&& ref6{ 5 }; // F

	const int&& ref7{ x }; // G
	const int&& ref8{ 5 }; // H

	return 0;
}
```
B, E and G wont compile 
- non-const l-val references cna only bind to non-const l-values 
- const l-value references can bind to non-const l-vals, const l-vals, and r-vals 
- r-val references can only bind to r-vals 
x is a non const l-val, so we can bind a non-const l-val reference (A) and a const l-val reference (C) to it, we cannot bind a non-const l-val referenece to an r-val (B)
5 is an r-val, so we cna bind a const l-val reference (D) and r-val reference (F/H) to it, we cnanot bind an r-val reference to an l-val(E/G)

### Move constructors and move assignment 

- copy constructors are used to initialize a class by aking a copy of an object of the smae class, copy assignemnt is used to copy one class object to antoher existing class object 
- by default, c++ provides a copy construcotr and copy assignemnt operator if one is not explicitly provided, these compiler provided funtions do shallow copies, which may caus eproblems for calsees that allocate dynamic memory 
```c++
#include <iostream>

template<typename T>
class Auto_ptr3
{
	T* m_ptr {};
public:
	Auto_ptr3(T* ptr = nullptr)
		: m_ptr { ptr }
	{
	}

	~Auto_ptr3()
	{
		delete m_ptr;
	}

	// Copy constructor
	// Do deep copy of a.m_ptr to m_ptr
	Auto_ptr3(const Auto_ptr3& a)
	{
		m_ptr = new T;
		*m_ptr = *a.m_ptr;
	}

	// Copy assignment
	// Do deep copy of a.m_ptr to m_ptr
	Auto_ptr3& operator=(const Auto_ptr3& a)
	{
		// Self-assignment detection
		if (&a == this)
			return *this;

		// Release any resource we're holding
		delete m_ptr;

		// Copy the resource
		m_ptr = new T;
		*m_ptr = *a.m_ptr;

		return *this;
	}

	T& operator*() const { return *m_ptr; }
	T* operator->() const { return m_ptr; }
	bool isNull() const { return m_ptr == nullptr; }
};

class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

Auto_ptr3<Resource> generateResource()
{
	Auto_ptr3<Resource> res{new Resource};
	return res; // this return value will invoke the copy constructor
}

int main()
{
	Auto_ptr3<Resource> mainres;
	mainres = generateResource(); // this assignment will invoke the copy assignment

	return 0;
}
```
prints:
Resource acquired
Resource acquired
Resource destroyed
Resource acquired
Resource destroyed
Resource destroyed
- in the above, we use `generateResource()` to create a smart ptr enapsulated resource, which is then passed back to function `main()`, which then assigned that to an existing auto_ptr3 object
1) inside `generateResource()`, local variable res is created and initialzied with a dynamically allocated Resource, which causes the first "Resource Acquired" 
2) res is returned back to `main()` by calue, we return by value here because res is a local vairables -- it cant be returned by address or reference because res will be destroyed when `generateResouce()` end, so res is copy constructed into a temporary object, this does a deep copy, so a new Resource is allocated here, which causes the second "Resource acquired" 
3) Res goes out of scope, destroying the originally created Resource, which causes the first "Resource destroyed" 
4) The temp object is assigned to mainres by copy assignment, which does a deep copy allocating a new Resouce and "Resource acquired" 
5) assignment expression ends, the temp object goes out of scopre and is destroyed, causeing a "resouce destroyed" 
6) at the end of `main()`, mainres goes out of scope, and our final "Resouce destroyed" is displayed 
- basically, we call the copy construcotr once to copy construct res to a temporary, and copy assignemnt once to copy the temp into main res, we end up allocating and destroying 3 seperate objects in total 

#### Move constructors and move assignment 
- while the goal of the copy constructor and copy asisgnment is to make a copy of one object to another, the goal of the move constructor and move assignment is to move owenrship of the resources from one object to antoehr 
- defining a move constructor and move assignment work analogously to their copy counterparts, the copy versions take a const l-val reference paramter (which will bind to jsut about anythign), the move version use non const r-val refrence parameters (only bind to rvals) 
```c++
#include <iostream>

template<typename T>
class Auto_ptr4
{
	T* m_ptr {};
public:
	Auto_ptr4(T* ptr = nullptr)
		: m_ptr { ptr }
	{
	}

	~Auto_ptr4()
	{
		delete m_ptr;
	}

	// Copy constructor
	// Do deep copy of a.m_ptr to m_ptr
	Auto_ptr4(const Auto_ptr4& a)
	{
		m_ptr = new T;
		*m_ptr = *a.m_ptr;
	}

	// Move constructor
	// Transfer ownership of a.m_ptr to m_ptr
	Auto_ptr4(Auto_ptr4&& a) noexcept
		: m_ptr(a.m_ptr)
	{
		a.m_ptr = nullptr; // we'll talk more about this line below
	}

	// Copy assignment
	// Do deep copy of a.m_ptr to m_ptr
	Auto_ptr4& operator=(const Auto_ptr4& a)
	{
		// Self-assignment detection
		if (&a == this)
			return *this;

		// Release any resource we're holding
		delete m_ptr;

		// Copy the resource
		m_ptr = new T;
		*m_ptr = *a.m_ptr;

		return *this;
	}

	// Move assignment
	// Transfer ownership of a.m_ptr to m_ptr
	Auto_ptr4& operator=(Auto_ptr4&& a) noexcept
	{
		// Self-assignment detection
		if (&a == this)
			return *this;

		// Release any resource we're holding
		delete m_ptr;

		// Transfer ownership of a.m_ptr to m_ptr
		m_ptr = a.m_ptr;
		a.m_ptr = nullptr; // we'll talk more about this line below

		return *this;
	}

	T& operator*() const { return *m_ptr; }
	T* operator->() const { return m_ptr; }
	bool isNull() const { return m_ptr == nullptr; }
};

class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

Auto_ptr4<Resource> generateResource()
{
	Auto_ptr4<Resource> res{new Resource};
	return res; // this return value will invoke the move constructor
}

int main()
{
	Auto_ptr4<Resource> mainres;
	mainres = generateResource(); // this assignment will invoke the move assignment

	return 0;
}
```
prints: 
Resource acquired 
Resouce destroyed 
- instead of deep copying the source objet (a) into the implicit object, we simply move (steal) the source objcets resources, this involces shallow copying the source ptr into the implicit object,a dnsetting the src ptr to null 
1) inside generateResouce(), local variable res is created and initialized with a dynamically allocated Resouce, wihich causes the first "resource acquired" 
2) res is returned back to main() by vaule, res is moce constructed into a temp object, transferring teh dynamically created obect stored in res to the temp object 
3) res goes out of scopre, because res no longer manages a ptr, nothing happens here 
4) the temp object is move assigned to mainres, this transfers the dynamically created objcet stored in the temp to mainres 
5) the assignment expression ends, and the temp object goes out of expression scope and is destroyed, because it no longer manages a ptr, nothign happens 
6) at the end of main, mainres goes out of scopre, and final 'resource destroyed' is displayed 

- the move construcotr and move assignmentare called when those functions have been defined,a dn the argueent for construction or assignment is an r-val, usualyl this r-al will be a literal or temp val 
- the copy constructor and copy assignment are used otherwise, when teh arguement is an l-val or when the arg is an r-val and the move constructor or move assignment functions arent defined 

The compiler will create an implicit move construtor and move assignemnt operator if all the following are true: 
- no used decalred copy constructors or copy assignemnt operators 
- no user-declared move constructors or move assignment operators 
- no user-decalred destructor

#### Key insight behind move semantics 
- if we construct an object or do an assignment where the arguement is an l-val, the only thing we can reasnably do is copy the lval 
- we cant assume its safe to alter the lval, because it may be used again later in the program 
- if we have an expression "a=b" (where b is an lval), we wouldnt expect b to be changed in any way 
- however, if we consruct an object or do an assignemnt where the arguement is an r-val, thenw eknow that the r-val is just a temp object of some kind 
- instead of copying it, we can simply transfer its resources to the object we're constructing or assigning, this isafe because the temp will be destoryed at the end of the expression anyway

#### Move functions should always leave both objects ina  valid state 
- in the above examples, both the move constructor ad move assignment functions set a.m_ptr to nullptr
- one might think this is not neede, as if `a` is a temp r-val, why bother doing cleanup if parameter `a` is going to be destroyed anyway 
- when `a` goes out of scope, the destrucor for `a` will be called, and `a.m_ptr` will be deleted, if at that point, `a.m_ptr` is still pting to the same object as m_ptr, m_ptr will be left as a danling ptr 
- wehn the object containing m_ptr eventualyl gets used, we'll get undefined behavoir

#### Automatic l-vals returned by value may be moved instead of copied
- in the `generateResouce()` function, when the variable res is returned by value, it is moved instead of copied, even though res is an l-val 
- c++ specs say that automatic objects returned from a function by val can be moedeven if they are l-vals, this makes sence, since res was going to be destroyed at the end of the function, we might as well steal its resources instead of making an expensive and uncessary copy 

simple dynamic templated array: 
```c++
#include <algorithm> // for std::copy_n
#include <iostream>

template <typename T>
class DynamicArray
{
private:
	T* m_array {};
	int m_length {};

public:
	DynamicArray(int length)
		: m_array { new T[length] }, m_length { length }
	{
	}

	~DynamicArray()
	{
		delete[] m_array;
	}

	// Copy constructor
	DynamicArray(const DynamicArray &arr)
		: m_length { arr.m_length }
	{
		m_array = new T[m_length];
		std::copy_n(arr.m_array, m_length, m_array); // copy m_length elements from arr to m_array
	}

	// Copy assignment
	DynamicArray& operator=(const DynamicArray &arr)
	{
		if (&arr == this)
			return *this;

		delete[] m_array;

		m_length = arr.m_length;
		m_array = new T[m_length];

		std::copy_n(arr.m_array, m_length, m_array); // copy m_length elements from arr to m_array

		return *this;
	}

	int getLength() const { return m_length; }
	T& operator[](int index) { return m_array[index]; }
	const T& operator[](int index) const { return m_array[index]; }

};
```

#### Issues with move semantics and `std::swap` 
- copy and swap also works for move semantics, meaning we ca implement our move constructor and move assignment by swapping resouces with the object that will be destroyed 
2 benefits
- the persistent object now controls the resourves that were previously under ownership of the dying object 
- the dying object now controls the resourves that were previously under ownership of the persistent object, when the dying obect actualyl dies, it can do any kind of cleanup required on those resources 
- HOWEVER, implementing the move constructor and move assignment using `std::swap()` is problematic, as when `std::swap()` is called on move-capable objects, it internally uses both move construction and move assignment 
- if we implement move operations using `std::swap()` we create a circular dependency that leads to infinite recursion 
- we can void this my using our own swap function, as long as it does not call the move constructor or move assignment 

```c++
template <typename T>
void mySwapCopy(T& a, T& b)
{
	T tmp { a }; // invokes copy constructor
	a = b; // invokes copy assignment
	b = tmp; // invokes copy assignment
}

int main()
{
	std::string x{ "abc" };
	std::string y{ "de" };

	std::cout << "x: " << x << '\n';
	std::cout << "y: " << y << '\n';

	mySwapCopy(x, y);

	std::cout << "x: " << x << '\n';
	std::cout << "y: " << y << '\n';

	return 0;
}
```
- the above makes 3 copies, one for each copy constructor/assignment being called, which is inefficient 
- doing copies isnt necessary here, we can accomplish the same thing using 3 moves instead, but the parameters are l-val refs, not r-val refs, which is what move expects 

#### `std::move` 
- library function that casts (using static_cast) its arguement into an r-val reference, so that move semantics can be invoked 
- thus, we can use `std::move` to cast an l-val into a type that willl prefer being moved over being copied 
```c++
template <typename T>
void mySwapMove(T& a, T& b)
{
	T tmp { std::move(a) }; // invokes move constructor
	a = std::move(b); // invokes move assignment
	b = std::move(tmp); // invokes move assignment
}

int main()
{
	std::string x{ "abc" };
	std::string y{ "de" };

	std::cout << "x: " << x << '\n';
	std::cout << "y: " << y << '\n';

	mySwapMove(x, y);

	std::cout << "x: " << x << '\n';
	std::cout << "y: " << y << '\n';

	return 0;
}
```
- we can also use `std::move` when filling elements of a container with l-vals 
- we first add an element to a vector using copy semantics, then we add an element to the vector using move semantics 
```c++
int main()
{
	std::vector<std::string> v;

	// We use std::string because it is movable (std::string_view is not)
	std::string str { "Knock" };

	std::cout << "Copying str\n";
	v.push_back(str); // calls l-value version of push_back, which copies str into the array element

	std::cout << "str: " << str << '\n';
	std::cout << "vector: " << v[0] << '\n';

	std::cout << "\nMoving str\n";

	v.push_back(std::move(str)); // calls r-value version of push_back, which moves str into the array element

	std::cout << "str: " << str << '\n'; // The result of this is indeterminate
	std::cout << "vector:" << v[0] << ' ' << v[1] << '\n';

	return 0;
}
```
- first case we passed an l-val to `push_back()`, which used copy semantics to add element to the vector, which leaves the value in str alone 
- second case we passed an r-val (converted l-val by `std::move`), which uses move semantics to add an element to the vector, this is more efficient, as the vector element can steal the strings value rather than having to copy it 

#### Moved from objects will be in a valid, but possibly indeterminate state 
- when we move the value from a temp object, it doesnt matter what value the moved-from object is left with, becasue the tmp object will be destroyed immediately 
- if we call `std::move` on an l-val object, we can continue to access these objects after their values have been moved
2 schools of thought
1) objects that have been moved from should be reset back to some default/zero state, where the object does not own a resource anymore 
2) do whatevers convenient 
- c++ standard says moved from objects shall be placed in a valid but unspecified state 
- in some cases, we want to reuse an object whose value has been moved, rather than allocating a new object
- with a moved-from object, it is safe to call any funtion taht does not depend on the current value of the object, this means we can set of reset the value of the moved-from object 
- we can also test the state of the moved-from object to see if the object has a vlua e

## `std::unique_ptr` 
- defining featue of a smart ptr is that it manages a dynamically allocated resource provided by the user of the smart ptr, and ensures the dynamically allocated object is properly cleaned up at the appropriate time 
- should never be dynamically allocated themselves, which means the object it owns would not be deallocated

- `std::unique_ptr` should be used to manage any dynamicalyl allocated object that is not shared by multiple objects, should completely own the object it manages, not share ownership with other classes 
- it is allocated on the stack, eventually will go out of scope, deleting the resource it is managing
- because `std::unique_ptr` is designed with move semanitcs in mind, copy initialization and copy assignment are disabled, if you want to transfer the contennts managed by it, must use `std::move` 

#### Accessing the managed object 
- 'std::unique_ptr' has an overloaded operator (*/->) that can be used to return the resource being managed, (*) returns ref, (->) returns a ptr 

#### Make unique 
- templated function constructs an object of the template type and intializes it with the args passed into the function 
- recommended over creating `std::unique_ptr` yourself due to simplicity 

#### Returning std::unique_ptr from a function 
- can be safely returned from a function by value 
```c++
std::unique_ptr<Resource> createResource()
{
     return std::make_unique<Resource>();
}

int main()
{
    auto ptr{ createResource() };

    // do whatever

    return 0;
}
```
- in above, `createResource()` returns a `std::unique_ptr` by value, if this val is not assigned to anything, the temp return val will go out of scopre and the rsc will be cleaned up 
- if it is assigned, move semantics will be employed to transfer the resource from the return val to the object assigned to, this makes returning a resource by `std::unique_ptr` much safer than returning raw ptrs 

#### Passing std::unique_ptr to a function 
- if you want the function to take ownership of the contents of the ptr, pass the `std::unique_ptr` by value, we would need to use `std::move` 
```c++
class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

std::ostream& operator<<(std::ostream& out, const Resource&)
{
	out << "I am a resource";
	return out;
}

// This function takes ownership of the Resource, which isn't what we want
void takeOwnership(std::unique_ptr<Resource> res)
{
     if (res)
          std::cout << *res << '\n';
} // the Resource is destroyed here

int main()
{
    auto ptr{ std::make_unique<Resource>() };

//    takeOwnership(ptr); // This doesn't work, need to use move semantics
    takeOwnership(std::move(ptr)); // ok: use move semantics

    std::cout << "Ending program\n";

    return 0;
}
```
- in this case, owernship of the resource was transferred to `takeOwnership()`, so the resource was destroyed at the end of `takeOwnership()` rather than the end of main 
- usually, we dont want the function to take ownership of the resource
```c++
class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

std::ostream& operator<<(std::ostream& out, const Resource&)
{
	out << "I am a resource";
	return out;
}

// The function only uses the resource, so we'll accept a pointer to the resource, not a reference to the whole std::unique_ptr<Resource>
void useResource(const Resource* res)
{
	if (res)
		std::cout << *res << '\n';
	else
		std::cout << "No resource\n";
}

int main()
{
	auto ptr{ std::make_unique<Resource>() };

	useResource(ptr.get()); // note: get() used here to get a pointer to the Resource

	std::cout << "Ending program\n";

	return 0;
} // The Resource is destroyed here
```
- we can pass in the raw ptr from a `std::unique_ptr` by using `get()`

#### `std::unique_ptr` and classes 
- you can use `std::unique_ptr` as a composition member of your class, this way, you dont have to worry about ensuring your class destructor deletes the dynamic memory, as the unique ptr will automatically be destroyed when the class object is destroyed 

#### Misusing `std::unique_ptr` 
1) dont get multiple objects manage the same resouce, as when the destructor is called, both will try to delete the resource 
2) dont manually delete the resource out from underneath the `std::unique_ptr` 

## `std::shared_ptr` 
- meant to solve the case where you need multiple smart ptrs co-owning a resource 
- it is fine to have multiple shared ptrs pointing to the same resource, internally, it keeps track of how many shared_ptr are sharing the resource 
- as long as we have 1 shared_ptr pting to the resource, the resource will not be deallocated 
- as soon as the last shared_ptr managing the resource goes out of scope (or reassigned), the resource will be deallocated 
```c++
class Resource
{
public:
	Resource() { std::cout << "Resource acquired\n"; }
	~Resource() { std::cout << "Resource destroyed\n"; }
};

int main()
{
	Resource* res { new Resource };
	std::shared_ptr<Resource> ptr1 { res };
	{
		std::shared_ptr<Resource> ptr2 { res }; // create ptr2 directly from res (instead of ptr1)

		std::cout << "Killing one shared pointer\n";
	} // ptr2 goes out of scope here, and the allocated Resource is destroyed

	std::cout << "Killing another shared pointer\n";

	return 0;
} // ptr1 goes out of scope here, and the allocated Resource is destroyed again
```
- over here, the program will crash because we created 2 shared_ptrs independently from each other, as a consequence, even though theyre both pting to the same resource, they arent aware of each other 
- when ptr2 goes out of scope, it thinks its the only owner of the resource, and deallocates it, when ptr1 later goes out of scope, it thinks the same thing, then tried to delete the resource again 
- if we need more than 1 shared_ptr to a given resouce, copy an existing shared ptr 

### Digging into `std::shared_ptr` 
- unlike `std::unique_ptr`, which uses a single ptr internally, `std::shared_ptr` uses 2 ptrs internally
- one ptr points at the resource being managed, while the other points at a control block, which is a dynamically allocated object that tracks many things, among how many shared_ptrs are pointing to the resource 
- whe a shared_ptr is created normally, te memory for the managed object and the control block are allocated seperataly, when we use `std::make_shared()`, this can be optimized into a single memory allocation, which leads to better performance 
- this is also why independently creating 2 shared_ptrs pointing to the same resouce is bad, each shared_ptr will have one ptr pointing at the resouce
- in addition, each shared_ptr will allocate its own control block, which indicates that only 1 ptr is pointing to the resource 
- when a shared_ptr is cloned using copy assignment, the data in the control block cna be appropriately updated to indicate that there are now additional shared_ptrs co-managing the resource 

### Shared ptrs can be created from unique ptrs 
- a unique_ptr can be converted to a shared_ptr via a special shared_ptr construcotr that accepts a unique_ptr r val, the contents of the unique_ptr are moved to the shared_ptr 
- vice versa is not true, if you are creating a function that is going to return a smart ptr, better off retunrning a unique ptr and assigning it to shared ptr if needed 

## Circular dependency issues with `std::shared_ptr` and `std::weak_ptr`
```c++
class Person
{
	std::string m_name;
	std::shared_ptr<Person> m_partner; // initially created empty

public:

	Person(const std::string &name): m_name(name)
	{
		std::cout << m_name << " created\n";
	}
	~Person()
	{
		std::cout << m_name << " destroyed\n";
	}

	friend bool partnerUp(std::shared_ptr<Person> &p1, std::shared_ptr<Person> &p2)
	{
		if (!p1 || !p2)
			return false;

		p1->m_partner = p2;
		p2->m_partner = p1;

		std::cout << p1->m_name << " is now partnered with " << p2->m_name << '\n';

		return true;
	}
};

int main()
{
	auto lucy { std::make_shared<Person>("Lucy") }; // create a Person named "Lucy"
	auto ricky { std::make_shared<Person>("Ricky") }; // create a Person named "Ricky"

	partnerUp(lucy, ricky); // Make "Lucy" point to "Ricky" and vice-versa

	return 0;
}
```
- in the above example, we dynamically allocate 2 persons, "lucy" and "ricky" using make_shared(), then partner up, which sets the shared_ptr in each person to pt to the other 
- however, when running the progran, no deallocations take place, 
- after partnerUp() is called, there are 2 shared ptrs pointing to each person, their own and the ptr in the other Person class 
- at the end of main(), the ricky shared_ptr goes out of scope first, when that happens, ricky checks if there are any other shared ptrs that co-own the person "ricky"
- lucy's internal shared ptr is also pointing to ricky, thus when ricky goes out of scope, it doesnt deallocate ricky, if it did, lucy's internal ptr would end up as a dangling ptr,
- at this pt, we now have one shared ptr to ricky (lucys internal) and 2 shared ptrs to lucy (lucy and ricky's internal)

### Ciruclar references 
- also called a cyclical reference, is a series of references where each object references the next, and the last object references back to the first, causing a loop
- this is what happened in the above case, ricky pts to lucy and lucy pts to ricky 

### `std::weak_ptr` 
- designed to solve the cyclical ownership problem above 
- it is an observer, can observe and access the same object, but is not considered an owner, when a shared_ptr goes out of scope, it only considers whether other shared_ptrs are co-pwning the object, weak_ptr does not count 
```c++
class Person
{
	std::string m_name;
	std::weak_ptr<Person> m_partner; // note: This is now a std::weak_ptr

public:

	Person(const std::string &name): m_name(name)
	{
		std::cout << m_name << " created\n";
	}
	~Person()
	{
		std::cout << m_name << " destroyed\n";
	}

	friend bool partnerUp(std::shared_ptr<Person> &p1, std::shared_ptr<Person> &p2)
	{
		if (!p1 || !p2)
			return false;

		p1->m_partner = p2;
		p2->m_partner = p1;

		std::cout << p1->m_name << " is now partnered with " << p2->m_name << '\n';

		return true;
	}
};

int main()
{
	auto lucy { std::make_shared<Person>("Lucy") };
	auto ricky { std::make_shared<Person>("Ricky") };

	partnerUp(lucy, ricky);

	return 0;
}
```
- now when ricky goes out of scope, there are no other shared_ptrs pointing to ricky, thus we will have a successful deallocation 

### Using `std::weak_ptr` 
- one downside of weak_ptr is that they are not directly usable, to use a weak_ptr, we must first convert to a shared ptr
- to convert a shared_ptr to a weak_ptr, we can use the `lock()` member function 
```c++
class Person
{
	std::string m_name;
	std::weak_ptr<Person> m_partner; // note: This is now a std::weak_ptr

public:

	Person(const std::string &name) : m_name(name)
	{
		std::cout << m_name << " created\n";
	}
	~Person()
	{
		std::cout << m_name << " destroyed\n";
	}

	friend bool partnerUp(std::shared_ptr<Person> &p1, std::shared_ptr<Person> &p2)
	{
		if (!p1 || !p2)
			return false;

		p1->m_partner = p2;
		p2->m_partner = p1;

		std::cout << p1->m_name << " is now partnered with " << p2->m_name << '\n';

		return true;
	}

	std::shared_ptr<Person> getPartner() const { return m_partner.lock(); } // use lock() to convert weak_ptr to shared_ptr
	const std::string& getName() const { return m_name; }
};

int main()
{
	auto lucy { std::make_shared<Person>("Lucy") };
	auto ricky { std::make_shared<Person>("Ricky") };

	partnerUp(lucy, ricky);

	auto partner = ricky->getPartner(); // get shared_ptr to Ricky's partner
	std::cout << ricky->getName() << "'s partner is: " << partner->getName() << '\n';

	return 0;
}
```
- here to acces the names of the partners, we first have to conver the internal weak_ptrs to shared_ptrs

### Avoiding dandling ptr with `std::weak_ptr` 
- consider the case where a normal 'dumb' ptr is holding the address of some object, and then that object is destroyed, such a ptr is dangling, and dereferencing the ptr will lead to undefind behaviour 
- there is no way for us to determine whether a ptr holding a non null address is dangling or not, this is a large part of why dumb ptrs are dangerous 
- because `std::weak_ptr` wont keep an owned resource alive, it is similarly possible for a `std::weak_ptr` to be left pointing to a resouce that has been deallocated by a `std::shared_ptr`
- instead, weak_ptr has access to the reference count for an object, it can determine if it is pointing to a valid object or not, if the ref count is non-zero, the resource is still valid, if it is 0, then the resource has been destroyed 
- we can use `expired()` which returns true if `std::weak_ptr` is pointing to an invalid object, and false otherwise 

### Summary 
- copy semanitcs allow our classes to be copied, done via copy constructor and copy assignment operator 
- move semantics will mean a class will transfer ownership of the object rather than making a copy, done via move construcotr and move assignment operator 
- r val reference is a reference that is designed to be initialized with an r-val, created using double &&, we can pass in r-val refs as paramenters, but never return 
- if we construct an object or do an assignment where the arguement is a l-val, the only thing we can reasonably do is copy the lval, we cant assume its safe to alter the l-val, because it may be used in the program again 
- if we construct an object or do an assignment where the arguement is an r-val, we know that the r-val is a temp object, and we can just transfer its resources to the object we are constructing/assigning instead of using an expensive copy operation 
- `std::move` allows us to treat an l-val as an r-val, useful when we want to invoke move semantics on an l-val 
- `std::unique_ptr` is a smart ptr class that should use if you have a single non shareable resource, use make_unique() 
- `std::shared_ptr` is used when we need multiple objects accessing the same resource, resource will not be destroyed until the last shared_ptr managing is destroyed, use make_shared, and copy semantics 
- `std::weak_ptr` is when you need one or more objects with the ability to view and access a resource managed by a shared_ptr, but not considerede when determining whether a resource should be destroyed 
- 











