// Lambda return-type inference must type the first `return` operand in the
// lambda's own scope. Each lambda below returns a name that an outer variable
// of a different class also spells; the compiler calls the method of the
// class named in the comment.
#include <cstdio>

struct Inner {
    int run() { return std::puts("Inner::run"); }
};

struct Outer {
    int run() { return std::puts("Outer::run"); }
};

int main() {
    Outer w;
    Outer v;
    Outer c;
    auto by_param = [](Inner w) { return w; };
    by_param(Inner{}).run(); // Inner::run
    auto by_local = [] {
        Inner v;
        return v;
    };
    by_local().run(); // Inner::run
    auto by_capture = [c = Inner{}] { return c; };
    by_capture().run(); // Inner::run
    auto generic = [](auto w) { return w; };
    generic(Inner{}).run(); // Inner::run (resolver: unknown, no link)
    auto defaulted = [](Inner w = Inner{}) { return w; };
    defaulted().run(); // Inner::run
    auto discarded = [](auto w) { return (static_cast<void>(w), Inner{}); };
    discarded(0).run(); // Inner::run (the comma's right operand decides)
    w.run();               // Outer::run
    v.run();               // Outer::run
    c.run();               // Outer::run
    return 0;
}
