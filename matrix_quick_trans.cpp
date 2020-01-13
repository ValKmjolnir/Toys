#include <iostream>
#include <list>
using namespace std;
struct triple
{
	int x;
	int y;
	int num;
	triple& operator=(const triple& p)
	{
		x=p.x;
		y=p.y;
		num=p.num;
		return *this;
	}
};
class matrix
{
	private:
		triple* mat;
		int elem_num;
	public:
		matrix(const int number)
		{
			elem_num=number;
			mat=new triple[number];
		}
		~matrix()
		{
			delete []mat;
		}
		void add_data(int x,int y,int num)
		{
			static int i=0;
			mat[i].x=x;
			mat[i].y=y;
			mat[i].num=num;
			++i;
			return;
		}
		void transpose()
		{
			triple* tmp=new triple[elem_num];
			int max_col=0;
			for(int i=0;i<elem_num;++i)
				if(mat[i].y>max_col)
					max_col=mat[i].y;
			int* num=new int[max_col];
			int* cpot=new int[max_col];
			for(int i=0;i<max_col;++i)
				num[i]=0;
			for(int i=0;i<elem_num;++i)
				++num[mat[i].y-1];
			cpot[0]=1;
			for(int i=1;i<max_col;++i)
				cpot[i]=cpot[i-1]+num[i-1];
			for(int i=0;i<elem_num;++i)
			{
				int col=mat[i].y-1;
				int place=cpot[col]-1;
				tmp[place].x=mat[i].y;
				tmp[place].y=mat[i].x;
				tmp[place].num=mat[i].num;
				++cpot[col];
			}
			for(int i=0;i<elem_num;++i)
				cout<<tmp[i].x<<" "<<tmp[i].y<<" "<<tmp[i].num<<endl;
			delete []num;
			delete []cpot;
			delete []tmp;
			return;
		}
};
int main()
{
	matrix M(8);
	for(int i=0;i<8;++i)
	{
		int x,y,num;
		cin>>x>>y>>num;
		M.add_data(x,y,num);
	}
	M.transpose();
	return 0;
}
/*
1 2 12
1 3 9
3 1 -3
3 6 14
4 3 24
5 2 18
6 1 15
6 4 -7

1 3 -3
1 6 15
2 1 12
2 5 18
3 1 9
3 4 24
4 6 -7
6 3 14
*/
