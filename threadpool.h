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

	// ��������ʱѰ�ҵ��������Ĺ��캯����Ȼ���û���ָ��baseָ�����������derive
	template <typename T>
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	template <typename T>
	T cast_()
	{
		// ����ָ��=��������ָ�� RTTI
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
	// Task��ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	// �û����÷�����ȡtask����ֵ
	Any get();

private:
	Any any_;					 // ���񷵻�ֵ
	Semaphore sem_;				 // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_;	 // �����Ƿ���Ч
};

// ���������
class Task
{
public:
	Task();
	~Task() = default;
	// exec�а���run��ִ��
	void exec();
	void setResult(Result *res);
	// ��дrun��������ʵ�ֶ�̬�Զ���������
	virtual Any run() = 0;

private:
	// �����񷵻�ֵд��result
	Result *result_;
};

enum PoolMode
{
	MODE_FIXED,	 // �߳������̶�
	MODE_CACHED, // ����������
};

class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	// �����߳�

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

	void setThreadSizeThresHold(int threshhold); // Cacheģʽ���߳���������

	// �ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	void start(int initThreadSize = 4);

private:
	void threadFunc(int threadid);

	// ���Pool������״̬
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�

	int initThreadSize_; // ��ʼ�߳�����

	int threadSizeThresHold_; // �߳�����������ֵ

	std::atomic_int curThreadSize_; // �߳�������

	std::atomic_int idleThreadSize_; // �����̵߳�����

	std::queue<std::shared_ptr<Task>> taskQue_; // �������

	std::atomic_int taskSize_; // ���������

	int taskQueMaxSizeThreshHold_; // �������������ֵ

	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ

	std::condition_variable notFull_; // ��ʾ������в���

	std::condition_variable notEmpty_; // ��ʾ������в���

	std::condition_variable exitCond_; // �ȴ��߳���Դȫ������

	PoolMode poolMode_;

	std::atomic_bool isPoolRunning_;
};
#endif