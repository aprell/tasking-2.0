#include "utest.h"

extern UTEST(Channel);
extern UTEST(Channel_close);
extern UTEST(Channel_cache);

UTEST_MAIN()
{
	//utest_Channel();
	//utest_Channel_close();
	utest_Channel_cache();
}

int main(void)
{
	utest();

	return 0;
}
