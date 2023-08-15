#include "threadpool.h"
#include <chrono>
#include <iostream>
#include <thread>

class MyTask :public Task
{
public:
	MyTask(int begin, int end) :begin_(begin), end_(end) {}
	Any run()
	{
		//std::cout << "tid" << std::this_thread::get_id() << "begin" << std::endl;
		unsigned long long sum = 0;
		for (unsigned long long i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		//std::cout << "tid" << std::this_thread::get_id() << "end" << std::endl;
		return sum;
	}
private:
	int begin_;
	int end_;
};

int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		unsigned long long sum1 = res1.get().cast_<unsigned long long>();
		unsigned long long sum2 = res2.get().cast_<unsigned long long>();
		unsigned long long sum3 = res3.get().cast_<unsigned long long>();
	}
	std::cout << "exit main!" << std::endl;
	getchar();

	return 0;
}