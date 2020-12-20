#include <iostream>
#include <ctime>
int tmp;
double ti[10],tt[10],tv[10]; 
void test_if(int i)
{
	clock_t begin=clock();
	for(int i=0;i<100000000;++i)
	{
		if(rand()&1) tmp=1;
		else tmp=2;
	}
	ti[i]=(double(clock()-begin))/CLOCKS_PER_SEC;
	return;
}

void test_tri(int i)
{
	clock_t begin=clock();
	for(int i=0;i<100000000;++i)
	{
		tmp=(rand()&1)?1:2;
	}
	tt[i]=(double(clock()-begin))/CLOCKS_PER_SEC;
	return;
}

void test_vec(int i)
{
	clock_t begin=clock();
	for(int i=0;i<100000000;++i)
	{
		int vec[2]={2,1};
		tmp=vec[rand()&1];
	}
	tv[i]=(double(clock()-begin))/CLOCKS_PER_SEC;
	return;
}

int main()
{
	std::srand(unsigned(std::time(NULL)));
	for(int i=0;i<10;++i)
	{
		test_if(i);
		test_tri(i);
		test_vec(i);
	}
	double tmp1=0,tmp2=0,tmp3=0;
	for(int i=0;i<10;++i)
	{
		tmp1+=ti[i];
		tmp2+=tt[i];
		tmp3+=tv[i];
	}
	std::cout<<"if-else    :"<<tmp1*0.1<<"s\n";
	std::cout<<"trinocular :"<<tmp2*0.1<<"s\n";
	std::cout<<"vectorize  :"<<tmp3*0.1<<"s\n";
	system("pause");
	return 0;
}
