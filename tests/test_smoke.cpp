#include <cstring>
#include "check.h"
static void run() { CHECK(1 + 1 == 2); CHECK_STREQ("pip", "pip"); }
TEST_MAIN()
