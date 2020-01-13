#include <iostream>
#include <queue>
using namespace std;
class Graph
{
	private:
		bool* visited;
		int* distance;
		int** vex;
		int vexnum;
	public:
		Graph()
		{
			visited=NULL;
			distance=NULL;
			vex=NULL;
		}
		~Graph()
		{
			delete []visited;
			delete []distance;
			for(int i=0;i<vexnum;++i)
				delete []vex[i];
			delete []vex;
		}
		void Init(int num)
		{
			vexnum=num;
			visited=new bool[vexnum];
			distance=new int[vexnum];
			vex=new int*[vexnum];
			for(int i=0;i<vexnum;++i)
				vex[i]=new int[vexnum];
			for(int i=0;i<vexnum;++i)
				for(int j=0;j<vexnum;++j)
					vex[i][j]=65536;
			return;
		}
		void AddVex(int begin_node,int end_node,int weight)
		{
			vex[begin_node][end_node]=weight;
			vex[end_node][begin_node]=weight;
			return;
		}
		void Dijkstra(int begin_node,int end_node)
		{
			for(int i=0;i<vexnum;++i)
			{
				visited[i]=false;
				distance[i]=65536;
			}
			visited[begin_node]=true;
			distance[begin_node]=0;
			for(int i=0;i<vexnum;++i)
				if(vex[begin_node][i])
					distance[i]=vex[begin_node][i];
			for(int i=0;i<vexnum;++i)
			{
				int shortest_node=0;
				for(int j=0;j<vexnum;++j)
					if(!visited[j] && (!shortest_node || distance[j]<distance[shortest_node]))
						shortest_node=j;
				visited[shortest_node]=true;
				for(int j=0;j<vexnum;++j)
					if(!visited[j] && distance[shortest_node]+vex[shortest_node][j]<distance[j])
						distance[j]=distance[shortest_node]+vex[shortest_node][j];	
			}
			cout<<distance[end_node]<<endl;
			return;
		}
		/*
		void Dijkstra_queue(int begin_node,int end_node)
		{
			for(int i=0;i<vexnum;++i)
			{
				visited[i]=false;
				distance[i]=65536;
			}
			visited[begin_node]=true;
			distance[begin_node]=0;
			return;
		}
		*/
};
int main()
{
	Graph G;
	int n,m,begin_node,end_node,weight;
	cin>>n>>m;
	G.Init(n);
	for(int i=0;i<m;++i)
	{
		cin>>begin_node>>end_node>>weight;
		G.AddVex(begin_node,end_node,weight);
	}
	cin>>begin_node>>end_node;
	G.Dijkstra(begin_node,end_node);
	G.Dijkstra_queue(begin_node,end_node);
	return 0;
}
/*
8 12
0 1 3
0 2 6
0 4 9
1 3 1
1 4 1
2 5 7
2 4 5
3 6 2
4 6 10
4 5 4
5 7 1
6 7 5
0 7

9
*/
