#include "threadPool.h"
#include <functional>
#include <iostream>
#include <mutex>
#include <condition_variable>

const int TASK_MAX_SIZE = 1024;
const int THREAD_MAX_SIZE = 10;
const int THREAD_IDLE_TIME = 10; // ��

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
	// �ر��̳߳�
	isPoolRunning_ = false;
	// ��������˯�ߵ��߳�
	notEmpty_.notify_all();

	// �ȴ������߳����� 
	std::unique_lock<std::mutex> lock(mtx_);
	poolClose_.wait(lock, [&]()->bool {return threads_.size() == 0;});

	std::cout << "�̳߳ؽ��� " << std::endl;
	
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
	// �����趨�������߳��࣬�����߳�ִ�к���
	for (int i = 0; i < threadInitSize_; ++i) {
		
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc_, this, std::placeholders::_1)); // �������ռλ��
		int id = ptr->getID();
		threads_.emplace(id, std::move(ptr));
	}
	// �����߳̿�ʼ����, i ��Ϊ key
	for (int i = 0; i < threadInitSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
		threadNum_++;
	}
	isPoolRunning_ = true;
}

// =============== �̺߳���ִ������ ============
void ThreadPool::threadFunc_(int threadId)
{
	auto lastTime = std::chrono::high_resolution_clock::now();
	// �̳߳�����ʱ���߳�ѭ��ִ��
	for (;;) {
		std::shared_ptr<Task> task;
		{
			// ������ȡ�����������
			std::unique_lock<std::mutex> lock(mtx_);
			// log
			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����......" << std::endl;
			
			// �������Ϊ0���߳���ѯ����
			while (taskQue_.size() == 0) {

				// �ж��̳߳��Ƿ�ر�
				if (!isPoolRunning_) {
					// �̳߳ؽ���
					threads_.erase(threadId);
					std::cout << "�̳߳ؽ��� tid: " << threadId << "��ɾ����" << std::endl;
					poolClose_.notify_all();
					return; // �߳̽���
				}
				// 1 cachedģʽ��������60sû��������ɾ���߳�   
				if (tMode_ == ThreadMode::MODE_CACHED) {
					// ��ȡwait_for����ֵ��timeout��ʱ���أ�not_timeout��ȡ����������Χ
					auto res = notEmpty_.wait_for(lock, std::chrono::seconds(1));
					if (std::cv_status::timeout == res) { // ��ʱ���أ����¼�߳̿���ʱ��
						auto curTime = std::chrono::high_resolution_clock::now();
						auto durTime = std::chrono::duration_cast<std::chrono::seconds>(curTime - lastTime);
						if (durTime.count() >= THREAD_IDLE_TIME && threadNum_ > threadInitSize_) {// ��ʱ60sɾ��
							// ɾ����ʱ�߳�
							// �����Ҫ����threadID, ����unorder_map����
							threads_.erase(threadId); // ֻ��ɾ����map�ڵļ�¼
							threadNum_--;
							idleThreadSize_--;
							std::cout << "��ʱ�߳� tid: " << threadId << "��ɾ����" << std::endl;
							return; // �̷߳��ؼ�ɾ��
						}
					}
				}
				else {
					// 2 fixedģʽ��һֱ�ȴ���������
					notEmpty_.wait(lock);
				}
				
			}

			
			// ȡ����
			idleThreadSize_--;
			task = taskQue_.front();
			taskQue_.pop();
			taskQueSize_--;
			std::cout << "tid: " << std::this_thread::get_id() << "��ȡ�ɹ���" << std::endl;
			// ���������߳�
			if (taskQueSize_ > 0) notEmpty_.notify_all();
			
			// �����û��߳��ύ
			notFull_.notify_all();

		}
		
		// �ͷ�����ִ������,��������洢
		// if (task != nullptr)  task->exec();
		if (task != nullptr) {
			try {
				task->exec();
			}
			catch (const std::exception& e) {
				std::cerr << "Exception in thread: " << e.what() << std::endl;
				// �����ڴ˴���¼�쳣����ȡ�ʵ��Ĵ�ʩ
			}
		}
		idleThreadSize_++;

		// ��¼�߳������ʱ��
		lastTime = std::chrono::high_resolution_clock::now();

	}
}

bool ThreadPool::checkPoolState()
{
	return isPoolRunning_;
}

// ================ �û��ύ���� =============
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ����
	std::unique_lock<std::mutex> lock(mtx_);
	// �߳�ͨ�ţ����в���ʱ�ύ���񣬷���wait���ȴ�1s��ʱ
	if (!notFull_.wait_for(
		lock,
		std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxshreshHold_;}
	)) {
		std::cerr << "��ʱ��task�ύʧ��" << std::endl;
		return Result(sp, false);
	}

	// �������ύ����
	taskQue_.emplace(sp);
	taskQueSize_++;

	// ���в��գ������߳�ִ������
	notEmpty_.notify_all();

	// ��ģʽΪcachedģʽ���ж������߳�
	if (tMode_ == ThreadMode::MODE_CACHED && (taskQue_.size() > idleThreadSize_) && (threadNum_ < threadsMaxShreshHold_)) {
		// �����߳�
		
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc_, this, std::placeholders::_1));
		int id = ptr->getID();
		std::cout << "���ⴴ���߳� tid: " << id <<" ..." << std::endl;
		threads_.emplace(id, std::move(ptr));
		threadNum_++;
		threads_[id]->start();

	}

	// ����򷵻�result���ͽ��
	// ��ʱ�����ɵ�result������Ϊ�����������task
	// task�ڲ�run�󽫽����¼��request��
	return Result(sp);
}

// ======= �߳��� ===========
int Thread::generalID_ = 0;

Thread::Thread(ThreadFunc func) :
	func_(func),
	threadID_(generalID_++)  // �����߳�id
{
}

Thread::~Thread()
{
}

void Thread::start()
{
	// �����߳�ִ�к����������̲߳�ִ��
	std::thread t(func_, this->threadID_);
	// �����߳�
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
		// ִ������,�����ݽ��
		// �����ź���������get��÷���ֵ
		res_->setAny(run());
	}
	
}

void Task::setResult(Result* res)
{
	res_ = res;
}

// ���ʣ����Լ�����task
Result::Result(std::shared_ptr<Task> t, bool flag)
	: task_(t), isVaild_(flag)
{
	t->setResult(this);
}

void Result::setAny(Any any)
{
	any_ = std::move(any);
	sem_.post(); // д����ϣ�����get���
}

Any Result::get()
{
	if (!isVaild_) return "";
	// �ȴ��������������
	sem_.wait();
	return std::move(any_); // ע��unique_ptr����ֵ����
}
