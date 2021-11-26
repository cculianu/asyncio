# asyncio
Asyncio is a C++20 coroutine library to write concurrent code using the await syntax, and imitate python asyncio library.

## Hello world
```cpp
Task<> hello_world() {
    fmt::print("hello\n");
    co_await asyncio::sleep(1s);
    fmt::print("world\n");
}

int main() {
    asyncio::run(hello_world());
}
```
output:
```shell
hello
world
```

## Dump callstack
```cpp
Task<int> factorial(int n) {
    if (n <= 1) {
        co_await dump_callstack();
        co_return 1;
    }
    co_return (co_await factorial(n - 1)) * n;
}

int main() {
    fmt::print("run result: {}\n", asyncio::run(factorial(10)));
    return 0;
}
```
output:
```shell
[0] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:17
[1] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[2] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[3] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[4] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[5] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[6] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[7] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[8] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20
[9] void factorial(factorial(int)::_Z9factoriali.Frame*) at /project/asyncio/test/st/hello_world.cpp:20

run result: 3628800
```

## Gather
```cpp
auto factorial(std::string_view name, int number) -> Task<int> {
    int r = 1;
    for (int i = 2; i <= number; ++i) {
        fmt::print("Task {}: Compute factorial({}), currently i={}...\n", name, number, i);
        co_await asyncio::sleep(500ms);
        r *= i;
    }
    fmt::print("Task {}: factorial({}) = {}\n", name, number, r);
    co_return r;
};

auto test_void_func() -> Task<> {
    fmt::print("this is a void value\n");
    co_return;
};

int main() {
    asyncio::run([&]() -> Task<> {
        auto&& [a, b, c, _void] = co_await asyncio::gather(
            factorial("A", 2),
            factorial("B", 3),
            factorial("C", 4),
            test_void_func());
        assert(a == 2);
        assert(b == 6);
        assert(c == 24);
    }());
}
```

output:
```shell
Task A: Compute factorial(2), currently i=2...
Task B: Compute factorial(3), currently i=2...
Task C: Compute factorial(4), currently i=2...
this is a void value
Task C: Compute factorial(4), currently i=3...
Task A: factorial(2) = 2
Task B: Compute factorial(3), currently i=3...
Task B: factorial(3) = 6
Task C: Compute factorial(4), currently i=4...
Task C: factorial(4) = 24
```

## Tested Compiler
- gcc-12

## TODO
- implement result type for code reuse, `variant<monostate, value, exception>`
- implement coroutine backtrace(dump continuation chain)
- implement some io coroutine(socket/read/write/close)

## Reference
- https://github.com/lewissbaker/cppcoro
- https://docs.python.org/3/library/asyncio.html
