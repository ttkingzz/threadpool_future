#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include<vector>
#include<thread>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include <future>
#include <unordered_map>

const int TASK_MAX_THRESHHOLD = 3;
const int THREAD_MAX_THREADHHOLD = 8;
const int THREAD_PER_WAIT_TIME = 1;
const int THREAD_MAX_WAIT_COUNT = 2;

// 线程池支持的两种模式
// enum class 访问枚举型加上作用域
enum class PoolMode
{
	MODE_FIXED,  // 固定容量
	MODE_CACHED, // 可调节容量
};

// 线程类
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(size_t)>;
	
	Thread(ThreadFunc func)
		:func_(func)    // 接受bind的仿函数 保存
		,threadsKey_(threadsCount_++) // 先赋值 再+1
	{ }
	~Thread() = default;
	void start()
	{
		std::thread t(func_,threadsKey_);  // C++来说 线程对象t 和线程函数func_
		t.detach(); //分离线程 防止出了函数就挂掉 pthread_detach
	}
	size_t getThreadId() {return threadsKey_;}

private:
	ThreadFunc func_;  // 存一个函数对象 传进来一个可调用对象包装器，存起来
	static size_t threadsCount_;   // 所有对象共享
	size_t threadsKey_;
};

size_t Thread::threadsCount_=0;



// 线程类型
class ThreadPool
{
public:
	// 线程池构造
	ThreadPool()
		: initThreadSize_(0)
	, taskSize_(0)
	, idelThreadSize_(0)
	, curTthreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THREADHHOLD)
	, PoolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

	// 线程池析构
	~ThreadPool()
	{
		isPoolRunning_ = false;
		notEmpty_.notify_all();
		std::unique_lock<std::mutex> lock(taskQueMxt_);
		exitCond_.wait(lock, [&]()->bool {return curTthreadSize_ == 0; });   // 等待所有线程结束
	}
	//设置工作模式
	void setMode(PoolMode mode)
	{
		if (checkRunningState())
			return;
		PoolMode_ = mode;
	}
	// 设置任务上线阈值
	void setTaskQueMaxThreshHold(int threadhold)
	{
		taskQueMaxThreshHold_ = threadhold;
	}
	
	void setThreadSizeThreshHold(int threadSize)
	{
		if (checkRunningState())  // 正在运行中 不允许设置了
			return;
		if (PoolMode_ == PoolMode::MODE_FIXED)  // fixed模式 设置干嘛呢
			return;
		threadSizeThreshHold_ = threadSize;
	}
	// 提交任务 可变惨模板编程 
	// decltype推导出函数返回值类型 从而实例化future
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// 打包任务
		using RType = decltype(func(args...));  // 重命名类型
		auto task = std::make_shared<std::packaged_task<RType()> >(
			std::bind(std::forward<Func>(func),std::forward<Args>(args)...)
		);
		std::future<RType> result = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMxt_);
		while(taskQue_.size() >= (size_t)taskQueMaxThreshHold_)  // 任务队列满 提交超时
		{
			if (std::cv_status::timeout ==  notFull_.wait_for(lock, std::chrono::seconds(2)))
			{
				// 也可以直接抛出异常
				task = std::make_shared<std::packaged_task<RType()>>([]()->RType {return RType();} ); // 返回0值
				(*task)();  //智能指针，解引用访问packaed_task 执行一下任务
				return task->get_future();  // 返回future类型 future.get()阻塞等待任务执行完成
			}
		}
		taskQue_.emplace( [task](){ (*task)(); } ); // 套一层 捕获task
		taskSize_++;

		//通知notEmpt 可以拿任务了
		notEmpty_.notify_all();

		if (PoolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idelThreadSize_
			&& curTthreadSize_ < threadSizeThreshHold_
			)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
			auto threadIndex = ptr->getThreadId();
			threadsMap_.emplace(threadIndex,std::move(ptr));
			threadsMap_[threadIndex]->start();
			curTthreadSize_++;
			idelThreadSize_++;
		}
		return result;
	}
	// 严阵以待
	void start(int initThreshSize=std::thread::hardware_concurrency())
	{
		isPoolRunning_ = true;
		// 记录初始线程个数
		initThreadSize_ = initThreshSize;
		curTthreadSize_ = initThreshSize;

		// 创建线程对象
		for (int i = 0; i < initThreadSize_; ++i)
		{
			auto ptr = std::make_unique<Thread>( std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1) );
			auto threadId = ptr->getThreadId();
			threadsMap_.emplace(threadId,std::move(ptr));
		}
		// 启动每个线程
		for (int i = 0; i < initThreadSize_; ++i)
		{
			threadsMap_[i]->start();   // key：value即Thread
			idelThreadSize_++;  //记录初始的空闲线程
		}
	}

	// 不允许拷贝赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(size_t threadId)
	{
		unsigned int  SleepCount = 0;  // 计数空闲线程睡眠了多少次
		for (;;)
		{
			Task task;
			{
				// 先获取锁
				std::unique_lock<std::mutex> lock(taskQueMxt_);
				// 等待notEmpty
				//notEmpty_.wait(lock, [&]() ->bool{return taskQue_.size() > 0; });
				while(taskQue_.size() == 0)  // 双重判断，避免退出时还在wait造成死锁
				{
					if (!isPoolRunning_)
					{
						// 结束运行 线程退出
						//threads_.erase();  // 如何退出？ 不知道这个Thread是哪一个啊 用unordermap
						threadsMap_.erase(threadId);
						curTthreadSize_--;
						idelThreadSize_--;
						// 通知一下 可以不用等待
						exitCond_.notify_all();
						return;
					}

					// 如果是cacahed模式 空闲线程都在等待任务，需要减少
					if (PoolMode_ == PoolMode::MODE_CACHED
						&&curTthreadSize_>initThreadSize_)
					{
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(THREAD_PER_WAIT_TIME)))  // 等待并放锁
						{
							SleepCount++;
							if (SleepCount >= THREAD_MAX_WAIT_COUNT && curTthreadSize_ > initThreadSize_)
							{
								// 线程退出啦
								threadsMap_.erase(threadId);
								curTthreadSize_--;
								idelThreadSize_--;
								if (!isPoolRunning_)
									exitCond_.notify_all();
								SleepCount = 0;
								return;
							}
						}
					}
					else   // fixed 线程死等不自杀
					{
						notEmpty_.wait(lock);  // 结束等待：有任务 or 析构--->就绪态重新抢锁
					}
				}

				// 取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				// 消费了还是>0 来人还可以消费
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}
				notFull_.notify_all();
			}
			if (task != nullptr)
			{
				SleepCount = 0;
				idelThreadSize_--;  //空闲线程开始工作
				task();  //  执行任务 注意任务提交失败不会到达这里，因此任务提交失败之后就执行返回
				idelThreadSize_++;  //处理完了 空闲
			}
		}
	}

	//检测poo的运行状态
	bool checkRunningState() const
	{
		if (isPoolRunning_)
			return true;
		else return false;
	}
