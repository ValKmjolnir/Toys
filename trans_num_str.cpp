#include <iostream>
#include <cstring>

bool check_numerable_string(std::string str)
{
	if(str.length()==1)
		return ('0'<=str[0] && str[0]<='9');
	else if(str[0]=='0' && str[1]=='x')
	{
		if(str.length()==2)
			return false;
		for(int i=2;i<str.length();++i)
			if(!('0'<=str[i] && str[i]<='9' || 'a'<=str[i] && str[i]<='f' || 'A'<=str[i] && str[i]<='F'))
				return false;
	}
	else if(str[0]=='0' && str[1]=='o')
	{
		if(str.length()==2)
			return false;
		for(int i=2;i<str.length();++i)
			if(!('0'<=str[i] && str[i]<='7'))
				return false;
	}
	else
	{
		int str_len=str.length();
		int float_dot_cnt=0;
		for(int i=0;i<str_len;++i)
			if(str[i]=='.')
				++float_dot_cnt;
		if(float_dot_cnt>1)
			return false;
		for(int i=0;i<str_len;++i)
			if(!('0'<=str[i] && str[i]<='9' || str[i]=='.'))
				return false;
	}
	return true;
}
double trans_string_to_number(std::string str)
{
	double trans_str_number=0;
	if(str.length()==1)
		trans_str_number=(double)(str[0]-'0');
	else if(str[0]=='0' && str[1]=='x')
	{
		long long int hex_number=0,bit_pow=1;
		for(int i=str.length()-1;i>1;--i)
		{
			hex_number+=bit_pow*(str[i]-'0');
			bit_pow<<=4;
		}
		trans_str_number=(double)hex_number;
	}
	else if(str[0]=='0' && str[1]=='o')
	{
		long long int oct_number=0,bit_pow=1;
		for(int i=str.length()-1;i>1;--i)
		{
			oct_number+=bit_pow*(str[i]-'0');
			bit_pow<<=3;
		}
		trans_str_number=(double)oct_number;
	}
	else
	{
		int float_dot_place=str.length();
		int str_len=str.length();
		double pow;
		for(int i=0;i<str_len;++i)
			if(str[i]=='.')
			{
				float_dot_place=i;
				break;
			}
		pow=0.1;
		for(int i=float_dot_place+1;i<str_len;++i)
		{
			trans_str_number+=pow*(double(str[i]-'0'));
			pow/=10;
		}
		pow=1;
		for(int i=float_dot_place-1;i>=0;--i)
		{
			trans_str_number+=pow*(double(str[i]-'0'));
			pow*=10;
		}
	}
	return trans_str_number;
}
std::string trans_number_to_string(double number)
{
	std::string trans_num_string="";
	double integer_bit=1;
	while(number>=integer_bit)
		integer_bit*=10;
	integer_bit/=10;
	while(integer_bit!=0.1)
	{
		trans_num_string+=(char)('0'+(int(number/integer_bit)));
		number-=(double)(int(number/integer_bit))*integer_bit;
		integer_bit/=10;
	}
	if(number!=0)
		trans_num_string+='.';
	while(number!=0)
	{
		trans_num_string+=(char)('0'+int(number*10));
		number*=10;
		number-=(double)(int(number));
	}
	return trans_num_string;
}
int main()
{
	double a=2147483648.91022341243025;
	std::cout<<trans_string_to_number(trans_number_to_string(a))<<std::endl;
	std::cout<<check_numerable_string(trans_number_to_string(a))<<std::endl;
	return 0;
}
