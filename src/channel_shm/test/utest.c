#include "utest.h"

extern UTEST(Channel);
extern UTEST(Channel_close);

UTEST_MAIN()
{ 
	utest_Channel();
	utest_Channel_close();
}

int main(void)
{
	utest();

	return 0;
}
