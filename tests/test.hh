#ifndef TEST_HH
#define TEST_HH
#include "ecs.hh"
#include <cstdio>
#include <cstdlib>

using namespace monkero;
#define test(condition) if(!(condition)) {fprintf(stderr, __FILE__ ":%d " #condition, __LINE__); exit(EXIT_FAILURE);}
#endif
