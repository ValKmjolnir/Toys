#include <stdio.h>
#include <stdarg.h>

int miniprintf(const char* format,...)
{
	if(!format)
		return -1;
	va_list val;
	va_start(val,format);
	const char* tmp;
	
	int int_val;
	double dbl_val;
	const char* str_val;
	
	for(tmp=format;*tmp;++tmp)
	{
		if(*tmp!='%')
		{
			putchar(*tmp);
			continue;
		}
		switch(*(++tmp))
		{
			case 'd':
				int_val=va_arg(val,int);
				printf("%d",int_val);
				break;
			case 'f':
				dbl_val=va_arg(val,double);
				printf("%f",dbl_val);
				break;
			case 's':
				str_val=va_arg(val,char*);
				for(;*str_val;++str_val)
					putchar(*str_val);
				break;
			default:
				putchar(*tmp);
				break;
		}
	}
	
	va_end(val);
	return tmp-format;
}
 
int main()
{
   char str[]="hello world";
   miniprintf("%d, %f, %s",10,2.0,str);
   return 0;
}
