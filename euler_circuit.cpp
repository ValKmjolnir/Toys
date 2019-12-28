#include <iostream>
#include <cmath>

class graph
{
	private:
		int node_number;
		int arcs_number;
		bool have_output;
		bool** map;
		bool** visited;
		int* path;
	public:
		graph()
		{
			map=NULL;
			visited=NULL;
			path=NULL;
			// input the number of nodes and arcs to create a graph
			std::cout<<"input number of nodes: ";
			std::cin>>node_number;
			std::cout<<"input number of arcs: ";
			std::cin>>arcs_number;
			
			if(node_number>0)
			{
				map=new bool*[node_number];
				visited=new bool*[node_number];
				path=new int[arcs_number<<1];
				for(int i=0;i<node_number;++i)
				{
					map[i]=new bool[node_number];
					visited[i]=new bool[node_number];
					for(int j=0;j<node_number;++j)
					{
						map[i][j]=false;
						visited[i][j]=false;
					}
				}
				for(int i=0;i<(arcs_number<<1);++i)
					path[i]=-1;
			}
			return;
		}
		~graph()
		{
			if(map)
			{
				for(int i=0;i<node_number;++i)
					delete []map[i];
				delete []map;
			}
			if(visited)
			{
				for(int i=0;i<node_number;++i)
					delete []visited[i];
				delete []visited;
			}
			if(path)
				delete []path;
			return;
		}
		int get_node_number()
		{
			return node_number;
		}
		void input_arcs()
		{
			int node_begin,node_end;
			// create arcs
			for(int i=0;i<arcs_number;++i)
			{
				std::cin>>node_begin>>node_end;
				map[node_begin][node_end]=true;
			}
			return;
		}
		void find_euler_path_recursion_work(int node,int depth)
		{
			path[depth]=node;
			if(depth==arcs_number && path[0]==path[depth])// check if all the arcs have been visited and the path generates a loop
			{
				have_output=true;
				std::cout<<"euler circuit: ";
				for(int i=0;i<=depth;++i)
					std::cout<<path[i]<<(i==depth? " ":" -> ");
				std::cout<<std::endl;
				return;
			}
			for(int i=0;i<node_number;++i)
				if(map[node][i] && !visited[node][i])
				{
					visited[node][i]=true;// this arc has been visited
					find_euler_path_recursion_work(i,depth+1);
					visited[node][i]=false;// return last visit
				}
			return;
		}
		void find_euler_path()
		{
			for(int i=0;i<node_number;++i)
			{
				int in_cnt=0;
				int out_cnt=0;
				for(int j=0;j<node_number;++j)
				{
					if(map[i][j])
						++out_cnt;
					if(map[j][i])
						++in_cnt;
				}
				if(!(in_cnt && out_cnt && in_cnt==out_cnt))
				{
					std::cout<<"the graph does not have an euler circuit."<<std::endl;
					return;
				}
			}
			have_output=false;
			for(int i=0;i<node_number;++i)
				find_euler_path_recursion_work(i,0);
			if(!have_output)
				std::cout<<"the graph does not have an euler circuit."<<std::endl;
			return;
		}
};

int main()
{
	graph G;
	G.input_arcs();
	if(G.get_node_number()>0)
		G.find_euler_path();
	return 0;
}
