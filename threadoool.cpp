#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10; // SECONDS

ThreadPool::ThreadPool()
	: initThreadSize_(0),
	  taskSize_(0),
	  taskQueMaxSizeThreshHold_(TASK_MAX_THRESHHOLD),
	  threadSizeThresHold_(THREAD_MAX_THRESHHOLD),
	  poolMode_(PoolMode::MODE_FIXED),
	  isPoolRunning_(false),
	  idleThreadSize_(0),
	  curThreadSize_(0)
{
}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// 等待所有线程结束返回
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]() -> bool
				   { return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
	{
		return;
	}
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	taskQueMaxSizeThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThresHold(int threshhold)
{
	if (checkRunningState())
	{
		return;
	}
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThresHold_ = threshhold;
	}
}

// 提交任务 用户调用接口，传入对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 线程通信，等待任务队列有空余
	// 用户提交任务，最长不能阻塞超过1s，否则任务提交失败返回
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
						   [&]() -> bool
						   { return taskQue_.size() < (size_t)taskQueMaxSizeThreshHold_; }))
	{
		// 表示等待1s条件依然没有满足
		std::cerr << "task queue is full,sumbit task fail." << std::endl;
		return Result(sp, false);
	}

	// 如果有空余，将任务放进任务队列中
	taskQue_.emplace(sp);
	taskSize_++;
	// 因为新放了任务，任务队列肯定不空了，在notEmpty通知
	notEmpty_.notify_all();

	// cache模式，任务处理紧急，场景：小而快的任务
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThresHold_)
	{
		// 创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}
	return Result(sp);
}

void ThreadPool::start(int initThreadsize)
{
	isPoolRunning_ = true;

	initThreadSize_ = initThreadsize;
	curThreadSize_ = initThreadsize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		curThreadSize_++;
		/*threads_.emplace_back(std::move(ptr));*/
	}
	// 启动线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++; // 记录初始空闲线程的数量
	}
}

// 从任务队列里消费队伍
void ThreadPool::threadFunc(int threadid)
{
	/*std::cout << "begin threadFunc" << std::endl;*/
	auto lastTime = std::chrono::high_resolution_clock().now();
	// 任务必须执行
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
			// 每一秒返回一次
			while (taskQue_.size() == 0)
			{
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
				// cached模式下，空闲线程超过60s需要回收超过initThreadSize的线程
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 超时返回
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							// 回收当前线程
							// 记录线程数量值修改
							// 把线程对象从线程列表容器中删除
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notempty
					notEmpty_.wait(lock);
				}
			}

			idleThreadSize_--;
			std::cout << "tid" << std::this_thread::get_id() << "获取任务成功..." << std::endl;

			// 从任务队列中取任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有任务，通知其他线程取得任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 取出一个任务进行通知
			notFull_.notify_all();
		} // 释放锁

		// 执行任务
		if (task != nullptr)
		{
			// task->run();
			task->exec();
		}
		idleThreadSize_++;
		// 更新执行完任务的时间
		auto lastTime = std::chrono::high_resolution_clock().now();
	}
}
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}
// 线程方法实现-----------------------------------------------------------
int Thread::generateId_ = 0;

int Thread::getId() const
{
	return threadId_;
}

Thread::Thread(ThreadFunc func)
	: func_(func),
	  threadId_(generateId_++)
{
}

Thread::~Thread()
{
}
void Thread::start()
{
	// 创建一个线程执行线程函数
	std::thread t(func_, threadId_);
	t.detach();
}
//---------------------------------------------Task
Task::Task()
	: result_(nullptr)
{
}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());
	}
}

void Task::setResult(Result *res)
{
	result_ = res;
}
//---------------------------------------------Result

Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid), task_(task)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // 如果任务没有执行完，会阻塞用户线程
	return std::move(any_);
}

void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();
}