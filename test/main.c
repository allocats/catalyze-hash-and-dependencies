#include <stdio.h>

#include "arena.h"
#include "foo.h"
#include "lib/lib.h"
#include "lib2/lib.h"

int main() {
    foo();
    printf("main\n");
    return 0;
}
