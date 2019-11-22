#include <iostream>
#include <fstream>
#include <cstring>
#include <list>

class wordlib
{
	private:
		std::list<std::string> words;
	public:
		wordlib()
		{
			std::ifstream fin("word.bs");
			if(fin.fail())
			{
				fin.close();
				return;
			}
			std::string str;
			while(!fin.eof())
			{
				fin>>str;
				if(fin.eof())break;
				words.push_back(str);
			}
			fin.close();
			return;
		}
		~wordlib()
		{
			std::ofstream fout("word.bs");
			for(std::list<std::string>::iterator i=words.begin();i!=words.end();++i)
				fout<<*i<<" ";
			fout.close();
			words.clear();
			return;
		}
		void print_word_lib()
		{
			int cnt=1;
			for(std::list<std::string>::iterator i=words.begin();i!=words.end();++i,++cnt)
				std::cout<<cnt<<':'<<*i<<(cnt%8? " ":"\n");
			return;
		}
		void add_word(std::string str)
		{
			for(std::list<std::string>::iterator i=words.begin();i!=words.end();++i)
				if(*i==str)
					return;
			words.push_back(str);
			return;
		}
		void add_words(std::string filename)
		{
			std::ifstream fin(filename);
			if(fin.fail())
			{
				fin.close();
				return;
			}
			std::string str="";
			char c;
			while(!fin.eof())
			{
				c=fin.get();
				if(fin.eof())break;
				if('a'<=c&&c<='z' || 'A'<=c&&c<='Z' || c=='\'')
					str+=(('a'<=c&&c<='z' || c=='\'')? c:c-'A'+'a');
				else
				{
					if(str.length())
						add_word(str);
					str="";
				}
			}
			fin.close();
			return;
		}
		void sort_lib()
		{
			words.sort();
			return;
		}
};

int main()
{
	wordlib dictionary;
	std::string filename="record.cpp";
	dictionary.add_words(filename);
	dictionary.sort_lib();
	dictionary.print_word_lib();
	return 0;
}
