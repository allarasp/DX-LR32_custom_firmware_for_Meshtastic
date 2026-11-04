#include <stdint.h>
struct test_gpio {uint32_t OTYPER,PUPDR,AFR,DIRCR,ODCLR,ODSET,IDR;};
extern struct test_gpio test_gpio;
#define GPIOC (&test_gpio)
