#include "threadPool.h"
#include <iostream>
using namespace std;
using uLong = unsigned long long;
// 任务进行求和
class myTask : public Task {
public:
	myTask(int a, int b):
		begin_(a),
		end_(b){}

	Any run(){
		uLong sum = 0;
		for (int i = begin_; i <= end_; ++i) {
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return sum;

	}
private:
	int begin_;
	int end_;
};


int main() {
	{
		ThreadPool pool;
		pool.setThreadPoolMode(ThreadMode::MODE_CACHED);
		pool.start(2);

		// 提交submit返回result对象
		// 并将result对象传给当前的task作成员变量
		Result res1 = pool.submitTask(std::make_shared<myTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<myTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<myTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<myTask>(200000001, 300000000));
		pool.submitTask(std::make_shared<myTask>(200000001, 300000000));
		// 通过result获取Any结果，转为int
		uLong data1 = res1.get().cast_<uLong>();
		uLong data2 = res2.get().cast_<uLong>();
		cout << (data1 + data2 ) << endl;
	}

	
	cout << "main结束" << endl;
	getchar();
	return 0;
	
}