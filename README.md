# 可变惨模板的线程池
对采用多态和Any+Result实现的多线程池进行优化：[基于C++11的跨平台线程池](https://github.com/ttkingzz/ThreadPool.git)
- 基于**可变参模板编程**和**引用折叠**原理，实现线程池submitTask接口，支持任意任务函数和任意参数的传递
- 使**用future类型**定制submitTask提交任务的返回值

  取消了Any、Result和Task类，主要修改submitTask方法。
  ```c++
  template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
  {
  /*code*/
  }
  ```

## 细节：
优化后代码更加简洁，但是需要注意许多细节：
- 1.函数返回值自动类型推导和decltype搭配使用
- 2.task通过packaged_task封装，参数用bind绑定
- 3.返回类型未知，如何定义任务类型：直接用void()，然后再外面套一层lambda表达式，即[task](){(*task)();}
- 4.任务提交失败，要么抛出异常，或者返回返回值的零值；
- 5.调用future的get方法会等待任务执行完成，不管是提交失败 还是正常执行，注意要执行task

## Example
```c++
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
```
