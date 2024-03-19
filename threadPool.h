#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unordered_map>

// ==================================设计信号量类
class Semaphore {
public:
	Semaphore(int limit = 0) : limit_(limit), isExit_(false){}
	~Semaphore() {
		isExit_ = true;
	};

	// 使用信号量
	void wait() {
		if (isExit_) return;
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待使用
		cond_.wait(lock, [&]()->bool {return limit_ > 0;});
		limit_--;

	}
	// 归还信号量
	void post() {
		if (isExit_) return;
		std::unique_lock<std::mutex> lock(mtx_);
		limit_++;
		cond_.notify_all();
	}
private:
	std::atomic_bool isExit_;
	int limit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};
// ===================================设计Any类型，可以接收所有类型
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; // 左值引用删除
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<class T>
	// 通过基类指针指向派生类对象，将不同类型的data包含在派生类Dervie的成员变量中来保存
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	// 获取保存的data数据
	template<class T>
	T cast_() {
		
		Derive<T>* ptr = dynamic_cast<Derive<T>*>(base_.get());
		
		if (ptr == nullptr) {
			throw "type is unmatch!";
		}
		return ptr->data_;
	}
private:
	class Base {
	public:
		virtual ~Base() = default; // 构成多态
	};

	template<class T>
	class Derive : public Base {
	public:
		// 保存data数据
		Derive(T data) : data_(data) {}
		T data_;
	};
private:
	std::unique_ptr<Base> base_;  // 通过智能指针控制
};

// ============================ result类型，submitTask的返回类型，即Any上添加信号量
// 前置声明
class Task;
class Result {
public:
	Result(std::shared_ptr<Task> t, bool flag = true);
	~Result() = default;

	// 问题1：线程调用getAny，写入task执行结果
	void setAny(Any any);
	// 问题2：用户调用get方法，得到any_
	Any get(); 

private:
	Any any_;
	Semaphore sem_;
	// 保存任务
	std::shared_ptr<Task> task_;
	std::atomic_bool isVaild_;
};

// 提交任务类型
// 虚基类，子类进行重写
class Task {
public:
	Task();
	~Task() = default;
	virtual Any run() = 0;  // 执行任务后返回任意类型结果
	void exec();
	void setResult(Result* res);
private:
	Result* res_; // 接收外部的res，来传递any结果
};

//================================= 线程类,针对每一个线程的操作的封装
class Thread {
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();
	// 启动线程
	void start();
	int getID();
private:
	// 保留线程执行函数
	ThreadFunc func_;
	int threadID_;
	static int generalID_;
};

// 线程池模式
enum class ThreadMode {
	MODE_FIXED,
	MODE_CACHED
};
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator= (const ThreadPool&) = delete;

	// 设置线程池模式
	void setThreadPoolMode(ThreadMode mode);

	// 设置cached模式下线程池最大阈值
	void setThreadMaxShrehHold(int shreshHold);
	// 设置任务队列最大阈值
	void setTaskQueMaxShrehHold(int shreshHold);
	// 启动线程池
	void start(int size);
	// 提交任务
	Result submitTask(std::shared_ptr<Task> sp);
private:
	// 线程执行函数
	void threadFunc_(int threatId);
	// 检查运行状态，运行时不允许修改参数
	bool checkPoolState();
private:
	// 线程队列,通过智能指针防内存泄漏
	// std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	// 初始化线程个数
	size_t threadInitSize_;
	// 线程总数
	std::atomic_int threadNum_;
	// 最大线程阈值
	size_t threadsMaxShreshHold_;
	// 空闲线程个数
	std::atomic_int idleThreadSize_;
	// 当前线程池模式
	ThreadMode tMode_;
	// 记录线程状态
	std::atomic_bool isPoolRunning_;
	// 任务队列
	// 为保证task的声明周期，采用智能指针控制
	std::queue<std::shared_ptr<Task>> taskQue_;
	// 记录任务个数
	std::atomic_int taskQueSize_;
	// 任务队列阈值
	size_t taskQueMaxshreshHold_;
	// 线程同步
	std::mutex mtx_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable poolClose_;

};

