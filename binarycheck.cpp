#include <iostream>
#include <fstream>
#include <cstring>
using namespace std;
void binaryfile(string file)
{
	char c;
	ifstream fin(file,ios::binary);
	if(fin.fail())
		return;
	int count=0;
	while(!fin.eof())
	{
		fin>>c;
		if(fin.eof())
			break;
		++count;
		int bit[8];
		for(int i=0;i<8;++i)
		{
			bit[i]=(1 & c);
			c>>=1;
		}
		for(int i=7;i>=0;--i)
			cout<<bit[i];
		cout<<" ";
		if(count%8==0)
			cout<<endl;
	}
	cout<<endl;
	fin.close();
	return;
}
void hexfile(string file)
{
	char c;
	char rs[16];
	ifstream fin(file,ios::binary);
	if(fin.fail())
		return;
	int count=0;
	while(!fin.eof())
	{
		fin>>c;
		if(fin.eof())
			break;
		rs[count]=c;
		++count;
		int bit[8];
		for(int i=0;i<8;++i)
		{
			bit[i]=(1 & c);
			c>>=1;
		}
		int hex=bit[7]*8+bit[6]*4+bit[5]*2+bit[4];
		if(hex<10)
			cout<<hex;
		else
			cout<<(char)('a'+hex-10);
		hex=bit[3]*8+bit[2]*4+bit[1]*2+bit[0];
		if(hex<10)
			cout<<hex<<" ";
		else
			cout<<(char)('a'+hex-10)<<" ";
		if(count==16)
		{
			count=0;
			cout<<"  ";
			for(int i=0;i<16;++i)
			{
				if(rs[i]>31)
					cout<<rs[i];
				else
					cout<<".";
			}
			cout<<endl;
		}
	}
	for(int i=0;i<16-count;++i)
		cout<<"   ";
	cout<<"  ";
	for(int i=0;i<count;++i)
	{
		if(rs[i]>31)
			cout<<rs[i];
		else
			cout<<".";
	}
	cout<<endl;
	fin.close();
	return;
}
int main()
{
	string file;
	while(1)
	{
		cin>>file;
		//binaryfile(file);
		hexfile(file);
	}
	return 0;
}

