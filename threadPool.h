#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <unordered_map>

// ==================================����ź�����
class Semaphore {
public:
	Semaphore(int limit = 0) : limit_(limit), isExit_(false){}
	~Semaphore() {
		isExit_ = true;
	};

	// ʹ���ź���
	void wait() {
		if (isExit_) return;
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ�ʹ��
		cond_.wait(lock, [&]()->bool {return limit_ > 0;});
		limit_--;

	}
	// �黹�ź���
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
// ===================================���Any���ͣ����Խ�����������
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; // ��ֵ����ɾ��
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<class T>
	// ͨ������ָ��ָ����������󣬽���ͬ���͵�data������������Dervie�ĳ�Ա������������
	Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

	// ��ȡ�����data����
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
		virtual ~Base() = default; // ���ɶ�̬
	};

	template<class T>
	class Derive : public Base {
	public:
		// ����data����
		Derive(T data) : data_(data) {}
		T data_;
	};
private:
	std::unique_ptr<Base> base_;  // ͨ������ָ�����
};

// ============================ result���ͣ�submitTask�ķ������ͣ���Any������ź���
// ǰ������
class Task;
class Result {
public:
	Result(std::shared_ptr<Task> t, bool flag = true);
	~Result() = default;

	// ����1���̵߳���getAny��д��taskִ�н��
	void setAny(Any any);
	// ����2���û�����get�������õ�any_
	Any get(); 

private:
	Any any_;
	Semaphore sem_;
	// ��������
	std::shared_ptr<Task> task_;
	std::atomic_bool isVaild_;
};

// �ύ��������
// ����࣬���������д
class Task {
public:
	Task();
	~Task() = default;
	virtual Any run() = 0;  // ִ������󷵻��������ͽ��
	void exec();
	void setResult(Result* res);
private:
	Result* res_; // �����ⲿ��res��������any���
};

//================================= �߳���,���ÿһ���̵߳Ĳ����ķ�װ
class Thread {
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();
	// �����߳�
	void start();
	int getID();
private:
	// �����߳�ִ�к���
	ThreadFunc func_;
	int threadID_;
	static int generalID_;
};

// �̳߳�ģʽ
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

	// �����̳߳�ģʽ
	void setThreadPoolMode(ThreadMode mode);

	// ����cachedģʽ���̳߳������ֵ
	void setThreadMaxShrehHold(int shreshHold);
	// ����������������ֵ
	void setTaskQueMaxShrehHold(int shreshHold);
	// �����̳߳�
	void start(int size);
	// �ύ����
	Result submitTask(std::shared_ptr<Task> sp);
private:
	// �߳�ִ�к���
	void threadFunc_(int threatId);
	// �������״̬������ʱ�������޸Ĳ���
	bool checkPoolState();
private:
	// �̶߳���,ͨ������ָ����ڴ�й©
	// std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	// ��ʼ���̸߳���
	size_t threadInitSize_;
	// �߳�����
	std::atomic_int threadNum_;
	// ����߳���ֵ
	size_t threadsMaxShreshHold_;
	// �����̸߳���
	std::atomic_int idleThreadSize_;
	// ��ǰ�̳߳�ģʽ
	ThreadMode tMode_;
	// ��¼�߳�״̬
	std::atomic_bool isPoolRunning_;
	// �������
	// Ϊ��֤task���������ڣ���������ָ�����
	std::queue<std::shared_ptr<Task>> taskQue_;
	// ��¼�������
	std::atomic_int taskQueSize_;
	// ���������ֵ
	size_t taskQueMaxshreshHold_;
	// �߳�ͬ��
	std::mutex mtx_;
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;
	std::condition_variable poolClose_;

};

