#include "threadPool.h"
#include <functional>
#include <iostream>
#include <mutex>
#include <condition_variable>

const int TASK_MAX_SIZE = 1024;
const int THREAD_MAX_SIZE = 10;
const int THREAD_IDLE_TIME = 10; // 秒

ThreadPool::ThreadPool()
	:threadInitSize_(0),
	taskQueSize_(0),
	tMode_(ThreadMode::MODE_FIXED),
	taskQueMaxshreshHold_(TASK_MAX_SIZE),
	threadsMaxShreshHold_(THREAD_MAX_SIZE),
	isPoolRunning_(false),
	idleThreadSize_(0),
	threadNum_(0)
{
}

ThreadPool::~ThreadPool()
{
	// 关闭线程池
	isPoolRunning_ = false;
	// 唤醒所有睡眠的线程
	notEmpty_.notify_all();

	// 等待所有线程析构 
	std::unique_lock<std::mutex> lock(mtx_);
	poolClose_.wait(lock, [&]()->bool {return threads_.size() == 0;});

	std::cout << "线程池结束 " << std::endl;
	
}

void ThreadPool::setThreadPoolMode(ThreadMode mode)
{
	if (checkPoolState()) return;
	tMode_ = mode;
}

void ThreadPool::setThreadMaxShrehHold(int shreshHold)
{
	if (checkPoolState()) return;
	if (tMode_ == ThreadMode::MODE_CACHED) {
		threadsMaxShreshHold_ = shreshHold;
	}
	
}

void ThreadPool::setTaskQueMaxShrehHold(int shreshHold)
{
	if (checkPoolState()) return;
	taskQueMaxshreshHold_ = shreshHold;
}

void ThreadPool::start(int size)
{
	threadInitSize_ = size;
	// 创建设定数量的线程类，传入线程执行函数
	for (int i = 0; i < threadInitSize_; ++i) {
		
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc_, this, std::placeholders::_1)); // 加入参数占位符
		int id = ptr->getID();
		threads_.emplace(id, std::move(ptr));
	}
	// 所有线程开始工作, i 即为 key
	for (int i = 0; i < threadInitSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
		threadNum_++;
	}
	isPoolRunning_ = true;
}

// =============== 线程函数执行任务 ============
void ThreadPool::threadFunc_(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock::now();
	// 线程池启动时，线程循环执行
	for (;;) {
		std::shared_ptr<Task> task;
		{
			// 上锁，取完任务后析构
			std::unique_lock<std::mutex> lock(mtx_);
			// log
			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务......" << std::endl;
			
			// 任务队列为0，线程轮询阻塞
			while (taskQue_.size() == 0) {

				// 判断线程池是否关闭
				if (!isPoolRunning_) {
					// 线程池结束
					threads_.erase(threadId);
					std::cout << "线程池结束 tid: " << threadId << "已删除！" << std::endl;
					poolClose_.notify_all();
					return; // 线程结束
				}
				// 1 cached模式，若超过60s没有任务，则删除线程   
				if (tMode_ == ThreadMode::MODE_CACHED) {
					// 获取wait_for返回值，timeout超时返回，not_timeout获取任务正常范围
					auto res = notEmpty_.wait_for(lock, std::chrono::seconds(1));
					if (std::cv_status::timeout == res) { // 超时返回，则记录线程空闲时间
						auto curTime = std::chrono::high_resolution_clock::now();
						auto durTime = std::chrono::duration_cast<std::chrono::seconds>(curTime - lastTime);
						if (durTime.count() >= THREAD_IDLE_TIME && threadNum_ > threadInitSize_) {// 超时60s删除
							// 删除超时线程
							// 因此需要加入threadID, 并用unorder_map储存
							threads_.erase(threadId); // 只是删除了map内的记录
							threadNum_--;
							idleThreadSize_--;
							std::cout << "超时线程 tid: " << threadId << "已删除！" << std::endl;
							return; // 线程返回即删除
						}
					}
				}
				else {
					// 2 fixed模式，一直等待任务阻塞
					notEmpty_.wait(lock);
				}
				
			}

			
			// 取任务
			idleThreadSize_--;
			task = taskQue_.front();
			taskQue_.pop();
			taskQueSize_--;
			std::cout << "tid: " << std::this_thread::get_id() << "获取成功！" << std::endl;
			// 唤醒其他线程
			if (taskQueSize_ > 0) notEmpty_.notify_all();
			
			// 唤醒用户线程提交
			notFull_.notify_all();

		}
		
		// 释放锁后执行任务,并将结果存储
		// if (task != nullptr)  task->exec();
		if (task != nullptr) {
			try {
				task->exec();
			}
			catch (const std::exception& e) {
				std::cerr << "Exception in thread: " << e.what() << std::endl;
				// 可以在此处记录异常并采取适当的措施
			}
		}
		idleThreadSize_++;

		// 记录线程最后工作时间
		lastTime = std::chrono::high_resolution_clock::now();

	}
}

bool ThreadPool::checkPoolState()
{
	return isPoolRunning_;
}

// ================ 用户提交任务 =============
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 上锁
	std::unique_lock<std::mutex> lock(mtx_);
	// 线程通信，队列不满时提交任务，否则wait，等待1s超时
	if (!notFull_.wait_for(
		lock,
		std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxshreshHold_;}
	)) {
		std::cerr << "超时，task提交失败" << std::endl;
		return Result(sp, false);
	}

	// 不满则提交任务
	taskQue_.emplace(sp);
	taskQueSize_++;

	// 队列不空，唤醒线程执行任务
	notEmpty_.notify_all();

	// 若模式为cached模式，判断增添线程
	if (tMode_ == ThreadMode::MODE_CACHED && (taskQue_.size() > idleThreadSize_) && (threadNum_ < threadsMaxShreshHold_)) {
		// 创建线程
		
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc_, this, std::placeholders::_1));
		int id = ptr->getID();
		std::cout << "额外创建线程 tid: " << id <<" ..." << std::endl;
		threads_.emplace(id, std::move(ptr));
		threadNum_++;
		threads_[id]->start();

	}

	// 完毕则返回result类型结果
	// 此时将生成的result对象作为输出参数传给task
	// task内部run后将结果记录再request内
	return Result(sp);
}

// ======= 线程类 ===========
int Thread::generalID_ = 0;

Thread::Thread(ThreadFunc func) :
	func_(func),
	threadID_(generalID_++)  // 创建线程id
{
}

Thread::~Thread()
{
}

void Thread::start()
{
	// 传入线程执行函数，创建线程并执行
	std::thread t(func_, this->threadID_);
	// 分离线程
	t.detach();

}

int Thread::getID()
{
	return threadID_;
}


Task::Task() : res_(nullptr)
{
	
}

void Task::exec()
{
	if (res_ != nullptr) {
		// 执行任务,并传递结果
		// 设置信号量，允许get获得返回值
		res_->setAny(run());
	}
	
}

void Task::setResult(Result* res)
{
	res_ = res;
}

// 本质：将自己传给task
Result::Result(std::shared_ptr<Task> t, bool flag)
	: task_(t), isVaild_(flag)
{
	t->setResult(this);
}

void Result::setAny(Any any)
{
	any_ = std::move(any);
	sem_.post(); // 写入完毕，可以get结果
}

Any Result::get()
{
	if (!isVaild_) return "";
	// 等待任务结束，阻塞
	sem_.wait();
	return std::move(any_); // 注意unique_ptr的右值构造
}
