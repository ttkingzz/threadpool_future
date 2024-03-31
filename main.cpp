#include "threadpool.h"
#include<chrono>
#include<thread>
#include<iostream>
using namespace std;


int sum1(int a1,int a2)
{
	int sum=0;
	std::this_thread::sleep_for(std::chrono::seconds(5));
	for(int i=a1;i!=a2;i++)
	{
		sum += a1;
	}
	return sum;
}
using uLong = unsigned long long;
int main()
{
	{
		ThreadPool pool;
		// 用户设置线程池的工作模式
		pool.setMode(PoolMode::MODE_FIXED);
		// 开始启动线程池
		pool.start(2);
		future<int> res1= pool.submitTask(sum1,20,30);
		future<int> res2= pool.submitTask(sum1,20,30);
		future<int> res3= pool.submitTask(sum1,20,30);
		future<int> res4= pool.submitTask(sum1,20,30);
		future<int> res5= pool.submitTask(sum1,20,30);
		future<int> res6= pool.submitTask(sum1,20,30);
		
		cout << res1.get() << endl;
	}

}