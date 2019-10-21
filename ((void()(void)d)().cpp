#include <iostream>
using namespace std;
void prt()
{
	cout<<"fuck"<<endl;
	return;
}

int main()
{
	long long d=(long long)prt;
	printf("0x%hdd\n",d);
	((void(*)(void))d)();
	d=0xdeadbeef;
	((void(*)(void))d)();
	return 0;
}