private:
	std::vector<std::unique_ptr<Thread>> threads_; //线程列表 裸指针需要手动析构 用智能指针 容器析构，元素析构 智能指针删除

	std::unordered_map< size_t, std::unique_ptr<Thread> > threadsMap_; // map下的Thread

	int initThreadSize_;   //初始的线程数量
	std::atomic_int idelThreadSize_; //空闲线程数量
	std::atomic_int curTthreadSize_;     // 记录当前线程数量
	int threadSizeThreshHold_; // 线程数量上线阈值

	// using Task = std::function<返回值()>  返回不知道 加个中间层无返回值的lambda
	using Task = std::function<void()>;   // 内部封装的task 生命周期确定
	std::queue<Task>  taskQue_;
	std::atomic_int taskSize_;  //任务的数量 原子类型
	int taskQueMaxThreshHold_;    //任务阈值
	//同时操作任务队列 
	std::mutex taskQueMxt_;    //保证任务队列的线程安全
	//两个条件变量 notFull notEmpty
	std::condition_variable notFull_;  //任务队列不满
	std::condition_variable notEmpty_; //任务队列不空
	std::condition_variable exitCond_; //等待资源回收

	PoolMode PoolMode_;  //工作模式

	// 当前线程池的启动状态
	std::atomic_bool isPoolRunning_;
	static int minThreadSize_;
};
#endif