#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any &) = delete;
	Any &operator=(const Any &) = delete;
	Any(Any &&) = default;
	Any &operator=(Any &&) = default;

	// 返回类型时寻找到单变量的构造函数，然后用基类指针base指向派生类对象derive
	template <typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	template <typename T>
	T cast_()
	{
		// 基类指针=》派生类指针 RTTI
		Derive<T> *pd = dynamic_cast<Derive<T> *>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch!";
		}
		return pd->data_;
	}

private:
	class Base
	{
	public:
		virtual ~Base() = default;
	};
	template <typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data) {}
		T data_;
	};

private:
	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore(int limit = 0) : resLimit_(limit) {}
	~Semaphore() = default;

	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]() -> bool
				   { return resLimit_ > 0; });
		resLimit_--;
	}

	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}

private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	// Task获取任务执行完的返回值
	void setVal(Any any);
	// 用户调用方法获取task返回值
	Any get();

private:
	Any any_;					 // 任务返回值
	Semaphore sem_;				 // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_;	 // 任务是否有效
};

// 任务抽象类
class Task
{
public:
	Task();
	~Task() = default;
	// exec中包含run的执行
	void exec();
	void setResult(Result *res);
	// 重写run方法可以实现多态自定义任务处理
	virtual Any run() = 0;

private:
	// 将任务返回值写入result
	Result *result_;
};

enum PoolMode
{
	MODE_FIXED,	 // 线程数量固定
	MODE_CACHED, // 数量可增长
};

class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	// 启动线程

	Thread(ThreadFunc func);

	~Thread();

	void start();

	int getId() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;
};

class ThreadPool
{
public:
	ThreadPool();

	~ThreadPool();

	ThreadPool(const ThreadPool &) = delete;

	ThreadPool &operator=(const ThreadPool &) = delete;

	void setMode(PoolMode mode);

	void setTaskQueMaxThreshHold(int threshhold);

	void setThreadSizeThresHold(int threshhold); // Cache模式下线程数量上限

	// 提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	void start(int initThreadSize = 4);

private:
	void threadFunc(int threadid);

	// 检查Pool的运行状态
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_;//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程列表

	int initThreadSize_; // 初始线程数量

	int threadSizeThresHold_; // 线程数量上限阈值

	std::atomic_int curThreadSize_; // 线程总数量

	std::atomic_int idleThreadSize_; // 空闲线程的数量

	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列

	std::atomic_int taskSize_; // 任务的数量

	int taskQueMaxSizeThreshHold_; // 任务队列上限阈值

	std::mutex taskQueMtx_; // 保证任务队列的线程安全

	std::condition_variable notFull_; // 表示任务队列不满

	std::condition_variable notEmpty_; // 表示任务队列不空

	std::condition_variable exitCond_; // 等待线程资源全部回收

	PoolMode poolMode_;

	std::atomic_bool isPoolRunning_;
};
#endif