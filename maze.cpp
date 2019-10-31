#include <iostream>
#include <ctime>
#include <cstdlib>
using namespace std;
void delay(int n)
{
	for(int i=0;i<n*100000;++i);
	return;
}
class matrix_map
{
	private:
		int** map;
		int row;
		int col;
		int pr;
		int pc;
	public:
		matrix_map(int _row,int _col)
		{
			row=_row;
			col=_col;
			map=new int* [row];
			for(int i=0;i<row;++i)
				map[i]=new int[col];
			return;
		}
		~matrix_map()
		{
			for(int i=0;i<row;++i)
				delete []map[i];
			delete []map;
			return;
		}
		void init_map()
		{
			srand(unsigned(time(NULL)));
			for(int i=0;i<row;++i)
				map[i][0]=map[i][col-1]=1;
			for(int i=0;i<col;++i)
				map[0][i]=map[row-1][i]=1;
			for(int i=1;i<row-1;++i)
				for(int j=1;j<col-1;++j)
					map[i][j]=(rand()%100<40);
			while(1)
			{
				pr=rand()%row;
				pc=rand()%col;
				if(!map[pr][pc])
					break;
			}
		}
		void print_map()
		{
			system("cls");
			for(int i=0;i<row;++i)
			{
				for(int j=0;j<col;++j)
				{
					if(map[i][j]==1)
						cout<<'#';
					else if(map[i][j]==2)
						cout<<'+';
					else if(map[i][j]==3)
						cout<<'@';
					else
						cout<<' ';
				}
				cout<<endl;
			}
			return;
		}
		void run_main(int r,int c)
		{
			if(map[r][c])
				return;
			map[r][c]=3;
			delay(100);
			print_map();
			map[r][c]=2;
			run_main(r+1,c);
			map[r][c]=3;
			delay(100);
			print_map();
			map[r][c]=2;
			run_main(r,c+1);
			map[r][c]=3;
			delay(100);
			print_map();
			map[r][c]=2;
			run_main(r-1,c);
			map[r][c]=3;
			delay(100);
			print_map();
			map[r][c]=2;
			run_main(r,c-1);
			map[r][c]=3;
			delay(100);
			print_map();
			map[r][c]=2;
			return;
		}
		void run()
		{
			run_main(pr,pc);
			return;
		}
};

int main()
{
	matrix_map m(10,15);
	while(1)
	{
		m.init_map();
		m.print_map();
		m.run();
	}
	return 0;
}
